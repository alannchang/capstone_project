#include "python_bindings.hpp"

#include <iostream>
#include <filesystem>
#include <pybind11/embed.h>

namespace py = pybind11;

GmailManagerWrapper::GmailManagerWrapper(const std::string& credentials_path, const std::string& token_path, Logger& logger)
    : logger_(logger)
{
    logger_.log(LogLevel::INFO, "Initializing GmailManagerWrapper...");
    // set path for the api.py module
    // Assuming api.py is alongside the executable or in a known relative path
    // If api.py is in runtime-deps relative to build dir, this needs adjustment depending on execution location.
    // A safer approach might be to find the executable's path or pass runtime_deps path as config.
    // For now, assume execution from build directory where runtime-deps exists.
    std::filesystem::path api_module_dir = std::filesystem::current_path() / "runtime-deps";
    py::module_ sys = py::module_::import("sys");
    sys.attr("path").attr("insert")(0, api_module_dir.string());
    logger_.log(LogLevel::DEBUG, "Python sys.path extended with: ", api_module_dir.string());

    // Import Python GmailManager and instantiate it using provided paths
    try {
        logger_.log(LogLevel::DEBUG, "Importing python module 'api'...");
        py::object api = py::module_::import("api");
        logger_.log(LogLevel::DEBUG, "Getting GmailManager class from 'api' module...");
        py::object GmailManager = api.attr("GmailManager");

        logger_.log(LogLevel::INFO, "Instantiating Python GmailManager with creds=", credentials_path, ", token=", token_path);
        // Pass the configured paths to the Python constructor
        gmail = GmailManager(credentials_path, token_path);
        logger_.log(LogLevel::INFO, "Python GmailManager instantiated successfully.");
    } catch (const py::error_already_set& e) {
        logger_.log(LogLevel::ERROR, "Failed to initialize Python GmailManager: ", e.what());
        // Depending on the desired behavior, re-throw or handle the error.
        // Re-throwing makes the failure explicit.
        throw std::runtime_error(std::string("Failed to initialize Python GmailManager: ") + e.what());
    }
}

py::object GmailManagerWrapper::get_profile() const {
    py::gil_scoped_acquire gil;
    logger_.log(LogLevel::DEBUG, "Calling Python get_profile()...");
    py::object profile = gmail.attr("get_profile")();
    // Consider adding logging for the result or potential Python errors here
    return profile;
}

std::string GmailManagerWrapper::get_profile_str(py::object profile) {
    py::gil_scoped_acquire gil;
    logger_.log(LogLevel::DEBUG, "Formatting profile object to string.");
    std::string profile_stats = "";
    try {
        profile_stats += "Gmail Account: " + profile["emailAddress"].cast<std::string>() + "\n";
        // Add more fields if needed, with error handling
    } catch (const py::error_already_set& e) {
        logger_.log(LogLevel::ERROR, "Error accessing profile fields: ", e.what());
        profile_stats += "Error retrieving profile details.\n";
    }
    return profile_stats;
}


py::object& GmailManagerWrapper::get_instance() {
    py::gil_scoped_acquire gil;
    logger_.log(LogLevel::DEBUG, "Returning Python GmailManager instance.");
    return gmail;
}

