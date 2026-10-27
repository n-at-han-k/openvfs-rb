#include <rice/rice.hpp>
#include <rice/stl.hpp>
#include <ruby/thread.h>

#include <libsync/account.h>
#include <libsync/accessmanager.h>
#include <libsync/creds/abstractcredentials.h>
#include <libsync/propagateupload.h>
#include <libsync/common/utility.h>
#include <libsync/common/version.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace OpenVFSRuby {

namespace {

void ensure_application()
{
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char name[] = "openvfs-rb";
        static char *argv[] = {name, nullptr};
        // The embedded Qt runtime lives for the lifetime of the Ruby process.
        new QCoreApplication(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("openvfs-rb"));
    }
}

void check_thread()
{
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        throw std::runtime_error("OpenVFS::OCC objects must be used on the thread that loaded openvfs/occ");
    }
}

QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QUrl server_url(const std::string &value)
{
    const QUrl url(text(value), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
        throw std::invalid_argument("url must be an HTTP(S) WebDAV root without credentials, query, or fragment");
    }
    return url;
}

QString access_token(const std::string &value)
{
    if (value.empty() || value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos) {
        throw std::invalid_argument("access_token must be nonempty and contain no control separators");
    }
    return text(value);
}

// Credentials supplied by the Ruby caller stay in memory. Desktop's AccessManager
// and AbstractNetworkJob still handle TLS, request IDs, streaming, and errors.
class TokenAccessManager : public ::OCC::AccessManager
{
public:
    TokenAccessManager(QString token, QObject *parent)
        : ::OCC::AccessManager(parent), token_(std::move(token))
    {
    }

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *body) override
    {
        QNetworkRequest authenticated(request);
        authenticated.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
        return ::OCC::AccessManager::createRequest(operation, authenticated, body);
    }

private:
    QString token_;
};

class TokenCredentials : public ::OCC::AbstractCredentials
{
public:
    explicit TokenCredentials(QString token) : token_(std::move(token)) {}

    ::OCC::AccessManager *createAM() const override { return new TokenAccessManager(token_, _account); }
    bool ready() const override { return !token_.isEmpty(); }
    void fetchFromKeychain() override
    {
        _wasFetched = true;
        Q_EMIT fetched();
    }
    void restartOauth() override { Q_EMIT authenticationFailed(); }
    void persist() override {}
    void invalidateToken() override { token_.clear(); }
    void forgetSensitiveData() override { invalidateToken(); }

private:
    QString token_;
};

struct AccountState
{
    ::OCC::AccountPtr account;
    int activeJobs = 0;

    ~AccountState()
    {
        if (account && account->thread() != QThread::currentThread()) {
            // Ruby may collect an object on a different thread; Qt must destroy it
            // on its owning thread, including its network access manager.
            QMetaObject::invokeMethod(QCoreApplication::instance(), [owned = std::move(account)]() mutable { owned.clear(); }, Qt::QueuedConnection);
        }
    }
};

}

class Account
{
public:
    Account(const std::string &url, const std::string &token)
    {
        check_thread();
        const auto endpoint = server_url(url);
        const auto bearer = access_token(token);
        state_ = std::make_shared<AccountState>();
        state_->account = ::OCC::Account::create(QUuid::createUuid());
        state_->account->setUrl(endpoint);
        state_->account->setCredentials(new TokenCredentials(bearer));
    }

    std::string url() const
    {
        check_thread();
        return state_->account->url().toString().toStdString();
    }

    std::string uuid() const
    {
        check_thread();
        return state_->account->uuid().toString(QUuid::WithoutBraces).toStdString();
    }

    void set_access_token(const std::string &token)
    {
        check_thread();
        if (state_->activeJobs != 0) {
            throw std::runtime_error("cannot replace credentials while an upload is running");
        }
        state_->account->setCredentials(new TokenCredentials(access_token(token)));
    }

    std::shared_ptr<AccountState> state() const { return state_; }

private:
    std::shared_ptr<AccountState> state_;
};

struct UploadResult
{
    int http_status = 0;
    int network_error = 0;
    std::string etag;
    std::string file_id;
    std::string request_id;
    std::string response_body;
    std::string error;
    int64_t bytes_sent = 0;
    int64_t total_bytes = 0;
    int64_t elapsed_ms = 0;
    bool aborted = false;
    bool timed_out = false;

    bool ok() const
    {
        return error.empty() && network_error == 0 && (http_status == 200 || http_status == 201 || http_status == 204);
    }
};

namespace {

struct UploadState
{
    std::shared_ptr<AccountState> account;
    QPointer<::OCC::PUTFileJob> job;
    QElapsedTimer elapsed;
    UploadResult result;
    bool started = false;
    bool complete = false;

