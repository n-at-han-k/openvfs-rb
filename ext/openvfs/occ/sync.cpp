// Rice bindings for OpenCloud Desktop's file-watching, discovery and upload classes.
//
// is_convertible returns a double score, matching Rice 4.12.0's own converters in stl.hpp;
// Rice's published docs still describe the older enum form with a Cast member.
//
// Every binding here uses documented Rice API only: define_class / define_class_under with an
// optional base type, define_constructor, define_method, define_singleton_function, define_attr,
// define_enum_under / define_value, Arg, Return and the Type / To_Ruby / From_Ruby converter
// specializations.
//
// Rice binds public members. Members that OpenCloud declares private or protected are listed in
// AGENTS.md rather than bound, because Rice offers no documented way to reach them.

#include <rice/rice.hpp>
#include <rice/stl.hpp>

#include <gui/folder.h>
#include <gui/folderwatcher.h>
#include <gui/folderwatcher_linux.h>
#include <libsync/discovery.h>
#include <libsync/localdiscoverytracker.h>
#include <libsync/owncloudpropagator.h>
#include <libsync/propagateupload.h>
#include <libsync/propagateuploadtus.h>
#include <libsync/common/version.h>
#include <libsync/creds/abstractcredentials.h>
#include <cmd/tokencredentials.h>
#include <gui/accountmanager.h>
#include <gui/fetchserversettings.h>
#include <gui/accountstate.h>
#include <gui/folderdefinition.h>
#include <gui/folderman.h>
#include <gui/scheduling/syncscheduler.h>
#include <libsync/common/utility.h>
#include <libsync/logger.h>
#include <libsync/syncfileitem.h>
#include <libsync/bandwidthmanager.h>
#include <libsync/syncresult.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>
#include <QSet>
#include <QString>
#include <QUrl>

// ---------------------------------------------------------------------------------------------
// Type converters
//
// Rice ships converters for fundamental types and the STL. Qt's string and set types need their
// own, following the three documented steps: specialize Type, To_Ruby and From_Ruby.
// ---------------------------------------------------------------------------------------------

namespace Rice::detail
{
    template<>
    struct Type<QString>
    {
        static bool verify()
        {
            return true;
        }
    };

    template<>
    class To_Ruby<QString>
    {
    public:
        To_Ruby() = default;

        explicit To_Ruby(Arg* arg) : arg_(arg)
        {
        }

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

        explicit From_Ruby(Arg* arg) : arg_(arg)
        {
        }

        double is_convertible(VALUE value)
        {
            switch (rb_type(value))
            {
                case RUBY_T_STRING:
                    return Convertible::Exact;
                    break;
                default:
                    return Convertible::None;
            }
        }

