#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "http_request.hpp"
#include "json.hpp"
#include "router.hpp"

namespace http {

struct Todo {
    std::int64_t id = 0;
    std::string title;
    bool done = false;
};

Json to_json(const Todo& todo);

// In-memory to-do list behind the demo JSON API. Every member function
// locks, so handlers on different worker threads can share one store.
class TodoStore {
public:
    std::vector<Todo> list() const;
    std::optional<Todo> get(std::int64_t id) const;
    Todo create(std::string title, bool done = false);
    // Applies the fields that are set; returns the updated item, or nullopt
    // if no item has that id.
    std::optional<Todo> update(std::int64_t id, std::optional<std::string> title,
                               std::optional<bool> done);
    bool remove(std::int64_t id);
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<std::int64_t, Todo> todos_; // ordered by id
    std::int64_t next_id_ = 1;
};

// Parses a JSON request body. Throws ApiError (415 if Content-Type is not
// application/json, 400 for malformed JSON).
Json parse_json_body(const HttpRequest& request);

// An error a JSON API handler wants to answer with; register_demo_api()
// turns it into HttpResponse::json_error(status, message).
class ApiError : public std::runtime_error {
public:
    ApiError(int status, const std::string& message)
        : std::runtime_error(message), status_(status) {}
    int status() const noexcept { return status_; }

private:
    int status_;
};

// Registers the demo JSON endpoints under /api:
//   GET    /api              index of the endpoints
//   GET    /api/status       server name, version and uptime
//   GET    /api/todos        all items (?done=true|false filters)
//   POST   /api/todos        create {"title": "...", "done": false} -> 201
//   GET    /api/todos/:id    one item
//   PATCH  /api/todos/:id    update "title" and/or "done"
//   DELETE /api/todos/:id    remove -> 204
// `store` must outlive the router.
void register_demo_api(Router& router, TodoStore& store,
                       std::chrono::steady_clock::time_point started =
                           std::chrono::steady_clock::now());

} // namespace http
