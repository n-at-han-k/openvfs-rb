#include <rice/rice.hpp>
#include <rice/stl.hpp>

#include <openvfs/openvfs.h>
#include <openvfs/openvfconstants.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

using OpenVFS::PlaceHolderAttributes;
using OpenVFS::Registration;
using OpenVFS::Constants::PinStates;
using OpenVFS::Constants::States;
using Rice::Data_Type;
using Rice::Enum;
using Rice::Module;

std::filesystem::path path_from_string(const std::string &path)
{
    return std::filesystem::path(path);
}

std::string path_to_string(const std::filesystem::path &path)
{
    return path.string();
}

std::vector<uint8_t> bytes_from_string(const std::string &data)
{
    return {data.begin(), data.end()};
}

std::string string_from_bytes(const std::vector<uint8_t> &data)
{
    if (data.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(data.data()), data.size()};
}

}

extern "C" void Init_openvfs()
{
    Module rb_mOpenVFS = Rice::define_module("OpenVFS");
    Module rb_mConstants = Rice::define_module_under(rb_mOpenVFS, "Constants");

    Enum<States> rb_eStates = Rice::define_enum_under<States>("States", rb_mConstants)
        .define_value("Hydrated", States::Hydrated)
        .define_value("DeHydrated", States::DeHydrated)
        .define_value("Hydrating", States::Hydrating);

    Enum<PinStates> rb_ePinStates = Rice::define_enum_under<PinStates>("PinStates", rb_mConstants)
        .define_value("Unspecified", PinStates::Unspecified)
        .define_value("OnlineOnly", PinStates::OnlineOnly)
        .define_value("AlwaysLocal", PinStates::AlwaysLocal)
        .define_value("Inherited", PinStates::Inherited)
        .define_value("Excluded", PinStates::Excluded);

    Module rb_mXAttributeNames = Rice::define_module_under(rb_mConstants, "XAttributeNames");
    rb_mXAttributeNames.define_constant("Data", std::string(OpenVFS::Constants::XAttributeNames::Data));
    rb_mXAttributeNames.define_constant("RealSize", std::string(OpenVFS::Constants::XAttributeNames::RealSize));
    rb_mXAttributeNames.define_constant("Registration", std::string(OpenVFS::Constants::XAttributeNames::Registration));

    Data_Type<Registration> rb_cRegistration = Rice::define_class_under<Registration>(rb_mOpenVFS, "Registration")
        .define_singleton_function("register_filesystem", [](const std::string &path, const std::string &owner) {
            return Registration::registerFilesystem(path_from_string(path), owner);
        })
        .define_singleton_function("from_attributes", [](const std::string &path) {
            return Registration::fromAttributes(path_from_string(path));
        })
        .define_singleton_function("from_data", [](const std::string &path, const std::string &data) {
            return Registration::fromData(path_from_string(path), bytes_from_string(data));
        })
        .define_singleton_function("unregister_filesystem", &Registration::unregisterFilesystem)
        .define_method("to_data", [](const Registration &self) { return string_from_bytes(self.toData()); })
        .define_method("path", [](const Registration &self) { return path_to_string(self.path()); })
        .define_method("owner", &Registration::owner)
        .define_method("version", &Registration::version)
        .define_method("error", &Registration::error)
        .define_method("ok?", &Registration::isOk);

    Data_Type<PlaceHolderAttributes> rb_cPlaceHolderAttributes = Rice::define_class_under<PlaceHolderAttributes>(rb_mOpenVFS, "PlaceHolderAttributes")
        .define_singleton_function("create", [](const std::string &path, const std::string &etag, const std::string &file_id, std::size_t size) {
            return PlaceHolderAttributes::create(path_from_string(path), etag, file_id, size);
        })
        .define_singleton_function("from_data", [](const std::string &path, const std::string &data) {
            return PlaceHolderAttributes::fromData(path_from_string(path), bytes_from_string(data));
        })
        .define_singleton_function("from_attributes", [](const std::string &path) {
            return PlaceHolderAttributes::fromAttributes(path_from_string(path));
        })
        .define_method("to_data", [](const PlaceHolderAttributes &self) { return string_from_bytes(self.toData()); })
        .define_method("real_size", &PlaceHolderAttributes::realSize)
        .define_method("validate?", &PlaceHolderAttributes::validate)
        .define_method("ok?", &PlaceHolderAttributes::isOk)
        .define_method("absolute_path", [](const PlaceHolderAttributes &self) { return path_to_string(self.absolutePath); })
        .define_method("absolute_path=", [](PlaceHolderAttributes &self, const std::string &path) { self.absolutePath = path_from_string(path); })
        .define_attr("etag", &PlaceHolderAttributes::etag)
        .define_attr("file_id", &PlaceHolderAttributes::fileId)
        .define_attr("size", &PlaceHolderAttributes::size)
        .define_attr("state", &PlaceHolderAttributes::state)
        .define_attr("pin_state", &PlaceHolderAttributes::pinState);
}
