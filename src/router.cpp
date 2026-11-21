#include "router.hpp"

#include <set>
#include <stdexcept>

namespace http {

namespace {

// Splits "/users/42/posts" into {"users", "42", "posts"}. Empty segments
// are kept, so "/users/" yields {"users", ""} and does not match "/users/:id".
std::vector<std::string_view> split_path(std::string_view path) {
    std::vector<std::string_view> parts;
    if (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    while (true) {
        const auto slash = path.find('/');
        parts.push_back(path.substr(0, slash));
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }
    return parts;
}

std::string allowed_methods(const std::set<Method>& methods) {
    std::string allow;
    const auto append = [&allow](Method method) {
        if (!allow.empty()) {
            allow += ", ";
        }
        allow += to_string(method);
    };
    for (const Method method : methods) {
        append(method);
        if (method == Method::Get && methods.count(Method::Head) == 0) {
            append(Method::Head);
        }
    }
    return allow;
}

const Handler* find_handler(const std::map<Method, Handler>& handlers, Method method) {
    auto it = handlers.find(method);
    if (it == handlers.end() && method == Method::Head) {
        it = handlers.find(Method::Get);
    }
    return it == handlers.end() ? nullptr : &it->second;
}

} // namespace

bool Router::PatternRoute::match(const std::vector<std::string_view>& parts,
                                 std::unordered_map<std::string, std::string>& params) const {
    if (parts.size() != segments.size()) {
        return false;
    }
    std::unordered_map<std::string, std::string> captured;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const Segment& segment = segments[i];
        if (segment.is_param) {
            if (parts[i].empty()) {
                return false;
            }
            captured.emplace(segment.text, std::string(parts[i]));
        } else if (segment.text != parts[i]) {
            return false;
        }
    }
    params = std::move(captured);
    return true;
}

std::vector<Router::Segment> Router::compile(const std::string& pattern) {
    std::vector<Segment> segments;
    std::set<std::string_view> names;
    for (const std::string_view part : split_path(pattern)) {
        if (!part.empty() && part.front() == ':') {
            const std::string_view name = part.substr(1);
            if (name.empty()) {
                throw std::invalid_argument("empty parameter name in route: " + pattern);
            }
            if (!names.insert(name).second) {
                throw std::invalid_argument("duplicate parameter '" + std::string(name) +
                                            "' in route: " + pattern);
            }
            segments.push_back({std::string(name), true});
        } else {
            segments.push_back({std::string(part), false});
        }
    }
    return segments;
}

Router& Router::add(Method method, std::string path, Handler handler) {
    if (path.empty() || path.front() != '/') {
        throw std::invalid_argument("route path must start with '/': " + path);
    }
    if (!handler) {
        throw std::invalid_argument("route handler must not be empty");
    }

    if (path.find("/:") == std::string::npos) {
        routes_[std::move(path)][method] = std::move(handler);
        return *this;
    }

    for (PatternRoute& route : patterns_) {
        if (route.pattern == path) {
            route.handlers[method] = std::move(handler);
            return *this;
        }
    }
    PatternRoute route;
    route.segments = compile(path);
    route.pattern = std::move(path);
    route.handlers[method] = std::move(handler);
    patterns_.push_back(std::move(route));
    return *this;
}

HttpResponse Router::route(const HttpRequest& request) const {
    std::set<Method> allowed;
    bool path_known = false;

    if (const auto it = routes_.find(request.path); it != routes_.end()) {
        path_known = true;
        if (const Handler* handler = find_handler(it->second, request.method)) {
            return (*handler)(request);
        }
        for (const auto& [method, handler] : it->second) {
            allowed.insert(method);
        }
    }

    const auto parts = split_path(request.path);
    std::unordered_map<std::string, std::string> params;
    for (const PatternRoute& route : patterns_) {
        if (!route.match(parts, params)) {
            continue;
        }
        path_known = true;
        if (const Handler* handler = find_handler(route.handlers, request.method)) {
            HttpRequest matched = request;
            matched.params = std::move(params);
            return (*handler)(matched);
        }
        for (const auto& [method, handler] : route.handlers) {
            allowed.insert(method);
        }
    }

    if (!path_known) {
        return HttpResponse::text(status::kNotFound, "404 Not Found: " + request.path + "\n");
    }

    HttpResponse response = HttpResponse::text(
        status::kMethodNotAllowed,
        "405 Method Not Allowed: " + std::string(to_string(request.method)) + "\n");
    response.set_header("Allow", allowed_methods(allowed));
    return response;
}

std::size_t Router::size() const noexcept {
    std::size_t count = 0;
    for (const auto& [path, handlers] : routes_) {
        count += handlers.size();
    }
    for (const PatternRoute& route : patterns_) {
        count += route.handlers.size();
    }
    return count;
}

} // namespace http
