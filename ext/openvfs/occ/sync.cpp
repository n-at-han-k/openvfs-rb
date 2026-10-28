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
#include <libsync/capabilities.h>
#include <libsync/common/version.h>
#include <libsync/creds/abstractcredentials.h>
#include <libsync/creds/credentialmanager.h>

#include <qt6keychain/keychain.h>

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>
#include <QVariant>

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

extern "C" void Init_occ()
{
    Module rb_mOCC = define_module_under(define_module("OpenVFS"), "OCC");
    rb_mOCC.define_constant("DESKTOP_VERSION", OCC::Version::displayString().toStdString());
    rb_mOCC.define_constant("DESKTOP_REVISION", OCC::Version::gitSha().toStdString());

    // -----------------------------------------------------------------------------------------
    // The desktop's entry point: src/gui/main.cpp, reduced by the patches to its non-UI
    // bootstrap. It takes the arguments the command line already accepts.
    // -----------------------------------------------------------------------------------------

    rb_mOCC.define_module_function("init", &OCC::init, Arg("arguments"));

    // -----------------------------------------------------------------------------------------
    // Errors the keychain reports.
    // -----------------------------------------------------------------------------------------

    Enum<QKeychain::Error> rb_cKeychainError =
        define_enum_under<QKeychain::Error>("KeychainError", rb_mOCC)
        .define_value("NoError", QKeychain::NoError)
        .define_value("EntryNotFound", QKeychain::EntryNotFound)
        .define_value("CouldNotDeleteEntry", QKeychain::CouldNotDeleteEntry)
        .define_value("AccessDeniedByUser", QKeychain::AccessDeniedByUser)
        .define_value("AccessDenied", QKeychain::AccessDenied)
        .define_value("NoBackendAvailable", QKeychain::NoBackendAvailable)
        .define_value("NotImplemented", QKeychain::NotImplemented)
        .define_value("OtherError", QKeychain::OtherError);

    Data_Type<QKeychain::Job> rb_cKeychainJob =
        define_class_under<QKeychain::Job>(rb_mOCC, "KeychainJob")
        .define_method("start", &QKeychain::Job::start)
        .define_method("service", &QKeychain::Job::service)
        .define_method("error", &QKeychain::Job::error)
        .define_method("error_string", &QKeychain::Job::errorString)
        .define_method("key", &QKeychain::Job::key)
        .define_method("key=", &QKeychain::Job::setKey, Arg("key"))
        .define_method("auto_delete?", &QKeychain::Job::autoDelete)
        .define_method("auto_delete=", &QKeychain::Job::setAutoDelete, Arg("auto_delete"))
        .define_method("insecure_fallback?", &QKeychain::Job::insecureFallback)
        .define_method("insecure_fallback=", &QKeychain::Job::setInsecureFallback, Arg("insecure_fallback"));

    // -----------------------------------------------------------------------------------------
    // OCC::CredentialManager - the account's keychain.
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::CredentialJob> rb_cCredentialJob =
        define_class_under<OCC::CredentialJob>(rb_mOCC, "CredentialJob")
        .define_method("key", &OCC::CredentialJob::key)
        .define_method("error", &OCC::CredentialJob::error)
        .define_method("error_string", &OCC::CredentialJob::errorString)
        .define_method("data", &OCC::CredentialJob::data);

    Data_Type<OCC::CredentialManager> rb_cCredentialManager =
        define_class_under<OCC::CredentialManager>(rb_mOCC, "CredentialManager")
        .define_method("get", &OCC::CredentialManager::get, Arg("key"))
        .define_method("set", &OCC::CredentialManager::set, Arg("key"), Arg("data"))
        .define_method("remove", &OCC::CredentialManager::remove, Arg("key"))
        .define_method("contains?", &OCC::CredentialManager::contains, Arg("key"))
        .define_method("account", &OCC::CredentialManager::account);

    // -----------------------------------------------------------------------------------------
    // OCC::Account - one configured server connection.
    // -----------------------------------------------------------------------------------------

    Data_Type<OCC::AbstractCredentials> rb_cAbstractCredentials =
        define_class_under<OCC::AbstractCredentials>(rb_mOCC, "AbstractCredentials")
        .define_method("ready?", &OCC::AbstractCredentials::ready)
        .define_method("was_fetched?", &OCC::AbstractCredentials::wasFetched)
        .define_method("fetch_from_keychain", &OCC::AbstractCredentials::fetchFromKeychain)
        .define_method("invalidate_token", &OCC::AbstractCredentials::invalidateToken)
        .define_method("forget_sensitive_data", &OCC::AbstractCredentials::forgetSensitiveData);

    Data_Type<OCC::Capabilities> rb_cCapabilities =
        define_class_under<OCC::Capabilities>(rb_mOCC, "Capabilities");

    Data_Type<OCC::Account> rb_cAccount =
        define_class_under<OCC::Account>(rb_mOCC, "Account")
        .define_singleton_function("common_cache_directory", &OCC::Account::commonCacheDirectory)
        .define_singleton_function("common_cache_directory=", &OCC::Account::setCommonCacheDirectory,
            Arg("directory"))
        .define_method("uuid", &OCC::Account::uuid)
        .define_method("url", &OCC::Account::url)
        .define_method("url=", &OCC::Account::setUrl, Arg("url"))
        .define_method("host_name", &OCC::Account::hostName)
        .define_method("dav_display_name", &OCC::Account::davDisplayName)
        .define_method("dav_display_name=", &OCC::Account::setDavDisplayName, Arg("display_name"))
        .define_method("display_name_with_host", &OCC::Account::displayNameWithHost)
        .define_method("default_sync_root", &OCC::Account::defaultSyncRoot)
        .define_method("default_sync_root=", &OCC::Account::setDefaultSyncRoot, Arg("sync_root"))
        .define_method("default_sync_root?", &OCC::Account::hasDefaultSyncRoot)
        .define_method("credential_manager", &OCC::Account::credentialManager)
        .define_method("credentials", &OCC::Account::credentials)
        .define_method("credentials=", &OCC::Account::setCredentials, Arg("credentials").keepAlive())
        .define_method("capabilities", &OCC::Account::capabilities)
        .define_method("capabilities=", &OCC::Account::setCapabilities, Arg("capabilities"))
        .define_method("capabilities?", &OCC::Account::hasCapabilities)
        .define_method("server_support_level", &OCC::Account::serverSupportLevel)
        .define_method("http2_supported?", &OCC::Account::isHttp2Supported)
        .define_method("http2_supported=", &OCC::Account::setHttp2Supported, Arg("supported"))
        .define_method("clear_cookie_jar", &OCC::Account::clearCookieJar)
        .define_method("clear_am_cache", &OCC::Account::clearAMCache)
        .define_method("invalid_credentials_encountered", &OCC::Account::invalidCredentialsEncountered);

    Enum<OCC::Account::ServerSupportLevel> rb_cServerSupportLevel =
        define_enum_under<OCC::Account::ServerSupportLevel>("ServerSupportLevel", rb_cAccount)
        .define_value("Supported", OCC::Account::ServerSupportLevel::Supported)
        .define_value("Unsupported", OCC::Account::ServerSupportLevel::Unsupported)
        .define_value("Unknown", OCC::Account::ServerSupportLevel::Unknown);

    // Account::create hands back a QSharedPointer, and every libsync class takes one.
    // Wrapping the pointer itself - rather than what it points at - is what keeps the
    // reference count alive while Ruby holds it.
    Data_Type<OCC::AccountPtr> rb_cAccountPtr =
        define_class_under<OCC::AccountPtr>(rb_mOCC, "AccountPtr")
        .define_singleton_function("create", &OCC::Account::create, Arg("uuid"))
        .define_method("get", [](OCC::AccountPtr& self) { return self.data(); })
        .define_method("null?", [](const OCC::AccountPtr& self) { return self.isNull(); });
}
