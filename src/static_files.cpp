#include "static_files.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "mime.hpp"

namespace http {

namespace fs = std::filesystem;

namespace {

HttpResponse forbidden() {
    return HttpResponse::text(status::kForbidden, "403 Forbidden\n");
}

} // namespace

StaticFiles::StaticFiles(fs::path root, std::string index_file)
    : index_file_(std::move(index_file)) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        throw std::invalid_argument("document root is not a directory: " + root.string());
    }
    // Canonical form (absolute, no symlinks) so containment checks are exact.
    root_ = fs::canonical(root);
}

bool StaticFiles::inside_root(const fs::path& path) const {
    // Component-wise prefix check, so "/srv/www-private" is not inside "/srv/www".
    return std::mismatch(root_.begin(), root_.end(), path.begin(), path.end()).first ==
           root_.end();
}

std::optional<fs::path> StaticFiles::resolve(std::string_view url_path) const {
    if (url_path.empty() || url_path.front() != '/' || url_path.find('\\') != std::string_view::npos) {
        return std::nullopt;
    }

    fs::path target = root_;
    std::string_view rest = url_path.substr(1);
    while (!rest.empty()) {
        const auto slash = rest.find('/');
        const std::string_view segment = rest.substr(0, slash);
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
        if (segment.empty()) {
            continue; // "//" or trailing slash
        }
        // Rejects ".", "..", and hidden files such as ".git" or ".env".
        if (segment.front() == '.') {
            return std::nullopt;
        }
        target /= fs::path(std::string(segment));
    }

    // Lexically the path is inside the root; make sure symlinks don't lead out.
    std::error_code error;
    const fs::path resolved = fs::weakly_canonical(target, error);
    if (error || !inside_root(resolved)) {
        return std::nullopt;
    }
    return resolved;
}

HttpResponse StaticFiles::serve(const HttpRequest& request) const {
    if (request.method != Method::Get && request.method != Method::Head) {
        HttpResponse response = HttpResponse::text(status::kMethodNotAllowed,
                                                   "405 Method Not Allowed: " +
                                                       std::string(to_string(request.method)) +
                                                       "\n");
        response.set_header("Allow", "GET, HEAD");
        return response;
    }

    // request.path is already percent-decoded, so "%2e%2e" arrives as "..".
    const auto resolved = resolve(request.path);
    if (!resolved) {
        return forbidden();
    }
    fs::path target = *resolved;

    std::error_code error;
    if (fs::is_directory(target, error)) {
        if (request.path.back() != '/') {
            std::string location = request.path + "/";
            if (!request.query.empty()) {
                location += "?" + request.query;
            }
            HttpResponse response = HttpResponse::text(status::kMovedPermanently,
                                                       "Moved to " + location + "\n");
            response.set_header("Location", std::move(location));
            return response;
        }
        target = fs::weakly_canonical(target / index_file_, error);
        if (error || !inside_root(target)) {
            return forbidden();
        }
    }

    if (!fs::is_regular_file(target, error)) {
        return HttpResponse::text(status::kNotFound, "404 Not Found: " + request.path + "\n");
    }
    return serve_file(target);
}

HttpResponse StaticFiles::serve_file(const fs::path& file) const {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return forbidden();
    }
    std::string body{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw std::runtime_error("failed to read " + file.string());
    }

    HttpResponse response(status::kOk);
    response.set_body(std::move(body), std::string(mime_type(file.string())));
    return response;
}

} // namespace http
