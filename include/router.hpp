#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>

#include "http_request.hpp"
#include "http_response.hpp"

namespace http {

using Handler = std::function<HttpResponse(const HttpRequest&)>;

// Maps (method, path) pairs to handlers.
//  - unknown path              -> 404 Not Found
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

    HttpResponse route(const HttpRequest& request) const;

    std::size_t size() const noexcept;

private:
    // path -> (method -> handler); std::map keeps Allow headers sorted.
    std::map<std::string, std::map<Method, Handler>> routes_;
};

} // namespace http
