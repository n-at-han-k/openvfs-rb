// Rice bindings for OpenCloud Desktop.
//
// What is wrapped: OCC::Account and OCC::CredentialManager, OCC::ProgressDispatcher and the
// Folder, SyncFileItem, ProgressInfo and Estimates its signals carry, plus the types all of
// their methods take and return, and OCC::init - the desktop's own entry point, with the UI
// patched out. Nothing here adds behaviour that libsync does not already have.
//
// is_convertible returns a double score, matching Rice 4.12.0's own converters in
// stl.hpp; Rice's published docs still describe an older enum form.

// Qt comes before Ruby, and the order is load-bearing on Windows: ruby.h defines truncate and access as macros onto its own win32 replacements, and Qt's headers stop with an #error rather than be parsed after that.
#include <gui/folder.h>
#include <libsync/account.h>
#include <libsync/creds/credentialmanager.h>
#include <libsync/progressdispatcher.h>

#include <qt6keychain/keychain.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QTimer>
#include <QUuid>
#include <QVariant>

// rice.hpp uses std::exception_ptr, std::sort and std::count_if without including the headers that define them. libstdc++ happens to pull them in anyway; libc++ does not, so on clang exception_ptr is incomplete and the algorithms are missing unless these come first.
#include <algorithm>
#include <exception>

#include <rice/rice.hpp>
#include <rice/stl.hpp>

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
        // Whatever the name is, it is the account's: ConfigFile derives both the config
        // directory and the file inside it from the application name, so losing it would
        // point the engine at a different account's config.
        _name = QCoreApplication::applicationName();

        if (QCoreApplication::instance()) {
            return;
        }

        static int argc = 1;
        static char name[] = "openvfs-rb";
        static char *argv[] = {name, nullptr};
        _application = std::make_unique<QCoreApplication>(argc, argv);
        _application->setApplicationName(_name);
    }

    ~ApplicationScope()
    {
        if (!_application) {
            return;
        }

        _application.reset();
        // QCoreApplication clears the application name when it is destroyed.
        QCoreApplication::setApplicationName(_name);
    }

private:
    QString _name;
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

// ProgressDispatcher's whole interface is four Qt signals, and the Ruby side of each one is
// a block. Rice always turns a block into a proc and passes it as a trailing VALUE parameter
// marked Arg(...).setValue(); Object(proc).call("call", ...) then invokes it with the
// arguments converted by To_Ruby. Both halves are the documented
// square_with_proc/invokeFunction pattern, only with a Qt connection in between.
//
// The proc has to outlive the subscribing call, so each lambda captures a Rice::Pin, whose
// documented use is exactly "callback adapters that capture a Ruby proc". Pin's copy
// semantics share one GC anchor, so every copy Qt takes of the lambda keeps the same proc
// alive. Nothing disconnects: the dispatcher is a singleton and Rice does not free callback
// wrappers either, for the same reason - C++ may call back at any time.

void on_progress_info(OCC::ProgressDispatcher &self, VALUE callback)
{
    Rice::Pin pin(callback);
    QObject::connect(&self, &OCC::ProgressDispatcher::progressInfo,
        [pin](OCC::Folder *folder, const OCC::ProgressInfo &progress) {
            Object(pin.value()).call("call", folder, progress);
        });
}

void on_item_completed(OCC::ProgressDispatcher &self, VALUE callback)
{
    Rice::Pin pin(callback);
    QObject::connect(&self, &OCC::ProgressDispatcher::itemCompleted,
        [pin](OCC::Folder *folder, const OCC::SyncFileItemPtr &item) {
            // By value, not by reference: a reference would wrap the signal's own
            // QSharedPointer, which is gone once the emission returns. A copy is what Ruby
            // frees, and copying a QSharedPointer is what holds the SyncFileItem open for as
            // long as the block keeps it.
            Object(pin.value()).call("call", folder, OCC::SyncFileItemPtr(item));
        });
}

void on_sync_error(OCC::ProgressDispatcher &self, VALUE callback)
{
    Rice::Pin pin(callback);
    QObject::connect(&self, &OCC::ProgressDispatcher::syncError,
        [pin](OCC::Folder *folder, const QString &message, OCC::ErrorCategory category) {
            Object(pin.value()).call("call", folder, message, category);
        });
}

