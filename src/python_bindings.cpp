#include "python_bindings.hpp"

#include <iostream>
#include <filesystem>
#include <pybind11/embed.h>

namespace py = pybind11;

GmailManagerWrapper::GmailManagerWrapper(const std::string& credentials_path, const std::string& token_path) {
    // set path
    std::filesystem::path api_path = std::filesystem::current_path() / "runtime-deps";
    py::module_ sys = py::module_::import("sys");
    sys.attr("path").attr("insert")(0, api_path.string());

    // Import Python GmailManager and instantiate it
    py::object api = py::module_::import("api");
    py::object GmailManager = api.attr("GmailManager");

    gmail = GmailManager(credentials_path, token_path);
}

py::object GmailManagerWrapper::get_profile() const {
    py::object profile = gmail.attr("get_profile")();
    return profile;
}

std::string GmailManagerWrapper::get_profile_str(py::object profile) {
    std::string profile_stats = "";
    profile_stats += "Gmail Account: " + profile["emailAddress"].cast<std::string>() + "\n";
    // profile_stats += "Total Messages: " + profile["messagesTotal"].cast<std::string>() + "\n";
    // profile_stats += "Total Threads: " + profile["threadsTotal"].cast<int>() + "\n";
    // profile_stats += "History ID: " + profile["historyID"].cast<int>() + "\n";
    return profile_stats;
}


py::object& GmailManagerWrapper::get_instance() {
    return gmail;
}

