#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "http_request.hpp"
#include "http_response.hpp"

namespace http {

// Serves files from a document root directory.
//  - "/css/site.css" maps to "<root>/css/site.css"
//  - directories serve their index.html; a directory requested without a
//    trailing slash is redirected (301) to the slash form
//  - Content-Type is derived from the file extension
//  - only GET and HEAD are allowed (405 otherwise)
//  - requests can never escape the root: ".." segments, hidden files
//    (".git", ".env") and symlinks pointing outside the root get 403
class StaticFiles {
public:
    explicit StaticFiles(std::filesystem::path root, std::string index_file = "index.html");

    HttpResponse serve(const HttpRequest& request) const;

    const std::filesystem::path& root() const noexcept { return root_; }

    // Maps a decoded URL path to a file system path inside the root.
    // Returns std::nullopt if the path is unsafe (traversal, hidden file,
    // or resolves outside the root through a symlink).
    std::optional<std::filesystem::path> resolve(std::string_view url_path) const;

private:
    bool inside_root(const std::filesystem::path& path) const;
    HttpResponse serve_file(const std::filesystem::path& file) const;

    std::filesystem::path root_;
    std::string index_file_;
};

} // namespace http
