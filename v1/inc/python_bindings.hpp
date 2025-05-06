#pragma once

#include <pybind11/embed.h>
#include <string>

class GmailManagerWrapper {
public:
    GmailManagerWrapper(const std::string& credentials_path, const std::string& token_path);

    pybind11::object get_profile() const;

    std::string get_profile_str(pybind11::object profile);

    // Add more methods here
    pybind11::object& get_instance();

private:
    pybind11::object gmail;
};