    void finish()
    {
        if (!complete) {
            complete = true;
            result.elapsed_ms = elapsed.elapsed();
            --account->activeJobs;
        }
    }
};

struct WaitSlice
{
    UploadState *state;
    std::exception_ptr exception;
};

void *wait_slice(void *argument)
{
    auto &slice = *static_cast<WaitSlice *>(argument);
    try {
        if (!slice.state->complete) {
            QEventLoop loop;
            QTimer::singleShot(20, &loop, &QEventLoop::quit);
            QObject::connect(slice.state->job, &::OCC::AbstractNetworkJob::finishedSignal, &loop, &QEventLoop::quit);
            loop.exec();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    } catch (...) {
        slice.exception = std::current_exception();
    }
    return nullptr;
}

}

class PUTFileJob
{
public:
    PUTFileJob(Account &account, const std::string &localPath, const std::string &remotePath,
        const std::optional<std::string> &etag, bool createOnly, int timeoutMs)
        : localPath_(text(localPath)), remotePath_(text(remotePath)), etag_(etag), createOnly_(createOnly), timeoutMs_(timeoutMs)
    {
        check_thread();
        if (localPath.empty() || localPath.find('\0') != std::string::npos) {
            throw std::invalid_argument("local_path must be a nonempty filesystem path");
        }
        if (remotePath.empty() || remotePath_.startsWith(QLatin1Char('/')) || remotePath_.contains(QLatin1Char('\0'))
            || remotePath_.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            throw std::invalid_argument("remote_path must be relative to the account's WebDAV root");
        }
        if (timeoutMs <= 0) {
            throw std::invalid_argument("timeout_ms must be positive");
        }
        if (etag && (etag->empty() || etag->find_first_of("\r\n") != std::string::npos || etag->find('\0') != std::string::npos)) {
            throw std::invalid_argument("etag must be nonempty and contain no control separators");
        }
        if (createOnly && etag) {
            throw std::invalid_argument("create_only and etag are mutually exclusive");
        }
        state_ = std::make_shared<UploadState>();
        state_->account = account.state();
        context_ = new QObject;
    }

    PUTFileJob(const PUTFileJob &) = delete;
    PUTFileJob &operator=(const PUTFileJob &) = delete;

    ~PUTFileJob()
    {
        auto cleanup = [context = context_, state = state_] {
            if (state->job && !state->complete) {
                state->result.aborted = true;
                state->job->abort();
            }
            delete context;
        };
        if (context_->thread() == QThread::currentThread()) {
            cleanup();
        } else {
            QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(cleanup), Qt::QueuedConnection);
        }
    }

    void start()
    {
        check_thread();
        if (state_->started) {
            throw std::runtime_error("an upload job can only be started once");
        }
        auto file = std::make_unique<QFile>(localPath_);
        const QFileInfo info(localPath_);
        if (!info.isFile()) {
            throw std::invalid_argument("local_path must name an existing regular file");
        }
        if (!file->open(QIODevice::ReadOnly)) {
            throw std::runtime_error(file->errorString().toStdString());
        }
        state_->result.total_bytes = file->size();
        ::OCC::HeaderMap headers;
        headers["Content-Type"] = "application/octet-stream";
        headers["OC-Total-Length"] = QByteArray::number(file->size());
        headers["X-OC-Mtime"] = QByteArray::number(info.lastModified().toSecsSinceEpoch());
        if (etag_) {
            auto match = QByteArray::fromStdString(*etag_);
            if (match != "*" && !match.startsWith('"') && !match.startsWith("W/\"")) {
                match = '"' + match + '"';
            }
            headers["If-Match"] = match;
        }
        if (createOnly_) {
            headers["If-None-Match"] = "*";
        }

        auto native = new ::OCC::PUTFileJob(state_->account->account, state_->account->account->url(), remotePath_, std::move(file), headers, context_);
        state_->job = native;
        native->setTimeout(std::chrono::milliseconds(timeoutMs_));
        QObject::connect(native, &::OCC::PUTFileJob::uploadProgress, context_, [state = state_](qint64 sent, qint64 total) {
            if (sent > 0 || total > 0) {
                state->result.bytes_sent = std::max<int64_t>(state->result.bytes_sent, sent);
            }
        });

        auto deadline = new QTimer(context_);
        deadline->setSingleShot(true);
        QObject::connect(deadline, &QTimer::timeout, context_, [state = state_] {
            if (state->job && !state->complete) {
                state->result.timed_out = true;
                state->job->abort();
            }
        });
        QObject::connect(native, &::OCC::AbstractNetworkJob::finishedSignal, context_, [state = state_, deadline] {
            deadline->stop();
            auto job = state->job.data();
            auto reply = job->reply();
            auto &result = state->result;
            result.http_status = job->httpStatusCode();
            result.network_error = static_cast<int>(reply->error());
            result.timed_out = result.timed_out || job->timedOut();
            auto etag = reply->rawHeader("OC-ETag");
            if (etag.isEmpty()) {
                etag = reply->rawHeader("ETag");
            }
            result.etag = ::OCC::Utility::normalizeEtag(QString::fromUtf8(etag)).toStdString();
            result.file_id = reply->rawHeader("OC-FileID").toStdString();
            result.request_id = job->requestId().toStdString();
            const auto body = reply->readAll();
            result.response_body = body.toStdString();
            if (result.timed_out) {
                result.error = "Upload timed out";
            } else if (result.aborted) {
                result.error = "Upload aborted";
            } else if (reply->error() != QNetworkReply::NoError) {
                result.error = ::OCC::errorMessage(::OCC::networkReplyErrorString(*reply), body).toStdString();
            } else if (!result.ok()) {
                result.error = "Unexpected upload HTTP status: " + std::to_string(result.http_status);
            }
            state->finish();
        });
        QObject::connect(native, &QObject::destroyed, context_, [state = state_] {
            if (!state->complete) {
                state->result.error = state->result.aborted ? "Upload aborted" : "Upload job ended without a response";
                state->finish();
            }
        });

        state_->started = true;
        ++state_->account->activeJobs;
        state_->elapsed.start();
        deadline->start(timeoutMs_);
        native->start();
    }