        QString convert(VALUE value)
        {
            if (value == Qnil && this->arg_ && this->arg_->hasDefaultValue())
            {
                return this->arg_->defaultValue<QString>();
            }

            const std::string string = From_Ruby<std::string>().convert(value);
            return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<QSet<QString>>
    {
        static bool verify()
        {
            return Type<QString>::verify();
        }
    };

    template<>
    class To_Ruby<QSet<QString>>
    {
    public:
        To_Ruby() = default;

        explicit To_Ruby(Arg* arg) : arg_(arg)
        {
        }

        VALUE convert(const QSet<QString>& paths)
        {
            Array result;

            for (const QString& path : paths)
            {
                result.push(path, true);
            }
            return result.value();
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<QSet<QString>>
    {
    public:
        From_Ruby() = default;

        explicit From_Ruby(Arg* arg) : arg_(arg)
        {
        }

        double is_convertible(VALUE value)
        {
            switch (rb_type(value))
            {
                case RUBY_T_ARRAY:
                    return Convertible::Exact;
                    break;
                default:
                    return Convertible::None;
            }
        }

        QSet<QString> convert(VALUE value)
        {
            if (value == Qnil && this->arg_ && this->arg_->hasDefaultValue())
            {
                return this->arg_->defaultValue<QSet<QString>>();
            }

            Array array(value);
            QSet<QString> result;

            for (long i = 0; i < array.size(); i++)
            {
                result.insert(From_Ruby<QString>().convert(array[i].value()));
            }
            return result;
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<QUuid>
    {
        static bool verify()
        {
            return true;
        }
    };

    template<>
    class To_Ruby<QUuid>
    {
    public:
        To_Ruby() = default;

        explicit To_Ruby(Arg* arg) : arg_(arg)
        {
        }

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

        explicit From_Ruby(Arg* arg) : arg_(arg)
        {
        }

        double is_convertible(VALUE value)
        {
            switch (rb_type(value))
            {
                case RUBY_T_STRING:
                    return Convertible::Exact;
                    break;
                default:
                    return Convertible::None;
            }
        }

        QUuid convert(VALUE value)
        {
            return QUuid::fromString(From_Ruby<QString>().convert(value));
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<OCC::AccountStatePtr>
    {
        static bool verify()
        {
            return Data_Type<OCC::AccountState>::is_defined();
        }
    };

    template<>
    class To_Ruby<OCC::AccountStatePtr>
    {
    public:
        To_Ruby() = default;

        explicit To_Ruby(Arg* arg) : arg_(arg)
        {
        }

        VALUE convert(const OCC::AccountStatePtr& state)
        {
            return detail::wrap(Data_Type<OCC::AccountState>::klass(),
                Data_Type<OCC::AccountState>::ruby_data_type(), state.data(), false);
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class From_Ruby<OCC::AccountStatePtr>
    {
    public:
        From_Ruby() = default;

        explicit From_Ruby(Arg* arg) : arg_(arg)
        {
        }

        double is_convertible(VALUE value)
        {
            switch (rb_type(value))
            {
                case RUBY_T_DATA:
                    return Data_Type<OCC::AccountState>::is_descendant(value) ? Convertible::Exact : Convertible::None;
                    break;
                default:
                    return Convertible::None;
            }
        }

        OCC::AccountStatePtr convert(VALUE value)
        {
            return OCC::AccountStatePtr(From_Ruby<OCC::AccountState*>().convert(value));
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    struct Type<QUrl>
    {
        static bool verify()
        {
            return true;
        }
    };

    template<>
    class From_Ruby<QUrl>
    {
    public:
        From_Ruby() = default;

        explicit From_Ruby(Arg* arg) : arg_(arg)
        {
        }

        double is_convertible(VALUE value)
        {
            switch (rb_type(value))
            {
                case RUBY_T_STRING:
                    return Convertible::Exact;
                    break;
                default:
                    return Convertible::None;
            }
        }

        QUrl convert(VALUE value)
        {
            return QUrl(From_Ruby<QString>().convert(value));
        }

    private:
        Arg* arg_ = nullptr;
    };

    template<>
    class To_Ruby<QUrl>
    {
    public:
        To_Ruby() = default;

        explicit To_Ruby(Arg* arg) : arg_(arg)
        {
        }

        VALUE convert(const QUrl& url)
        {
            return To_Ruby<QString>().convert(url.toString());
        }

    private:
        Arg* arg_ = nullptr;
    };

    // Rice matches converters on the exact parameter type, so `const T&` parameters
    // need their own specialization; each stores the converted value and returns it.
    template<typename T>
    class From_Ruby_Qt_Ref
    {
    public:
        From_Ruby_Qt_Ref() = default;

        explicit From_Ruby_Qt_Ref(Arg* arg) : arg_(arg)
        {
        }

        double is_convertible(VALUE value)
        {
            return From_Ruby<T>(this->arg_).is_convertible(value);
        }

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
    class From_Ruby<QUuid&> : public From_Ruby_Qt_Ref<QUuid>
    {
    public:
        using From_Ruby_Qt_Ref<QUuid>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<QUrl&> : public From_Ruby_Qt_Ref<QUrl>
    {
    public:
        using From_Ruby_Qt_Ref<QUrl>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<QSet<QString>&> : public From_Ruby_Qt_Ref<QSet<QString>>
    {
    public:
        using From_Ruby_Qt_Ref<QSet<QString>>::From_Ruby_Qt_Ref;
    };

    template<>
    class From_Ruby<OCC::AccountStatePtr&> : public From_Ruby_Qt_Ref<OCC::AccountStatePtr>
    {
    public:
        using From_Ruby_Qt_Ref<OCC::AccountStatePtr>::From_Ruby_Qt_Ref;
    };

    template<>
    class To_Ruby<QString&>
    {
    public:
        To_Ruby() = default;

        explicit To_Ruby(Arg* arg) : arg_(arg)
        {
        }

        VALUE convert(const QString& value)
        {
            return To_Ruby<QString>(this->arg_).convert(value);
        }

    private:
        Arg* arg_ = nullptr;
    };
}

using namespace Rice;

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
}

extern "C" void Init_occ()
{
    ensure_application();

    Module rb_mOCC = define_module_under(define_module("OpenVFS"), "OCC");
    rb_mOCC.define_constant("DESKTOP_VERSION", OCC::Version::displayString().toStdString());
    rb_mOCC.define_constant("DESKTOP_REVISION", OCC::Version::gitSha().toStdString());

    // -----------------------------------------------------------------------------------------
    // Enums appearing in the wrapped signatures.
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::SyncFileItem> rb_cSyncFileItem =
        define_class_under<OCC::SyncFileItem>(rb_mOCC, "SyncFileItem")
        .define_constructor(Constructor<OCC::SyncFileItem>())
        .define_method("local_name", &OCC::SyncFileItem::localName)
        .define_method("destination", &OCC::SyncFileItem::destination)
        .define_attr("size", &OCC::SyncFileItem::_size)
        .define_attr("modtime", &OCC::SyncFileItem::_modtime)
        .define_attr("etag", &OCC::SyncFileItem::_etag)
        .define_attr("file_id", &OCC::SyncFileItem::_fileId);

    Enum<OCC::SyncFileItem::Status> rb_cStatus =
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
        .define_value("BlacklistedError", OCC::SyncFileItem::BlacklistedError);

    // -----------------------------------------------------------------------------------------
    // The account every sync class is constructed against.
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::AbstractCredentials> rb_cAbstractCredentials =
        define_class_under<OCC::AbstractCredentials>(rb_mOCC, "AbstractCredentials")
        .define_method("ready?", &OCC::AbstractCredentials::ready)
        .define_method("invalidate_token", &OCC::AbstractCredentials::invalidateToken)
        .define_method("forget_sensitive_data", &OCC::AbstractCredentials::forgetSensitiveData);

    // The desktop's headless client (src/cmd) authenticates with these: basic auth,
    // no keychain and no OAuth flow.
    Data_Type<OCC::TokenCredentials> rb_cTokenCredentials =
        define_class_under<OCC::TokenCredentials, OCC::AbstractCredentials>(rb_mOCC, "TokenCredentials")
        .define_constructor(Constructor<OCC::TokenCredentials, QByteArray&&, QByteArray&&>(),
            Arg("username"), Arg("token"));

    Data_Type<OCC::Capabilities> rb_cCapabilities =
        define_class_under<OCC::Capabilities>(rb_mOCC, "Capabilities")
        .define_method("tus_support_valid?", [](const OCC::Capabilities& self)
        {
            return self.tusSupport().isValid();
        });

    Data_Type<OCC::Account> rb_cAccount =
        define_class_under<OCC::Account>(rb_mOCC, "Account")
        .define_method("uuid", &OCC::Account::uuid)
        .define_method("url", &OCC::Account::url)
        .define_method("url=", &OCC::Account::setUrl, Arg("url"))
        .define_method("dav_display_name", &OCC::Account::davDisplayName)
        .define_method("credentials", &OCC::Account::credentials)
        .define_method("credentials=", &OCC::Account::setCredentials, Arg("credentials").keepAlive())
        .define_method("capabilities", &OCC::Account::capabilities);

    // Account::create hands back a QSharedPointer, and every sync class takes one.
    // Wrapping the pointer itself - rather than what it points at - is what keeps the
    // reference count alive while Ruby holds it.
    Data_Type<OCC::AccountPtr> rb_cAccountPtr =
        define_class_under<OCC::AccountPtr>(rb_mOCC, "AccountPtr")
        .define_singleton_function("create", &OCC::Account::create, Arg("uuid"))
        .define_method("get", [](OCC::AccountPtr& self)
        {
            return self.data();
        })
        .define_method("null?", [](const OCC::AccountPtr& self)
        {
            return self.isNull();
        })
        .define_method("uuid", [](const OCC::AccountPtr& self)
        {
            return self->uuid();
        })
        .define_method("url", [](const OCC::AccountPtr& self)
        {
            return self->url();
        })
        .define_method("url=", [](OCC::AccountPtr& self, const QUrl& url)
        {
            self->setUrl(url);
        }, Arg("url"))
        .define_method("dav_display_name", [](const OCC::AccountPtr& self)
        {
            return self->davDisplayName();
        })
        .define_method("credentials=", [](OCC::AccountPtr& self, OCC::AbstractCredentials* credentials)
        {
            self->setCredentials(credentials);
        }, Arg("credentials").keepAlive())
        .define_method("capabilities", [](const OCC::AccountPtr& self)
        {
            return self->capabilities();
        })
        .define_method("has_capabilities?", [](const OCC::AccountPtr& self)
        {
            return self->hasCapabilities();
        })
        ;

    // libsync installs a Qt message handler that drops everything until a log file is
    // set, so without this nothing a sync run reports is visible.
    Data_Type<OCC::Logger> rb_cLogger =
        define_class_under<OCC::Logger>(rb_mOCC, "Logger")
        .define_singleton_function("instance", &OCC::Logger::instance)
        .define_method("install", &OCC::Logger::install)
        .define_method("log_file=", &OCC::Logger::setLogFile, Arg("name"))
        .define_method("log_debug=", &OCC::Logger::setLogDebug, Arg("debug"))
        .define_method("log_flush=", &OCC::Logger::setLogFlush, Arg("flush"))
        .define_method("logging_to_file?", &OCC::Logger::isLoggingToFile)
        .define_method("log_rules=", &OCC::Logger::setLogRules, Arg("rules"));

    Data_Type<OCC::SyncResult> rb_cSyncResult =
        define_class_under<OCC::SyncResult>(rb_mOCC, "SyncResult")
        .define_constructor(Constructor<OCC::SyncResult>())
        .define_method("status", &OCC::SyncResult::status)
        .define_method("error_string", &OCC::SyncResult::errorString)
        .define_method("error_strings", &OCC::SyncResult::errorStrings)
        .define_method("found_files_not_synced?", &OCC::SyncResult::foundFilesNotSynced)
        .define_method("folder_structure_was_changed?", &OCC::SyncResult::folderStructureWasChanged)
        .define_method("unresolved_conflicts?", &OCC::SyncResult::hasUnresolvedConflicts);

    Enum<OCC::SyncResult::Status> rb_cSyncResultStatus =
        define_enum_under<OCC::SyncResult::Status>("Status", rb_cSyncResult)
        .define_value("Undefined", OCC::SyncResult::Undefined)
        .define_value("Success", OCC::SyncResult::Success)
        .define_value("Queued", OCC::SyncResult::Queued)
        .define_value("SyncRunning", OCC::SyncResult::SyncRunning)
        .define_value("Paused", OCC::SyncResult::Paused)
        .define_value("Offline", OCC::SyncResult::Offline)
        .define_value("Problem", OCC::SyncResult::Problem)
        .define_value("Error", OCC::SyncResult::Error)
        .define_value("SetupError", OCC::SyncResult::SetupError);

    Data_Type<OCC::FetchServerSettingsJob> rb_cFetchServerSettingsJob =
        define_class_under<OCC::FetchServerSettingsJob>(rb_mOCC, "FetchServerSettingsJob")
        .define_constructor(Constructor<OCC::FetchServerSettingsJob, const OCC::AccountPtr&, QObject*>(),
            Arg("account"), Arg("parent") = static_cast<QObject*>(nullptr))
        .define_method("start", &OCC::FetchServerSettingsJob::start);

    Data_Type<OCC::AccountManager> rb_cAccountManager =
        define_class_under<OCC::AccountManager>(rb_mOCC, "AccountManager")
        .define_singleton_function("instance", &OCC::AccountManager::instance)
        .define_singleton_function("create_account", &OCC::AccountManager::createAccount, Arg("uuid"))
        .define_method("add_account", &OCC::AccountManager::addAccount, Arg("account"))
        .define_method("account", &OCC::AccountManager::account, Arg("uuid"))
        .define_method("accounts", &OCC::AccountManager::accounts)
        .define_method("restore", &OCC::AccountManager::restore)
        .define_method("save", &OCC::AccountManager::save)
        .define_method("shutdown", &OCC::AccountManager::shutdown);

    Data_Type<OCC::AccountState> rb_cAccountState =
        define_class_under<OCC::AccountState>(rb_mOCC, "AccountState")
        .define_method("account", &OCC::AccountState::account)
        .define_method("connected?", &OCC::AccountState::isConnected)
        .define_method("check_connectivity", &OCC::AccountState::checkConnectivity,
            Arg("verify_server_state") = false);

    Data_Type<OCC::FolderDefinition> rb_cFolderDefinition =
        define_class_under<OCC::FolderDefinition>(rb_mOCC, "FolderDefinition")
        .define_constructor(Constructor<OCC::FolderDefinition, const QUuid&, const QUrl&, const QString&, const QString&>(),
            Arg("account_uuid"), Arg("dav_url"), Arg("space_id"), Arg("display_name"))
        .define_attr("journal_path", &OCC::FolderDefinition::journalPath)
        .define_attr("paused", &OCC::FolderDefinition::paused)
        .define_attr("ignore_hidden_files", &OCC::FolderDefinition::ignoreHiddenFiles)
        .define_method("local_path", &OCC::FolderDefinition::localPath)
        .define_method("local_path=", &OCC::FolderDefinition::setLocalPath, Arg("path"))
        .define_method("web_dav_url", &OCC::FolderDefinition::webDavUrl)
        .define_method("display_name", &OCC::FolderDefinition::displayName)
        .define_method("space_id", &OCC::FolderDefinition::spaceId);

    Data_Type<OCC::SyncScheduler> rb_cSyncScheduler =
        define_class_under<OCC::SyncScheduler>(rb_mOCC, "SyncScheduler")
        .define_method("start", &OCC::SyncScheduler::start)
        .define_method("stop", &OCC::SyncScheduler::stop)
        .define_method("running?", &OCC::SyncScheduler::hasCurrentRunningSyncRunning);

    Data_Type<OCC::FolderMan> rb_cFolderMan =
        define_class_under<OCC::FolderMan>(rb_mOCC, "FolderMan")
        .define_singleton_function("instance", &OCC::FolderMan::instance)
        .define_method("add_folder", &OCC::FolderMan::addFolder,
            Arg("account_state"), Arg("folder_definition"))
        .define_method("remove_folder", &OCC::FolderMan::removeFolder, Arg("folder"))
        .define_method("scheduler", &OCC::FolderMan::scheduler)
        .define_method("folder_for_path", [](OCC::FolderMan& self, const QString& path)
        {
            return self.folderForPath(path);
        }, Arg("path"));

    // Qt's event loop has to run for any of this to make progress; a Ruby process
    // has no exec() to sit in, so the caller pumps it.
    rb_mOCC.define_module_function("process_events", [](int maxMilliseconds)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, maxMilliseconds);
    }, Arg("max_milliseconds") = 50);

    // processEvents does not iterate the loop the way exec() does; network replies in
    // particular need a real loop to complete.
    rb_mOCC.define_module_function("run_event_loop", [](int milliseconds)
    {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, [&loop] { loop.quit(); });
        loop.exec();
    }, Arg("milliseconds") = 100);

    // -----------------------------------------------------------------------------------------
    // Layer 1 - detecting the filesystem change
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::FolderWatcher> rb_cFolderWatcher =
        define_class_under<OCC::FolderWatcher>(rb_mOCC, "FolderWatcher")
        .define_method("init", &OCC::FolderWatcher::init, Arg("root"))
        .define_method("path_is_ignored?", &OCC::FolderWatcher::pathIsIgnored, Arg("path"))
        .define_method("reliable?", &OCC::FolderWatcher::isReliable)
        .define_method("pop_change_set", &OCC::FolderWatcher::popChangeSet)
        .define_method("restart", &OCC::FolderWatcher::restart)
        .define_method("test_linux_watch_count", &OCC::FolderWatcher::testLinuxWatchCount);

    Data_Type<OCC::FolderWatcherPrivate> rb_cFolderWatcherPrivate =
        define_class_under<OCC::FolderWatcherPrivate>(rb_mOCC, "FolderWatcherPrivate")
        .define_constructor(Constructor<OCC::FolderWatcherPrivate>())
        .define_method("test_watch_count", &OCC::FolderWatcherPrivate::testWatchCount)
        .define_method("ready?", &OCC::FolderWatcherPrivate::isReady);

    // -----------------------------------------------------------------------------------------
    // Layer 2 - turning changes into a scheduled sync
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::Folder> rb_cFolder =
        define_class_under<OCC::Folder>(rb_mOCC, "Folder")
        .define_singleton_function("prepare_folder", &OCC::Folder::prepareFolder,
            Arg("path"), Arg("display_name"), Arg("description"), Arg("override"))
        .define_method("start_sync", &OCC::Folder::startSync)
        .define_method("schedule_path_for_local_discovery", &OCC::Folder::schedulePathForLocalDiscovery,
            Arg("relative_path"))
        .define_method("next_sync_full_local_discovery", &OCC::Folder::slotNextSyncFullLocalDiscovery)
        .define_method("display_name", &OCC::Folder::displayName)
        .define_method("path", &OCC::Folder::path)
        .define_method("clean_path", &OCC::Folder::cleanPath)
        .define_method("short_gui_local_path", &OCC::Folder::shortGuiLocalPath)
        .define_method("web_dav_url", &OCC::Folder::webDavUrl)
        .define_method("web_url", &OCC::Folder::webUrl)
        .define_method("sync_paused?", &OCC::Folder::isSyncPaused)
        .define_method("sync_paused=", &OCC::Folder::setSyncPaused, Arg("paused"))
        .define_method("can_sync?", &OCC::Folder::canSync)
        .define_method("ready?", &OCC::Folder::isReady)
        .define_method("setup_error?", &OCC::Folder::hasSetupError)
        .define_method("sync_running?", &OCC::Folder::isSyncRunning)
        .define_method("deployed?", &OCC::Folder::isDeployed)
        .define_method("ignore_hidden_files?", &OCC::Folder::ignoreHiddenFiles)
        .define_method("ignore_hidden_files=", &OCC::Folder::setIgnoreHiddenFiles, Arg("ignore"))
        .define_method("file_excluded_absolute?", &OCC::Folder::isFileExcludedAbsolute, Arg("full_path"))
        .define_method("file_excluded_relative?", &OCC::Folder::isFileExcludedRelative, Arg("relative_path"))
        .define_method("virtual_files_enabled?", &OCC::Folder::virtualFilesEnabled)
        .define_method("virtual_files_enabled=", &OCC::Folder::setVirtualFilesEnabled, Arg("enabled"))
        .define_method("supports_selective_sync?", &OCC::Folder::supportsSelectiveSync)
        .define_method("priority", &OCC::Folder::priority)
        .define_method("priority=", &OCC::Folder::setPriority, Arg("priority"))
        .define_method("dirty_network_limits", &OCC::Folder::setDirtyNetworkLimits)
        .define_method("reload_sync_options", &OCC::Folder::reloadSyncOptions)
        .define_method("wipe_for_removal", &OCC::Folder::wipeForRemoval)
        .define_method("sync_result", &OCC::Folder::syncResult);

    Enum<OCC::Folder::ChangeReason> rb_cChangeReason =
        define_enum_under<OCC::Folder::ChangeReason>("ChangeReason", rb_cFolder)
        .define_value("Other", OCC::Folder::ChangeReason::Other)
        .define_value("UnLock", OCC::Folder::ChangeReason::UnLock);

    rb_cFolder.define_method("watched_paths_changed", &OCC::Folder::slotWatchedPathsChanged,
        Arg("paths"), Arg("reason") = OCC::Folder::ChangeReason::Other);

    Data_Type<OCC::LocalDiscoveryTracker> rb_cLocalDiscoveryTracker =
        define_class_under<OCC::LocalDiscoveryTracker>(rb_mOCC, "LocalDiscoveryTracker")
        .define_constructor(Constructor<OCC::LocalDiscoveryTracker>())
        .define_method("add_touched_path", &OCC::LocalDiscoveryTracker::addTouchedPath,
            Arg("relative_path"))
        .define_method("start_sync_full_discovery", &OCC::LocalDiscoveryTracker::startSyncFullDiscovery)
        .define_method("start_sync_partial_discovery", &OCC::LocalDiscoveryTracker::startSyncPartialDiscovery)
        .define_method("local_discovery_paths", &OCC::LocalDiscoveryTracker::localDiscoveryPaths)
        .define_method("sync_finished", &OCC::LocalDiscoveryTracker::slotSyncFinished, Arg("success"));

    // -----------------------------------------------------------------------------------------
    // Layer 3 - deciding it is an upload
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::ProcessDirectoryJob> rb_cProcessDirectoryJob =
        define_class_under<OCC::ProcessDirectoryJob>(rb_mOCC, "ProcessDirectoryJob");

    // -----------------------------------------------------------------------------------------
    // Layer 4 - performing the upload
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::PropagatorJob> rb_cPropagatorJob =
        define_class_under<OCC::PropagatorJob>(rb_mOCC, "PropagatorJob")
        .define_method("path", &OCC::PropagatorJob::path)
        .define_method("state", &OCC::PropagatorJob::state)
        .define_method("parallelism", &OCC::PropagatorJob::parallelism)
        .define_method("likely_finished_quickly?", &OCC::PropagatorJob::isLikelyFinishedQuickly)
        .define_method("committed_disk_space", &OCC::PropagatorJob::committedDiskSpace);

    Enum<OCC::PropagatorJob::AbortType> rb_cAbortType =
        define_enum_under<OCC::PropagatorJob::AbortType>("AbortType", rb_cPropagatorJob)
        .define_value("Synchronous", OCC::PropagatorJob::Synchronous)
        .define_value("Asynchronous", OCC::PropagatorJob::Asynchronous);

    rb_cPropagatorJob.define_method("abort", &OCC::PropagatorJob::abort, Arg("abort_type"));

    Data_Type<OCC::PropagateItemJob> rb_cPropagateItemJob =
        define_class_under<OCC::PropagateItemJob, OCC::PropagatorJob>(rb_mOCC, "PropagateItemJob");

    Data_Type<OCC::OwncloudPropagator> rb_cOwncloudPropagator =
        define_class_under<OCC::OwncloudPropagator>(rb_mOCC, "OwncloudPropagator")
        .define_method("create_job", &OCC::OwncloudPropagator::createJob, Arg("item"))
        .define_method("local_path", &OCC::OwncloudPropagator::localPath)
        .define_method("remote_path", &OCC::OwncloudPropagator::remotePath)
        .define_method("full_local_path", &OCC::OwncloudPropagator::fullLocalPath, Arg("file_name"))
        .define_method("full_remote_path", &OCC::OwncloudPropagator::fullRemotePath, Arg("file_name"))
        .define_method("web_dav_url", &OCC::OwncloudPropagator::webDavUrl)
        .define_method("small_file_size", &OCC::OwncloudPropagator::smallFileSize)
        .define_method("maximum_active_transfer_job", &OCC::OwncloudPropagator::maximumActiveTransferJob)
        .define_method("hard_maximum_active_job", &OCC::OwncloudPropagator::hardMaximumActiveJob)
        .define_method("adjust_renamed_path", &OCC::OwncloudPropagator::adjustRenamedPath, Arg("original"))
        .define_method("schedule_next_job", &OCC::OwncloudPropagator::scheduleNextJob)
        .define_method("abort", &OCC::OwncloudPropagator::abort);

    Data_Type<OCC::BandwidthManager> rb_cBandwidthManager =
        define_class_under<OCC::BandwidthManager>(rb_mOCC, "BandwidthManager")
        .define_constructor(Constructor<OCC::BandwidthManager, OCC::OwncloudPropagator*>(),
            Arg("propagator").keepAlive())
        .define_method("current_upload_limit", &OCC::BandwidthManager::currentUploadLimit)
        .define_method("current_upload_limit=", &OCC::BandwidthManager::setCurrentUploadLimit, Arg("limit"))
        .define_method("register_upload_device", &OCC::BandwidthManager::registerUploadDevice,
            Arg("device").keepAlive());

    Data_Type<OCC::UploadDevice> rb_cUploadDevice =
        define_class_under<OCC::UploadDevice>(rb_mOCC, "UploadDevice")
        .define_constructor(Constructor<OCC::UploadDevice, const QString&, qint64, qint64, OCC::BandwidthManager*>(),
            Arg("file_name"), Arg("start"), Arg("size"), Arg("bandwidth_manager").keepAlive())
        .define_method("open", &OCC::UploadDevice::open, Arg("mode"))
        .define_method("close", &OCC::UploadDevice::close)
        .define_method("at_end?", &OCC::UploadDevice::atEnd)
        .define_method("size", &OCC::UploadDevice::size)
        .define_method("bytes_available", &OCC::UploadDevice::bytesAvailable)
        .define_method("sequential?", &OCC::UploadDevice::isSequential)
        .define_method("seek", &OCC::UploadDevice::seek, Arg("pos"))
        .define_method("bandwidth_limited?", &OCC::UploadDevice::isBandwidthLimited)
        .define_method("bandwidth_limited=", &OCC::UploadDevice::setBandwidthLimited, Arg("limited"))
        .define_method("choked?", &OCC::UploadDevice::isChoked)
        .define_method("choked=", &OCC::UploadDevice::setChoked, Arg("choked"))
        .define_method("give_bandwidth_quota", &OCC::UploadDevice::giveBandwidthQuota, Arg("quota"))
        .define_method("job_upload_progress", &OCC::UploadDevice::slotJobUploadProgress,
            Arg("sent"), Arg("total"));

    Data_Type<OCC::PropagateUploadFileCommon> rb_cPropagateUploadFileCommon =
        define_class_under<OCC::PropagateUploadFileCommon, OCC::PropagateItemJob>(rb_mOCC, "PropagateUploadFileCommon")
        .define_method("start", &OCC::PropagateUploadFileCommon::start)
        .define_method("do_start_upload", &OCC::PropagateUploadFileCommon::doStartUpload)
        .define_method("finalize", &OCC::PropagateUploadFileCommon::finalize)
        .define_method("delete_existing=", &OCC::PropagateUploadFileCommon::setDeleteExisting, Arg("enabled"))
        .define_method("abort_with_error", &OCC::PropagateUploadFileCommon::abortWithError,
            Arg("status"), Arg("error"))
        .define_method("add_child_job", &OCC::PropagateUploadFileCommon::addChildJob, Arg("job").keepAlive())
        .define_method("likely_finished_quickly?", &OCC::PropagateUploadFileCommon::isLikelyFinishedQuickly);

    Data_Type<OCC::PropagateUploadFileTUS> rb_cPropagateUploadFileTUS =
        define_class_under<OCC::PropagateUploadFileTUS, OCC::PropagateUploadFileCommon>(rb_mOCC, "PropagateUploadFileTUS")
        .define_constructor(
            Constructor<OCC::PropagateUploadFileTUS,
            OCC::OwncloudPropagator*,
            const OCC::SyncFileItemPtr&>(),
            Arg("propagator").keepAlive(),
            Arg("item")
          )
        .define_method("do_start_upload", &OCC::PropagateUploadFileTUS::doStartUpload)
        .define_method("abort", &OCC::PropagateUploadFileTUS::abort, Arg("abort_type"));

    Data_Type<OCC::PUTFileJob> rb_cPUTFileJob =
        define_class_under<OCC::PUTFileJob>(rb_mOCC, "PUTFileJob")
        .define_method("start", &OCC::PUTFileJob::start)
        .define_method("error_string", &OCC::PUTFileJob::errorString)
        .define_method("ms_since_start", [](const OCC::PUTFileJob& self)
        {
            return self.msSinceStart().count();
        });

    Data_Type<OCC::PropagateUploadFileV1> rb_cPropagateUploadFileV1 =
        define_class_under<OCC::PropagateUploadFileV1, OCC::PropagateUploadFileCommon>(rb_mOCC, "PropagateUploadFileV1")
        .define_constructor(Constructor<OCC::PropagateUploadFileV1, OCC::OwncloudPropagator*, const OCC::SyncFileItemPtr&>(),
            Arg("propagator").keepAlive(), Arg("item"))
        .define_method("do_start_upload", &OCC::PropagateUploadFileV1::doStartUpload)
        .define_method("abort", &OCC::PropagateUploadFileV1::abort, Arg("abort_type"));
}
