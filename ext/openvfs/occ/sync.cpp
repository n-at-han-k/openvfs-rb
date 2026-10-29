// Rice bindings for OpenCloud Desktop.
//
// Only two classes are wrapped: OCC::Account and OCC::CredentialManager, plus the types
// their methods take and return, and OCC::init - the desktop's own entry point, with the
// UI patched out. Nothing here adds behaviour that libsync does not already have.
//
// is_convertible returns a double score, matching Rice 4.12.0's own converters in
// stl.hpp; Rice's published docs still describe an older enum form.

#include <rice/rice.hpp>
#include <rice/stl.hpp>

#include <libsync/account.h>
#include <libsync/creds/credentialmanager.h>

#include <qt6keychain/keychain.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QTimer>
#include <QUuid>
#include <QVariant>

#include <memory>

namespace OCC {
// Defined in src/gui/main.cpp, which the patches reduce to the non-UI bootstrap.
int init(const QStringList &arguments);
}

// ---------------------------------------------------------------------------------------------
// Type converters: Rice ships these for fundamental types and the STL, so Qt's own need
// the three documented steps - specialize Type, To_Ruby and From_Ruby.
// ---------------------------------------------------------------------------------------------

namespace Rice::detail
{
    template<>
    struct Type<QString>
    {
        static bool verify() { return true; }
    };

    template<>
    class To_Ruby<QString>
    {
    public:
        To_Ruby() = default;
        explicit To_Ruby(Arg* arg) : arg_(arg) { }

