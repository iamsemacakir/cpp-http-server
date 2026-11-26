#include "demo_api.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <utility>

#include "http_response.hpp"
#include "status.hpp"
#include "version.hpp"

namespace http {

namespace {

constexpr std::size_t kMaxTitleLength = 200;

// Wraps a handler so ApiError and JsonError become JSON error responses.
Handler json_handler(Handler handler) {
    return [handler = std::move(handler)](const HttpRequest& request) {
        try {
            return handler(request);
        } catch (const ApiError& error) {
            return HttpResponse::json_error(error.status(), error.what());
        } catch (const JsonError& error) {
            return HttpResponse::json_error(status::kBadRequest, error.what());
        }
    };
}

std::int64_t todo_id(const HttpRequest& request) {
    const std::string text = request.param("id").value_or("");
    std::int64_t id = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
    if (error != std::errc() || end != text.data() + text.size() || id <= 0) {
        throw ApiError(status::kNotFound, "no todo with id '" + text + "'");
    }
    return id;
}

std::string validated_title(const Json& value) {
    if (!value.is_string()) {
        throw ApiError(status::kBadRequest, "'title' must be a string");
    }
    std::string title = value.as_string();
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    title.erase(title.begin(), std::find_if(title.begin(), title.end(), not_space));
    title.erase(std::find_if(title.rbegin(), title.rend(), not_space).base(), title.end());
    if (title.empty()) {
        throw ApiError(status::kBadRequest, "'title' must not be empty");
    }
    if (title.size() > kMaxTitleLength) {
        throw ApiError(status::kBadRequest, "'title' is longer than " +
                                                std::to_string(kMaxTitleLength) +
                                                " characters");
    }
    return title;
}

bool validated_done(const Json& value) {
    if (!value.is_bool()) {
        throw ApiError(status::kBadRequest, "'done' must be true or false");
    }
    return value.as_bool();
}

// Rejects members other than the allowed ones, so typos are reported.
void check_members(const Json& body, std::initializer_list<std::string_view> allowed) {
    if (!body.is_object()) {
        throw ApiError(status::kBadRequest, "request body must be a JSON object");
    }
    for (const auto& [name, value] : body.as_object()) {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            throw ApiError(status::kBadRequest, "unknown field '" + name + "'");
        }
    }
}

} // namespace

Json to_json(const Todo& todo) {
    return Json::object({{"id", todo.id}, {"title", todo.title}, {"done", todo.done}});
}

// --- TodoStore ---------------------------------------------------------------

std::vector<Todo> TodoStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Todo> result;
    result.reserve(todos_.size());
    for (const auto& [id, todo] : todos_) {
        result.push_back(todo);
    }
    return result;
}

std::optional<Todo> TodoStore::get(std::int64_t id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = todos_.find(id);
    if (it == todos_.end()) {
        return std::nullopt;
    }
    return it->second;
}

Todo TodoStore::create(std::string title, bool done) {
    const std::lock_guard<std::mutex> lock(mutex_);
    Todo todo{next_id_++, std::move(title), done};
    todos_.emplace(todo.id, todo);
    return todo;
}

std::optional<Todo> TodoStore::update(std::int64_t id, std::optional<std::string> title,
                                      std::optional<bool> done) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = todos_.find(id);
    if (it == todos_.end()) {
        return std::nullopt;
    }
    if (title) {
        it->second.title = std::move(*title);
    }
    if (done) {
        it->second.done = *done;
    }
    return it->second;
}

bool TodoStore::remove(std::int64_t id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return todos_.erase(id) > 0;
}

std::size_t TodoStore::size() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return todos_.size();
}

// --- Request helpers ---------------------------------------------------------

Json parse_json_body(const HttpRequest& request) {
    std::string type = request.header("Content-Type").value_or("");
    type = type.substr(0, type.find(';'));
    type.erase(std::remove_if(type.begin(), type.end(),
                              [](unsigned char c) { return std::isspace(c); }),
               type.end());
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type != "application/json") {
        throw ApiError(status::kUnsupportedMediaType,
                       "expected Content-Type: application/json");
    }
    try {
        return Json::parse(request.body);
    } catch (const JsonError& error) {
        throw ApiError(status::kBadRequest, error.what());
    }
}

