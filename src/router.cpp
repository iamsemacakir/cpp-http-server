#include "router.hpp"

#include <stdexcept>

namespace http {

namespace {

std::string allowed_methods(const std::map<Method, Handler>& handlers) {
    std::string allow;
    const auto append = [&allow](Method method) {
        if (!allow.empty()) {
            allow += ", ";
        }
        allow += to_string(method);
    };
    for (const auto& [method, handler] : handlers) {
        append(method);
        if (method == Method::Get && handlers.count(Method::Head) == 0) {
            append(Method::Head);
        }
    }
    return allow;
}

} // namespace

Router& Router::add(Method method, std::string path, Handler handler) {
    if (path.empty() || path.front() != '/') {
        throw std::invalid_argument("route path must start with '/': " + path);
    }
    if (!handler) {
        throw std::invalid_argument("route handler must not be empty");
    }
    routes_[std::move(path)][method] = std::move(handler);
    return *this;
}

HttpResponse Router::route(const HttpRequest& request) const {
    const auto path_it = routes_.find(request.path);
    if (path_it == routes_.end()) {
        return HttpResponse::text(status::kNotFound, "404 Not Found: " + request.path + "\n");
    }

    const auto& handlers = path_it->second;
    auto handler_it = handlers.find(request.method);
    if (handler_it == handlers.end() && request.method == Method::Head) {
        handler_it = handlers.find(Method::Get);
    }
    if (handler_it == handlers.end()) {
        HttpResponse response = HttpResponse::text(
            status::kMethodNotAllowed,
            "405 Method Not Allowed: " + std::string(to_string(request.method)) + "\n");
        response.set_header("Allow", allowed_methods(handlers));
        return response;
    }

    return handler_it->second(request);
}

std::size_t Router::size() const noexcept {
    std::size_t count = 0;
    for (const auto& [path, handlers] : routes_) {
        count += handlers.size();
    }
    return count;
}

} // namespace http