        VALUE convert(const QString& string)
        {
            const QByteArray utf8 = string.toUtf8();
            return String(std::string(utf8.constData(), utf8.size())).value();
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<QString>
    {
    public:
        From_Ruby() = default;
        explicit From_Ruby(Arg* arg) : arg_(arg) { }

        double is_convertible(VALUE value)
        {
            return rb_type(value) == RUBY_T_STRING ? Convertible::Exact : Convertible::None;
        }

        QString convert(VALUE value)
        {
            const std::string string = From_Ruby<std::string>().convert(value);
            return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<QStringList>
    {
        static bool verify() { return Type<QString>::verify(); }
    };

    template<>
    class To_Ruby<QStringList>
    {
    public:
        To_Ruby() = default;
        explicit To_Ruby(Arg* arg) : arg_(arg) { }

        VALUE convert(const QStringList& strings)
        {
            Array result;
            for (const QString& string : strings) {
                result.push(string, true);
            }
            return result.value();
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<QStringList>
    {
    public:
        From_Ruby() = default;
        explicit From_Ruby(Arg* arg) : arg_(arg) { }

        double is_convertible(VALUE value)
        {
            return rb_type(value) == RUBY_T_ARRAY ? Convertible::Exact : Convertible::None;
        }

        QStringList convert(VALUE value)
        {
            Array array(value);
            QStringList result;
            for (long i = 0; i < array.size(); i++) {
                result.append(From_Ruby<QString>().convert(array[i].value()));
            }
            return result;
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<QUrl>
    {
        static bool verify() { return true; }
    };

    template<>
    class To_Ruby<QUrl>
    {
    public:
        To_Ruby() = default;
        explicit To_Ruby(Arg* arg) : arg_(arg) { }

        VALUE convert(const QUrl& url) { return To_Ruby<QString>().convert(url.toString()); }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<QUrl>
    {
    public:
        From_Ruby() = default;
        explicit From_Ruby(Arg* arg) : arg_(arg) { }

        double is_convertible(VALUE value)
        {
            return rb_type(value) == RUBY_T_STRING ? Convertible::Exact : Convertible::None;
        }

        QUrl convert(VALUE value) { return QUrl(From_Ruby<QString>().convert(value)); }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<QUuid>
    {
        static bool verify() { return true; }
    };

    template<>
    class To_Ruby<QUuid>
    {
    public:
        To_Ruby() = default;
        explicit To_Ruby(Arg* arg) : arg_(arg) { }

        VALUE convert(const QUuid& uuid)
        {
            return To_Ruby<QString>().convert(uuid.toString(QUuid::WithoutBraces));
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<QUuid>
    {
    public:
        From_Ruby() = default;
        explicit From_Ruby(Arg* arg) : arg_(arg) { }

        double is_convertible(VALUE value)
        {
            return rb_type(value) == RUBY_T_STRING ? Convertible::Exact : Convertible::None;
        }

        QUuid convert(VALUE value) { return QUuid::fromString(From_Ruby<QString>().convert(value)); }

    private:
        Arg* arg_ = nullptr;
    };

    // CredentialManager::set stores a QVariant and CredentialJob::data returns one.
    template<>
    struct Type<QVariant>
    {
        static bool verify() { return true; }
    };

    template<>
    class To_Ruby<QVariant>
    {
    public:
        To_Ruby() = default;
        explicit To_Ruby(Arg* arg) : arg_(arg) { }

        VALUE convert(const QVariant& variant)
        {
            if (!variant.isValid()) {
                return Qnil;
            }
            switch (variant.typeId()) {
                case QMetaType::Bool:
                    return variant.toBool() ? Qtrue : Qfalse;
                case QMetaType::Int:
                case QMetaType::UInt:
                case QMetaType::LongLong:
                case QMetaType::ULongLong:
                    return To_Ruby<long long>().convert(variant.toLongLong());
                case QMetaType::Double:
                    return To_Ruby<double>().convert(variant.toDouble());
                default:
                    return To_Ruby<QString>().convert(variant.toString());
            }
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<QVariant>
    {
    public:
        From_Ruby() = default;
        explicit From_Ruby(Arg* arg) : arg_(arg) { }

        double is_convertible(VALUE value)
        {
            switch (rb_type(value)) {
                case RUBY_T_STRING:
                case RUBY_T_FIXNUM:
                case RUBY_T_BIGNUM:
                case RUBY_T_FLOAT:
                case RUBY_T_TRUE:
                case RUBY_T_FALSE:
                case RUBY_T_NIL:
                    return Convertible::Exact;
                default:
                    return Convertible::None;
            }
        }

        QVariant convert(VALUE value)
        {
            switch (rb_type(value)) {
                case RUBY_T_NIL:
                    return QVariant();
                case RUBY_T_TRUE:
                    return QVariant(true);
                case RUBY_T_FALSE:
                    return QVariant(false);
                case RUBY_T_FIXNUM:
                case RUBY_T_BIGNUM:
                    return QVariant(From_Ruby<long long>().convert(value));
                case RUBY_T_FLOAT:
                    return QVariant(From_Ruby<double>().convert(value));
                default:
                    return QVariant(From_Ruby<QString>().convert(value));
            }
        }

    private:
        Arg* arg_ = nullptr;
    };

    // Rice matches converters on the exact parameter type, so `const T&` parameters need
    // their own; each stores the converted value and returns a reference to it.
    template<typename T>
    class From_Ruby_Qt_Ref
    {
    public:
        From_Ruby_Qt_Ref() = default;
        explicit From_Ruby_Qt_Ref(Arg* arg) : arg_(arg) { }

        double is_convertible(VALUE value) { return From_Ruby<T>(this->arg_).is_convertible(value); }

        T& convert(VALUE value)
        {
            this->converted_ = From_Ruby<T>(this->arg_).convert(value);
            return this->converted_;
        }

    private:
        Arg* arg_ = nullptr;
        T converted_;
    };

    template<>
    class From_Ruby<QString&> : public From_Ruby_Qt_Ref<QString>
    {
    public:
        using From_Ruby_Qt_Ref<QString>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<QUrl&> : public From_Ruby_Qt_Ref<QUrl>
    {
    public:
        using From_Ruby_Qt_Ref<QUrl>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<QUuid&> : public From_Ruby_Qt_Ref<QUuid>
    {
    public:
        using From_Ruby_Qt_Ref<QUuid>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<QVariant&> : public From_Ruby_Qt_Ref<QVariant>
    {
    public:
        using From_Ruby_Qt_Ref<QVariant>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<QStringList&> : public From_Ruby_Qt_Ref<QStringList>
    {
    public:
        using From_Ruby_Qt_Ref<QStringList>::From_Ruby_Qt_Ref;
    };
}

using namespace Rice;

namespace {
constexpr int keychainTimeoutMsC = 30000;

// QtKeychain queues its work onto the Qt event loop. Before OpenVFS.start there is no
// loop, so a credential operation creates a temporary application, runs the job to
// completion, and tears the application down again.
class ApplicationScope
{
public:
    ApplicationScope()
    {
        if (QCoreApplication::instance()) {
            return;
        }

        static int argc = 1;
        static char name[] = "openvfs-rb";
        static char *argv[] = {name, nullptr};
        _application = std::make_unique<QCoreApplication>(argc, argv);
        _application->setApplicationName(QStringLiteral("openvfs-rb"));
    }

    ~ApplicationScope()
    {
        if (!_application) {
            return;
        }

        _application.reset();
        // QCoreApplication clears the application name when it is destroyed.
        QCoreApplication::setApplicationName(QStringLiteral("openvfs-rb"));
    }

private:
    std::unique_ptr<QCoreApplication> _application;
};

bool wait_for(QKeychain::Job *job)
{
    QEventLoop loop;
    QObject::connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(keychainTimeoutMsC, &loop, &QEventLoop::quit);
    loop.exec();

    return job->error() == QKeychain::NoError;
}

bool credential_set(OCC::CredentialManager &manager, const QString &key, const QVariant &data)
{
    if (!data.isValid()) {
        return false;
    }

    ApplicationScope application;
    return wait_for(manager.set(key, data));
}

bool credential_remove(OCC::CredentialManager &manager, const QString &key)
{
    ApplicationScope application;
    if (!manager.contains(key)) {
        return false;
    }

    return wait_for(manager.remove(key));
}

bool credential_contains(const OCC::CredentialManager &manager, const QString &key)
{
    ApplicationScope application;
    return manager.contains(key);
}

QVariant credential_get(OCC::CredentialManager &manager, const QString &key)
{
    ApplicationScope application;
    auto *job = manager.get(key);

    QEventLoop loop;
    QObject::connect(job, &OCC::CredentialJob::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(keychainTimeoutMsC, &loop, &QEventLoop::quit);
    loop.exec();

    if (job->error() != QKeychain::NoError) {
        return {};
    }

    return job->data();
}
}

extern "C" void Init_occ()
{
    Rice::detail::cpp_protect([] {
        QCoreApplication::setApplicationName(QStringLiteral("openvfs-rb"));

        Module rb_mOpenVFS = define_module("OpenVFS");

        // -----------------------------------------------------------------------------------------
        // The desktop's entry point: src/gui/main.cpp, reduced by the patches to its non-UI
        // bootstrap. It takes the arguments the command line already accepts.
        // -----------------------------------------------------------------------------------------

        rb_mOpenVFS.define_module_function(
            "start",
            [](const QStringList &arguments) {
                return OCC::init(QStringList{QStringLiteral("openvfs-rb")} + arguments);
            },
            Arg("arguments") = QStringList{});

        // -----------------------------------------------------------------------------------------
        // OCC::CredentialManager - the account's keychain. Each operation runs the keychain job
        // to completion, so Ruby never sees a Qt job object.
        // -----------------------------------------------------------------------------------------

        Data_Type<OCC::CredentialManager> rb_cCredentialManager =
            define_class_under<OCC::CredentialManager>(rb_mOpenVFS, "CredentialManager")
            .define_method("set", &credential_set, Arg("key"), Arg("data"))
            .define_method("get", &credential_get, Arg("key"))
            .define_method("remove", &credential_remove, Arg("key"))
            .define_method("contains?", &credential_contains, Arg("key"));

        // -----------------------------------------------------------------------------------------
        // OCC::Account - one configured server connection.
        // -----------------------------------------------------------------------------------------

        // Account::create hands back a QSharedPointer, and every libsync class takes one.
        // Wrapping the pointer itself - rather than what it points at - is what keeps the
        // reference count alive while Ruby holds it.
        Data_Type<OCC::AccountPtr> rb_cAccount =
            define_class_under<OCC::AccountPtr>(rb_mOpenVFS, "Account")
            .define_singleton_function("create", &OCC::Account::create, Arg("uuid"))
            .define_method("uuid", [](const OCC::AccountPtr &self) { return self->uuid(); })
            .define_method("url", [](const OCC::AccountPtr &self) { return self->url(); })
            .define_method("url=", [](OCC::AccountPtr &self, const QUrl &url) { self->setUrl(url); }, Arg("url"))
            .define_method("credential_manager", [](const OCC::AccountPtr &self) { return self->credentialManager(); },
                Return().keepAlive());
    });
}