// --- Routes ------------------------------------------------------------------

void register_demo_api(Router& router, TodoStore& store,
                       std::chrono::steady_clock::time_point started) {
    router.get("/api", json_handler([](const HttpRequest&) {
        Json endpoints = Json::array();
        const auto add = [&endpoints](const char* method, const char* path, const char* about) {
            endpoints.push_back(
                Json::object({{"method", method}, {"path", path}, {"description", about}}));
        };
        add("GET", "/api/status", "server name, version and uptime");
        add("GET", "/api/todos", "list todos, optional ?done=true|false");
        add("POST", "/api/todos", "create a todo from {\"title\": ..., \"done\": ...}");
        add("GET", "/api/todos/:id", "fetch one todo");
        add("PATCH", "/api/todos/:id", "update \"title\" and/or \"done\"");
        add("DELETE", "/api/todos/:id", "delete a todo");
        return HttpResponse::json(status::kOk, Json::object({{"endpoints", std::move(endpoints)}}),
                                  2);
    }));

    router.get("/api/status", json_handler([started, &store](const HttpRequest&) {
        const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return HttpResponse::json(
            status::kOk, Json::object({{"name", kServerName},
                                       {"version", kServerVersion},
                                       {"uptime_seconds", static_cast<double>(uptime.count()) / 1000.0},
                                       {"todos", store.size()}}));
    }));

    router.get("/api/todos", json_handler([&store](const HttpRequest& request) {
        std::optional<bool> done_filter;
        if (const auto done = request.query_param("done")) {
            if (*done == "true") {
                done_filter = true;
            } else if (*done == "false") {
                done_filter = false;
            } else {
                throw ApiError(status::kBadRequest, "'done' must be true or false");
            }
        }
        Json items = Json::array();
        for (const Todo& todo : store.list()) {
            if (!done_filter || todo.done == *done_filter) {
                items.push_back(to_json(todo));
            }
        }
        const std::size_t count = items.size();
        return HttpResponse::json(status::kOk,
                                  Json::object({{"todos", std::move(items)}, {"count", count}}));
    }));

    router.post("/api/todos", json_handler([&store](const HttpRequest& request) {
        const Json body = parse_json_body(request);
        check_members(body, {"title", "done"});
        const Json* title = body.find("title");
        if (title == nullptr) {
            throw ApiError(status::kBadRequest, "missing field 'title'");
        }
        const Json* done = body.find("done");
        const Todo todo =
            store.create(validated_title(*title), done != nullptr && validated_done(*done));
        HttpResponse response = HttpResponse::json(status::kCreated, to_json(todo));
        response.set_header("Location", "/api/todos/" + std::to_string(todo.id));
        return response;
    }));

    router.get("/api/todos/:id", json_handler([&store](const HttpRequest& request) {
        const std::int64_t id = todo_id(request);
        const auto todo = store.get(id);
        if (!todo) {
            throw ApiError(status::kNotFound, "no todo with id " + std::to_string(id));
        }
        return HttpResponse::json(status::kOk, to_json(*todo));
    }));

    router.patch("/api/todos/:id", json_handler([&store](const HttpRequest& request) {
        const std::int64_t id = todo_id(request);
        const Json body = parse_json_body(request);
        check_members(body, {"title", "done"});
        std::optional<std::string> title;
        std::optional<bool> done;
        if (const Json* value = body.find("title")) {
            title = validated_title(*value);
        }
        if (const Json* value = body.find("done")) {
            done = validated_done(*value);
        }
        const auto todo = store.update(id, std::move(title), done);
        if (!todo) {
            throw ApiError(status::kNotFound, "no todo with id " + std::to_string(id));
        }
        return HttpResponse::json(status::kOk, to_json(*todo));
    }));

    router.del("/api/todos/:id", json_handler([&store](const HttpRequest& request) {
        const std::int64_t id = todo_id(request);
        if (!store.remove(id)) {
            throw ApiError(status::kNotFound, "no todo with id " + std::to_string(id));
        }
        return HttpResponse(status::kNoContent);
    }));
}

} // namespace http
