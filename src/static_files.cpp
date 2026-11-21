#include "static_files.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "mime.hpp"

namespace http {

namespace fs = std::filesystem;

StaticFiles::StaticFiles(fs::path root, std::string index_file)
    : root_(std::move(root)), index_file_(std::move(index_file)) {
    std::error_code error;
    if (!fs::is_directory(root_, error)) {
        throw std::invalid_argument("document root is not a directory: " + root_.string());
    }
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

    // request.path is already percent-decoded and always starts with '/'.
    fs::path target = root_ / fs::path(request.path).relative_path();

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
        target /= index_file_;
    }

    if (!fs::is_regular_file(target, error)) {
        return HttpResponse::text(status::kNotFound, "404 Not Found: " + request.path + "\n");
    }
    return serve_file(target);
}

HttpResponse StaticFiles::serve_file(const fs::path& file) const {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return HttpResponse::text(status::kForbidden, "403 Forbidden\n");
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
