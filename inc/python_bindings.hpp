#pragma once

#include <pybind11/embed.h>
#include <string>

class GmailManagerWrapper {
public:
    GmailManagerWrapper(const std::string& credentials_path, const std::string& token_path);

    void print_profile() const;

    // Add more methods here
    pybind11::object& get_instance();

private:
    pybind11::object gmail;
};

