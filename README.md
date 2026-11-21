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
./build/http_server 3000 www   # port + document root (default: public)
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

// Query strings: "/search?q=hello+world" -> query_param("q") == "hello world"
router.get("/search", [](const http::HttpRequest& request) {
    return http::HttpResponse::text(http::status::kOk,
                                    request.query_param("q").value_or("") + "\n");
});
```

Paths and query strings are percent-decoded (`%20`, and `+` in queries);
malformed escapes are rejected with `400 Bad Request`.

Static routes take priority over parameterised ones, so `/users/me` can be
registered alongside `/users/:id`.

Built-in demo routes:

| Method | Path       | Description                         |
|--------|------------|-------------------------------------|
| GET    | `/hello`   | Plain-text greeting                 |
| GET    | `/health`  | JSON health check                   |
| GET    | `/request` | Echoes the parsed request back      |
| GET    | `/search?q=...` | Reads decoded query parameters |
| GET    | `/users/:id` | Shows the captured `id` parameter |
| GET    | `/users/:id/posts/:post_id` | Two path parameters    |
| GET    | `/error`   | Throws, to demo the 500 error page  |
| POST   | `/echo`    | Returns the request body unchanged  |

## Static files

Any path that matches no route is looked up in the document root
(`public/` by default):

- `/css/style.css` serves `public/css/style.css` with `Content-Type: text/css`
  (the MIME type comes from the file extension)
- directories serve their `index.html`, so `/` serves `public/index.html`
- a directory without a trailing slash (`/docs`) is redirected to `/docs/`
- missing files return `404`, non-GET/HEAD methods return `405`
- requests can't escape the document root: `..` segments (including
  encoded ones like `%2e%2e`), hidden files such as `.env` or `.git`, and
  symlinks that resolve outside the root are answered with `403 Forbidden`

Unknown paths return `404`, known paths with the wrong method return `405`
with an `Allow` header, and `HEAD` is answered automatically for `GET` routes.

## Error pages

Errors produced by the server itself (unknown path, wrong method, forbidden
file, handler exception, malformed request) are rendered as HTML pages for
clients that send `Accept: text/html` (browsers). Other clients, like
`curl`, get a one-line plain-text body such as `404 Not Found: /nope`.

Templates live in `errors/` and are named after the status code
(`404.html`, `405.html`, `500.html`). They can use the placeholders
`{{status}}`, `{{reason}}` and `{{message}}` (HTML-escaped). Status codes
without a template get a simple built-in page. Error responses that a
handler builds itself (e.g. `/search` without `q`) are left untouched.

## Logging

Every request is written to an access log with a timestamp and level:

```text
2026-11-21 20:04:42.810 [INFO ] 127.0.0.1:49538 GET / -> 200 (501 B, 0.17 ms)
2026-11-21 20:04:42.829 [ERROR] handler error for /error: demo failure from /error
```

`DEBUG`/`INFO` lines go to stdout, `WARN`/`ERROR` to stderr. Set the level
with the `LOG_LEVEL` environment variable (`debug`, `info`, `warn`, `error`,
`off`; default `info`):

```bash
LOG_LEVEL=debug ./build/http_server
```

In code, use the thread-safe helpers from `logger.hpp`:
`http::log_info("served ", path, " in ", ms, " ms");`

## Project structure

```
include/   public headers
src/       implementation
public/    static files served by the demo
errors/    HTML templates for error pages
```

## License

MIT
