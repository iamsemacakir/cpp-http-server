#pragma once

#include <filesystem>
#include <string>

#include "http_request.hpp"
#include "http_response.hpp"

namespace http {

// Serves files from a document root directory.
//  - "/css/site.css" maps to "<root>/css/site.css"
//  - directories serve their index.html; a directory requested without a
//    trailing slash is redirected (301) to the slash form
//  - Content-Type is derived from the file extension
//  - only GET and HEAD are allowed (405 otherwise)
class StaticFiles {
public:
    explicit StaticFiles(std::filesystem::path root, std::string index_file = "index.html");

    HttpResponse serve(const HttpRequest& request) const;

    const std::filesystem::path& root() const noexcept { return root_; }

private:
    HttpResponse serve_file(const std::filesystem::path& file) const;

    std::filesystem::path root_;
    std::string index_file_;
};

} // namespace http