void on_folder_conflicts(OCC::ProgressDispatcher &self, VALUE callback)
{
    Rice::Pin pin(callback);
    QObject::connect(&self, &OCC::ProgressDispatcher::folderConflicts,
        [pin](OCC::Folder *folder, const QStringList &conflictPaths) {
            Object(pin.value()).call("call", folder, conflictPaths);
        });
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

        // ConfigFile::configPath is QStandardPaths::AppConfigLocation, which is
        // $XDG_CONFIG_HOME/<application name>, and ConfigFile::configFile is that plus the
        // lowercased name and ".cfg". So the name is what picks the config out, and one
        // process per account means setting it per account.
        rb_mOpenVFS.define_module_function("application_name", []() {
            return QCoreApplication::applicationName();
        });

        rb_mOpenVFS.define_module_function(
            "application_name=",
            [](const QString &name) {
                QCoreApplication::setApplicationName(name);
                return name;
            },
            Arg("name"));

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

        // -----------------------------------------------------------------------------------------
        // OCC::Folder - the sync folder a progress signal is about. Wrapped for its name and
        // path only; the dispatcher hands out Folder pointers and Ruby needs to tell them apart.
        //
        // No define_constructor: folders are created by the desktop's FolderMan, never by Ruby,
        // and a wrapped class without a constructor is simply one Ruby cannot instantiate.
        // -----------------------------------------------------------------------------------------

        Data_Type<OCC::Folder> rb_cFolder =
            define_class_under<OCC::Folder>(rb_mOpenVFS, "Folder")
            .define_method("display_name", &OCC::Folder::displayName)
            .define_method("path", &OCC::Folder::path)
            .define_method("clean_path", &OCC::Folder::cleanPath)
            .define_method("short_gui_local_path", &OCC::Folder::shortGuiLocalPath)
            .define_method("sync_running?", &OCC::Folder::isSyncRunning);

        // -----------------------------------------------------------------------------------------
        // OCC::SyncFileItem - one file in a sync run.
        //
        // As with Account, what gets wrapped is the QSharedPointer rather than what it points at,
        // so the reference count keeps the item alive while Ruby holds it. The methods therefore
        // take the pointer and dereference it.
        // -----------------------------------------------------------------------------------------

        Data_Type<OCC::SyncFileItemPtr> rb_cSyncFileItem =
            define_class_under<OCC::SyncFileItemPtr>(rb_mOpenVFS, "SyncFileItem")
            .define_method("local_name", [](const OCC::SyncFileItemPtr &self) { return self->localName(); })
            .define_method("destination", [](const OCC::SyncFileItemPtr &self) { return self->destination(); })
            .define_method("rename_target", [](const OCC::SyncFileItemPtr &self) { return self->_renameTarget; })
            .define_method("original_file", [](const OCC::SyncFileItemPtr &self) { return self->_originalFile; })
            .define_method("status", [](const OCC::SyncFileItemPtr &self) { return self->_status; })
            .define_method("direction", [](const OCC::SyncFileItemPtr &self) { return self->_direction; })
            .define_method("error_string", [](const OCC::SyncFileItemPtr &self) { return self->_errorString; })
            .define_method("message_string", [](const OCC::SyncFileItemPtr &self) { return self->_messageString; })
            .define_method("http_error_code", [](const OCC::SyncFileItemPtr &self) { return self->_httpErrorCode; })
            .define_method("affected_items", [](const OCC::SyncFileItemPtr &self) { return self->_affectedItems; })
            .define_method("size", [](const OCC::SyncFileItemPtr &self) { return self->_size; })
            .define_method("modtime", [](const OCC::SyncFileItemPtr &self) { return self->_modtime; })
            .define_method("etag", [](const OCC::SyncFileItemPtr &self) { return self->_etag; })
            .define_method("empty?", [](const OCC::SyncFileItemPtr &self) { return self->isEmpty(); })
            .define_method("directory?", [](const OCC::SyncFileItemPtr &self) { return self->isDirectory(); })
            .define_method("error_status?", [](const OCC::SyncFileItemPtr &self) { return self->hasErrorStatus(); })
            // OCC::Progress's two free functions turn an item into the line the desktop's own
            // UI would show for it.
            .define_method("action_string", [](const OCC::SyncFileItemPtr &self) { return OCC::Progress::asActionString(*self); })
            .define_method("result_string", [](const OCC::SyncFileItemPtr &self) { return OCC::Progress::asResultString(*self); })
            .define_method("warning_kind?", [](const OCC::SyncFileItemPtr &self) { return OCC::Progress::isWarningKind(self->_status); })
            .define_method("ignored_kind?", [](const OCC::SyncFileItemPtr &self) { return OCC::Progress::isIgnoredKind(self->_status); });

        Enum<OCC::SyncFileItem::Status> rb_eSyncFileItemStatus =
            define_enum_under<OCC::SyncFileItem::Status>("Status", rb_cSyncFileItem)
            .define_value("NoStatus", OCC::SyncFileItem::NoStatus)
            .define_value("FatalError", OCC::SyncFileItem::FatalError)
            .define_value("NormalError", OCC::SyncFileItem::NormalError)
            .define_value("SoftError", OCC::SyncFileItem::SoftError)
            .define_value("Success", OCC::SyncFileItem::Success)
            .define_value("Conflict", OCC::SyncFileItem::Conflict)
            .define_value("FileIgnored", OCC::SyncFileItem::FileIgnored)
            .define_value("Restoration", OCC::SyncFileItem::Restoration)
            .define_value("DetailError", OCC::SyncFileItem::DetailError)
            .define_value("BlacklistedError", OCC::SyncFileItem::BlacklistedError)
            .define_value("Excluded", OCC::SyncFileItem::Excluded)
            .define_value("Message", OCC::SyncFileItem::Message)
            .define_value("FilenameReserved", OCC::SyncFileItem::FilenameReserved);

        Enum<OCC::SyncFileItem::Direction> rb_eSyncFileItemDirection =
            define_enum_under<OCC::SyncFileItem::Direction>("Direction", rb_cSyncFileItem)
            .define_value("None", OCC::SyncFileItem::None)
            .define_value("Up", OCC::SyncFileItem::Up)
            .define_value("Down", OCC::SyncFileItem::Down);

        // -----------------------------------------------------------------------------------------
        // OCC::ProgressInfo - the payload of the progressInfo signal.
        // -----------------------------------------------------------------------------------------

        // Estimates is a plain struct of two public members, which is what define_attr is for.
        // Read-only: it is a snapshot the dispatcher computed, so writing to it means nothing.
        Data_Type<OCC::ProgressInfo::Estimates> rb_cEstimates =
            define_class_under<OCC::ProgressInfo::Estimates>(rb_mOpenVFS, "Estimates")
            .define_attr("estimated_bandwidth", &OCC::ProgressInfo::Estimates::estimatedBandwidth, Rice::AttrAccess::Read)
            .define_attr("estimated_eta", &OCC::ProgressInfo::Estimates::estimatedEta, Rice::AttrAccess::Read);

        // One file's share of the run. The engine keeps a Progress per file in flight and
        // discards it when the file finishes, so this is what a per-file progress bar reads;
        // ProgressInfo's own totals are the whole run.
        Data_Type<OCC::ProgressInfo::Progress> rb_cProgress =
            define_class_under<OCC::ProgressInfo::Progress>(rb_mOpenVFS, "Progress")
            .define_method("completed", &OCC::ProgressInfo::Progress::completed)
            .define_method("remaining", &OCC::ProgressInfo::Progress::remaining)
            .define_method("estimates", &OCC::ProgressInfo::Progress::estimates);

        Data_Type<OCC::ProgressInfo::ProgressItem> rb_cProgressItem =
            define_class_under<OCC::ProgressInfo::ProgressItem>(rb_mOpenVFS, "ProgressItem")
            // Both by value, so Ruby owns a copy and may keep it after the signal returns -
            // the engine's own entry goes when the file does.
            .define_method("item", [](const OCC::ProgressInfo::ProgressItem &self) {
                return OCC::SyncFileItemPtr::create(self._item);
            })
            .define_method("progress", [](const OCC::ProgressInfo::ProgressItem &self) {
                return self._progress;
            });

        Data_Type<OCC::ProgressInfo> rb_cProgressInfo =
            define_class_under<OCC::ProgressInfo>(rb_mOpenVFS, "ProgressInfo")
            .define_method("status", &OCC::ProgressInfo::status)
            .define_method("updating_estimates?", &OCC::ProgressInfo::isUpdatingEstimates)
            .define_method("total_files", &OCC::ProgressInfo::totalFiles)
            .define_method("completed_files", &OCC::ProgressInfo::completedFiles)
            .define_method("total_size", &OCC::ProgressInfo::totalSize)
            .define_method("completed_size", &OCC::ProgressInfo::completedSize)
            .define_method("current_file", &OCC::ProgressInfo::currentFile)
            // Estimates comes back by value, so Rice copy-constructs it and Ruby frees the copy.
            .define_method("total_progress", &OCC::ProgressInfo::totalProgress)
            .define_method("optimistic_eta", &OCC::ProgressInfo::optimisticEta)
            .define_method("trust_eta?", &OCC::ProgressInfo::trustEta)
            .define_method("file_progress", [](const OCC::ProgressInfo &self, const OCC::SyncFileItemPtr &item) {
                return self.fileProgress(*item);
            }, Arg("item"))
            // The files in flight, keyed by the path the engine names them by. A Hash rather
            // than the QHash itself: Rice ships no converter for one, and what a caller wants
            // is to look a path up.
            .define_method("current_items", [](const OCC::ProgressInfo &self) {
                Hash items;

                for (auto entry = self._currentItems.constBegin(); entry != self._currentItems.constEnd(); ++entry) {
                    items[entry.key()] = entry.value();
                }

                return items;
            })
            .define_method("current_discovered_remote_folder", [](const OCC::ProgressInfo &self) {
                return self._currentDiscoveredRemoteFolder;
            })
            .define_method("current_discovered_local_folder", [](const OCC::ProgressInfo &self) {
                return self._currentDiscoveredLocalFolder;
            })
            // _lastCompletedItem is a SyncFileItem value, and SyncFileItem is wrapped through
            // its QSharedPointer, so hand back a copy in one rather than a reference into a
            // ProgressInfo that only lives for the duration of the signal.
            .define_method("last_completed_item", [](const OCC::ProgressInfo &self) {
                return OCC::SyncFileItemPtr::create(self._lastCompletedItem);
            });

        Enum<OCC::ProgressInfo::Status> rb_eProgressInfoStatus =
            define_enum_under<OCC::ProgressInfo::Status>("Status", rb_cProgressInfo)
            .define_value("None", OCC::ProgressInfo::None)
            .define_value("Discovery", OCC::ProgressInfo::Discovery)
            .define_value("Reconcile", OCC::ProgressInfo::Reconcile)
            .define_value("Propagation", OCC::ProgressInfo::Propagation)
            .define_value("Done", OCC::ProgressInfo::Done);

        Enum<OCC::ErrorCategory> rb_eErrorCategory =
            define_enum_under<OCC::ErrorCategory>("ErrorCategory", rb_mOpenVFS)
            .define_value("Normal", OCC::ErrorCategory::Normal)
            .define_value("InsufficientRemoteStorage", OCC::ErrorCategory::InsufficientRemoteStorage);

        // -----------------------------------------------------------------------------------------
        // OCC::ProgressDispatcher - the singleton every sync progress signal comes through.
        //
        // instance() is a static member function, so define_singleton_function. It returns a
        // pointer and Rice's default is that C++ keeps ownership, which is what a singleton
        // wants - no Return().takeOwnership() here, or Ruby would free the one instance.
        //
        // Each on_* method subscribes a block; see the helpers above for how.
        // -----------------------------------------------------------------------------------------

        Data_Type<OCC::ProgressDispatcher> rb_cProgressDispatcher =
            define_class_under<OCC::ProgressDispatcher>(rb_mOpenVFS, "ProgressDispatcher")
            .define_singleton_function("instance", &OCC::ProgressDispatcher::instance)
            .define_method("on_progress_info", &on_progress_info, Arg("callback").setValue())
            .define_method("on_item_completed", &on_item_completed, Arg("callback").setValue())
            .define_method("on_sync_error", &on_sync_error, Arg("callback").setValue())
            .define_method("on_folder_conflicts", &on_folder_conflicts, Arg("callback").setValue());
    });
}
