# cpp-http-server

A small HTTP/1.1 web server written **from scratch** in modern C++ (C++17) —
no frameworks, just POSIX sockets and the standard library.

The goal is to understand how the web works under the hood: TCP sockets,
parsing raw HTTP requests, routing, serving static files, and handling many
clients concurrently.

## Features (planned)

- TCP socket server built on POSIX sockets (RAII wrappers)
- HTTP/1.1 request parser (request line, headers, body)
- Response builder with proper status codes
- Router with path parameters (`/users/:id`) and query strings
- Static file serving with MIME types and path-traversal protection
- Thread pool for concurrent connections, keep-alive
- Config file, logging, graceful shutdown
- Unit tests and CI

See [ROADMAP.md](ROADMAP.md) for progress.

## Build

Requirements: a C++17 compiler (GCC 9+ / Clang 10+) and CMake 3.16+.
Linux and macOS are supported (POSIX sockets).

```bash
cmake -S . -B build
cmake --build build
./build/http_server        # listens on port 8080
./build/http_server 3000   # or pick a port
```

Then open http://localhost:8080 or run `curl -i http://localhost:8080/`.

## Usage

Routes are registered with a small, fluent API:

```cpp
http::Router router;

router.get("/hello", [](const http::HttpRequest&) {
    return http::HttpResponse::text(http::status::kOk, "Hello!\n");
});

router.post("/echo", [](const http::HttpRequest& request) {
    return http::HttpResponse::text(http::status::kOk, request.body);
});

// Path parameters: "/users/42" -> param("id") == "42"
router.get("/users/:id", [](const http::HttpRequest& request) {
    return http::HttpResponse::text(http::status::kOk,
                                    "User #" + request.param("id").value_or("") + "\n");
});
```

Static routes take priority over parameterised ones, so `/users/me` can be
registered alongside `/users/:id`.

Built-in demo routes:

| Method | Path       | Description                         |
|--------|------------|-------------------------------------|
| GET    | `/`        | HTML welcome page                   |
| GET    | `/hello`   | Plain-text greeting                 |
| GET    | `/health`  | JSON health check                   |
| GET    | `/request` | Echoes the parsed request back      |
| GET    | `/users/:id` | Shows the captured `id` parameter |
| GET    | `/users/:id/posts/:post_id` | Two path parameters    |
| POST   | `/echo`    | Returns the request body unchanged  |

Unknown paths return `404`, known paths with the wrong method return `405`
with an `Allow` header, and `HEAD` is answered automatically for `GET` routes.

## Project structure

```
include/   public headers
src/       implementation
public/    static files served by the demo
```

## License

MIT