    UploadResult wait()
    {
        check_thread();
        if (!state_->started) {
            throw std::runtime_error("start the upload before waiting for it");
        }
        try {
            while (!state_->complete) {
                WaitSlice slice{state_.get(), {}};
                // Short native event-loop slices release the GVL, so Ruby servers
                // and other Ruby threads run, and interrupts are checked promptly.
                Rice::detail::protect(rb_thread_call_without_gvl, wait_slice, &slice, static_cast<rb_unblock_function_t *>(nullptr), static_cast<void *>(nullptr));
                if (slice.exception) {
                    std::rethrow_exception(slice.exception);
                }
            }
        } catch (...) {
            abort();
            throw;
        }
        return state_->result;
    }

    void abort()
    {
        check_thread();
        if (state_->started && !state_->complete && state_->job) {
            state_->result.aborted = true;
            state_->job->abort();
        }
    }

    bool finished() const { check_thread(); return state_->complete; }
    int64_t bytes_sent() const { check_thread(); return state_->result.bytes_sent; }
    int64_t total_bytes() const { check_thread(); return state_->result.total_bytes; }
    std::optional<UploadResult> result() const
    {
        check_thread();
        if (state_->complete) {
            return state_->result;
        }
        return std::nullopt;
    }

private:
    std::shared_ptr<UploadState> state_;
    QObject *context_ = nullptr;
    QString localPath_;
    QString remotePath_;
    std::optional<std::string> etag_;
    bool createOnly_;
    int timeoutMs_;
};

}

extern "C" void Init_occ()
{
    using namespace Rice;
    using namespace OpenVFSRuby;

    ensure_application();
    Module module = define_module_under(define_module("OpenVFS"), "OCC");
    module.define_constant("DESKTOP_VERSION", ::OCC::Version::displayString().toStdString());
    module.define_constant("DESKTOP_REVISION", ::OCC::Version::gitSha().toStdString());

    define_class_under<Account>(module, "Account")
        .define_constructor(Constructor<Account, const std::string &, const std::string &>(), Arg("url"), Arg("access_token"))
        .define_method("url", &Account::url)
        .define_method("uuid", &Account::uuid)
        .define_method("access_token=", &Account::set_access_token);

    define_class_under<UploadResult>(module, "UploadResult")
        .define_method("ok?", &UploadResult::ok)
        .define_attr("http_status", &UploadResult::http_status, AttrAccess::Read)
        .define_attr("network_error", &UploadResult::network_error, AttrAccess::Read)
        .define_attr("etag", &UploadResult::etag, AttrAccess::Read)
        .define_attr("file_id", &UploadResult::file_id, AttrAccess::Read)
        .define_attr("request_id", &UploadResult::request_id, AttrAccess::Read)
        .define_attr("response_body", &UploadResult::response_body, AttrAccess::Read)
        .define_attr("error", &UploadResult::error, AttrAccess::Read)
        .define_attr("bytes_sent", &UploadResult::bytes_sent, AttrAccess::Read)
        .define_attr("total_bytes", &UploadResult::total_bytes, AttrAccess::Read)
        .define_attr("elapsed_ms", &UploadResult::elapsed_ms, AttrAccess::Read)
        .define_attr("aborted?", &UploadResult::aborted, AttrAccess::Read)
        .define_attr("timed_out?", &UploadResult::timed_out, AttrAccess::Read);

    define_class_under<PUTFileJob>(module, "PUTFileJob")
        .define_constructor(Constructor<PUTFileJob, Account &, const std::string &, const std::string &, const std::optional<std::string> &, bool, int>(),
            Arg("account"), Arg("local_path"), Arg("remote_path"), Arg("etag") = std::optional<std::string>{}, Arg("create_only") = false, Arg("timeout_ms") = 300000)
        .define_method("start", &PUTFileJob::start)
        .define_method("wait", &PUTFileJob::wait)
        .define_method("abort", &PUTFileJob::abort)
        .define_method("finished?", &PUTFileJob::finished)
        .define_method("bytes_sent", &PUTFileJob::bytes_sent)
        .define_method("total_bytes", &PUTFileJob::total_bytes)
        .define_method("result", &PUTFileJob::result);
}
