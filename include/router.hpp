#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "http_request.hpp"
#include "http_response.hpp"

namespace http {

using Handler = std::function<HttpResponse(const HttpRequest&)>;

// Maps (method, path) pairs to handlers.
//  - paths may contain parameters, e.g. "/users/:id/posts/:post"; captured
//    values are available through HttpRequest::param()
//  - exact (static) routes win over parameterised ones, and parameterised
//    routes are tried in registration order
//  - unknown path              -> fallback handler if set, else 404 Not Found
//  - known path, wrong method  -> 405 Method Not Allowed + Allow header
//  - HEAD falls back to the GET handler when no HEAD route exists
class Router {
public:
    Router& add(Method method, std::string path, Handler handler);

    Router& get(std::string path, Handler handler) {
        return add(Method::Get, std::move(path), std::move(handler));
    }
    Router& post(std::string path, Handler handler) {
        return add(Method::Post, std::move(path), std::move(handler));
    }
    Router& put(std::string path, Handler handler) {
        return add(Method::Put, std::move(path), std::move(handler));
    }
    Router& del(std::string path, Handler handler) {
        return add(Method::Delete, std::move(path), std::move(handler));
    }

    // Handler for requests whose path matches no route (e.g. static files).
    Router& fallback(Handler handler);

    HttpResponse route(const HttpRequest& request) const;

    std::size_t size() const noexcept;

private:
    using MethodHandlers = std::map<Method, Handler>;

    struct Segment {
        std::string text; // literal text, or the parameter name
        bool is_param = false;
    };

    struct PatternRoute {
        std::string pattern;
        std::vector<Segment> segments;
        MethodHandlers handlers;

        // Returns true and fills `params` when `parts` matches this pattern.
        bool match(const std::vector<std::string_view>& parts,
                   std::unordered_map<std::string, std::string>& params) const;
    };

    static std::vector<Segment> compile(const std::string& pattern);

    // Static path -> (method -> handler); std::map keeps Allow headers sorted.
    std::map<std::string, MethodHandlers> routes_;
    std::vector<PatternRoute> patterns_;
    Handler fallback_;
};

} // namespace http
