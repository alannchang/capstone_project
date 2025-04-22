#include "python_bindings.hpp"

#include <iostream>
#include <filesystem>
#include <pybind11/embed.h>

namespace py = pybind11;

GmailManagerWrapper::GmailManagerWrapper(const std::string& credentials_path, const std::string& token_path) {
    // Insert Gmail API directory to sys.path
    std::filesystem::path gmail_api_path = std::filesystem::canonical("../gmail-api");

    py::module_ sys = py::module_::import("sys");
    sys.attr("path").attr("insert")(0, gmail_api_path.string());

    // Import Python GmailManager and instantiate it
    py::object api = py::module_::import("api");
    py::object GmailManager = api.attr("GmailManager");

    gmail = GmailManager(credentials_path, token_path);
}

void GmailManagerWrapper::print_profile() const {
    py::object profile = gmail.attr("get_profile")();
    std::cout << "Gmail Profile:\n" << std::string(py::str(profile)) << std::endl;
}

py::object& GmailManagerWrapper::get_instance() {
    return gmail;
}

