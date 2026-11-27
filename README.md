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
- Config file, logging, graceful shutdown on SIGINT/SIGTERM
- Unit tests and CI

See [ROADMAP.md](ROADMAP.md) for progress.

## Build

Requirements: a C++17 compiler (GCC 9+ / Clang 10+) and CMake 3.16+.
Linux and macOS are supported (POSIX sockets).

```bash
cmake -S . -B build
cmake --build build
./build/http_server                  # listens on port 8080
./build/http_server --port 3000      # or pick a port
./build/http_server -p 3000 -r www   # port + document root (default: public)
./build/http_server --help           # all options
```

Settings are read from `server.conf` when it exists (see
[Configuration](#configuration)) and can be overridden on the
[command line](#command-line).

Then open http://localhost:8080 or run `curl -i http://localhost:8080/`.

## Demo website

`public/` holds a small website that the server ships with, built only from
static HTML, CSS and plain JavaScript that talk to the server's own routes:

| Page                | What it shows                                                     |
|---------------------|-------------------------------------------------------------------|
| `/`                 | Live uptime, to-do count and `/health` round-trip time, plus the endpoint list read from `GET /api` |
| `/todos/`           | A to-do app on top of the [JSON API](#json-api): add, tick, filter and delete items |
| `/playground/`      | Send any method, path and body to the server and inspect the raw status, headers and body |

```text
public/
├── index.html            home page
├── favicon.svg
├── css/style.css         shared styles (light and dark mode)
├── js/common.js          JSON fetch helper + server status in the footer
├── js/home.js
├── js/todos.js
├── js/playground.js
├── todos/index.html      served for /todos/ (and /todos redirects there)
└── playground/index.html
```

The site exercises most of the server at once: MIME types for `.html`,
`.css`, `.js` and `.svg`, directory index pages and trailing-slash
redirects, keep-alive (the page and its assets share connections), JSON
requests and error responses. Point `--root` at another directory to serve
your own site instead.

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
| *      | `/api/...` | JSON to-do API, see [JSON API](#json-api) |

## JSON API

`json.hpp` provides `http::Json`, a small JSON value type (null, boolean,
number, string, array, object) with a parser and a serializer, and
`HttpResponse` has two helpers built on it:

```cpp
router.get("/api/user", [](const http::HttpRequest&) {
    http::Json user = http::Json::object({{"id", 42}, {"name", "Ada"}});
    user["tags"] = http::Json::array({"math", "engines"});
    return http::HttpResponse::json(http::status::kOk, user);  // {"id":42,...}
});

http::HttpResponse::json_error(404, "no todo with id 7");
// {"error":{"status":404,"message":"no todo with id 7"}}

const http::Json body = http::Json::parse(request.body);       // throws JsonError
body.at("title").as_string();
```

Objects keep insertion order, integers stay exact (`int64`), `dump(2)`
pretty-prints, and parse errors name the byte offset
(`invalid JSON at offset 9: unexpected end of input`). Nesting is limited
to 64 levels.

The demo server registers an in-memory to-do API (`demo_api.hpp`,
`http::register_demo_api(router, store)`), shared safely by all worker
threads:

| Method | Path             | Description                                   |
|--------|------------------|-----------------------------------------------|
| GET    | `/api`           | Index of the endpoints (pretty-printed)       |
| GET    | `/api/status`    | Server name, version, uptime, number of todos |
| GET    | `/api/todos`     | All todos; `?done=true` or `?done=false` filters |
| POST   | `/api/todos`     | Create from `{"title": "...", "done": false}` → `201` + `Location` |
| GET    | `/api/todos/:id` | One todo                                      |
| PATCH  | `/api/todos/:id` | Update `title` and/or `done`                  |
| DELETE | `/api/todos/:id` | Delete → `204 No Content`                     |

```bash
curl -s localhost:8080/api/todos
curl -si -X POST localhost:8080/api/todos -H 'Content-Type: application/json' -d '{"title":"Buy milk"}'
curl -s -X PATCH localhost:8080/api/todos/3 -H 'Content-Type: application/json' -d '{"done":true}'
curl -s -X DELETE localhost:8080/api/todos/3
```

Request bodies must be sent with `Content-Type: application/json`
(otherwise `415`). Malformed JSON, wrong field types, empty titles and
unknown fields get `400`, missing ids `404`, always as a JSON error body.
The store starts with two sample items and lives only as long as the
process.

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

## Configuration

At startup the server reads `server.conf` from the current directory, or
the file named by the `SERVER_CONFIG` environment variable (which then must
exist). The file uses simple `key = value` lines; `#` and `;` start a
comment line, values may be quoted, and every setting is optional:

```ini
host = 0.0.0.0
port = 8080
document_root = public        # relative paths start from the working directory
error_pages = errors
threads = 0                   # 0 = one worker per CPU core
client_timeout_ms = 10000
keep_alive = true             # true/false, yes/no, on/off, 1/0
keep_alive_timeout_ms = 5000
keep_alive_max_requests = 100
log_level = info
```

Settings are applied in this order, later ones winning: built-in defaults,
the config file, the `LOG_LEVEL` environment variable, then
[command-line options](#command-line). `--config FILE` picks the file
explicitly and takes priority over `SERVER_CONFIG`.

```bash
SERVER_CONFIG=/etc/cpp-http-server.conf ./build/http_server
./build/http_server --config staging.conf
LOG_LEVEL=debug ./build/http_server   # also prints the effective configuration
```

Mistakes stop the server with the file and line of the problem, e.g.
`fatal: server.conf:3: invalid port '70000' (expected a number from 0 to 65535)`.
Unknown keys are rejected too, so a typo never goes unnoticed. In code,
use `http::load_config(path)` or `http::parse_config(text)` from
`config.hpp`, which return an `http::ServerConfig`.

## Command line

```text
Usage: http_server [options] [port] [document_root]

  -c, --config FILE         read settings from FILE (default: ./server.conf)
  -H, --host ADDR           address to bind to (default: 0.0.0.0)
  -p, --port PORT           port to listen on, 0 = any free port (default: 8080)
  -r, --root DIR            document root for static files (default: public)
  -e, --error-pages DIR     directory with error page templates
  -t, --threads N           worker threads, 0 = one per CPU core
  -l, --log-level LEVEL     debug, info, warn, error or off
      --no-keep-alive       close every connection after one response
      --print-config        print the effective configuration and exit
  -h, --help                show this help and exit
  -V, --version             show the version and exit
```

Values can be written as `--port 3000`, `--port=3000`, `-p 3000` or
`-p3000`, and `--` ends option parsing. Only the options you pass override
the config file. The older positional form `http_server 3000 www` still
works, but giving the same setting both ways is an error.

`--print-config` is handy to check what a combination of config file,
environment and options resolves to:

```bash
./build/http_server -p 9090 -t 4 --no-keep-alive --print-config
```

Invalid arguments print a short message and exit with status `2`:

```text
http_server: invalid value '70000' for --port (expected a number from 0 to 65535)
Try 'http_server --help' for more information.
```

In code, `http::parse_cli(argc, argv)` from `cli.hpp` returns an
`http::CliOptions`, and `http::apply_cli(options, config)` layers it on top
of a `ServerConfig`.

## Logging

Every request is written to an access log with a timestamp and level:

```text
2026-11-21 20:04:42.810 [INFO ] 127.0.0.1:49538 GET / -> 200 (501 B, 0.17 ms)
2026-11-21 20:04:42.829 [ERROR] handler error for /error: demo failure from /error
```

`DEBUG`/`INFO` lines go to stdout, `WARN`/`ERROR` to stderr. Set the level
with `log_level` in the config file, the `LOG_LEVEL` environment variable
or `--log-level` (`debug`, `info`, `warn`, `error`, `off`; default `info`):

```bash
LOG_LEVEL=debug ./build/http_server
./build/http_server --log-level warn
```

In code, use the thread-safe helpers from `logger.hpp`:
`http::log_info("served ", path, " in ", ms, " ms");`

## Thread pool

`thread_pool.hpp` provides `http::ThreadPool`, a fixed number of worker
threads sharing a FIFO task queue:

```cpp
http::ThreadPool pool(4);                    // 0 = one worker per CPU core
pool.post([] { /* fire and forget */ });
std::future<int> sum = pool.submit([](int a, int b) { return a + b; }, 2, 3);
sum.get();                                   // 5, exceptions are rethrown here
pool.shutdown();                             // drain the queue, join workers
```

Exceptions escaping a `post()`ed task are logged instead of killing the
worker. After `shutdown()` (also run by the destructor) new tasks are
rejected with `ThreadPoolStopped`.

## Concurrency

The server accepts connections on the main thread and hands each one to a
`ThreadPool` with one worker per CPU core, so a slow or idle client only
occupies a single worker while everyone else keeps being served:

```cpp
http::ThreadPool pool;
server.serve([&](http::Socket& client) { http::serve_connection(client, handle_request); }, pool);
```

Every accepted socket gets a receive/send timeout (10 seconds by default,
`TcpServer::set_client_timeout()`), after which a stalled client is dropped
and logged as `timed out, closing connection`. The router, static file
handler and error pages are read-only after startup, so workers share them
without locking. `TcpServer::serve(handler)` without a pool still handles
clients one at a time on the calling thread.

## Keep-alive

Connections are persistent by default, so a browser loading a page and its
stylesheet reuses a single TCP connection. `http::serve_connection()` keeps
reading requests from the same socket until:

- the client sends `Connection: close` (HTTP/1.0 clients must instead opt in
  with `Connection: keep-alive`),
- the connection stays idle longer than the keep-alive timeout (5 seconds),
- 100 requests have been served on it, or
- a request is malformed (`400`/`413` responses always close).

Every response says which way it went:

```text
Connection: keep-alive
Keep-Alive: timeout=5, max=99
```

Pipelined requests (several sent back to back without waiting) are answered
in order, because `RequestReader` keeps any bytes past the current request
for the next one. Limits are configurable through `http::KeepAliveOptions`
(`enabled`, `idle_timeout`, `max_requests`). Try it with:

```bash
curl -v http://localhost:8080/hello http://localhost:8080/health   # "Re-using existing connection"
```

## Graceful shutdown

`Ctrl+C` (SIGINT) or `kill` / `docker stop` / systemd (SIGTERM) shut the
server down cleanly instead of cutting connections off mid-response:

1. the listening socket is closed, so new clients are refused right away;
2. idle keep-alive connections are closed immediately;
3. requests that are already being received or handled are finished and
   answered with `Connection: close`;
4. the thread pool drains and the process exits with status `0`.

```text
2026-11-25 09:45:11.809 [INFO ] received SIGINT, shutting down gracefully (2 active connection(s), send it again to force quit)
2026-11-25 09:45:11.809 [INFO ] waiting for 1 connection(s) to finish
2026-11-25 09:45:12.110 [INFO ] 127.0.0.1:49246 POST /echo -> 200 (5 B, 0.04 ms)
2026-11-25 09:45:12.110 [INFO ] server stopped (drained in 300 ms)
```

Sending the signal a second time exits immediately with status `128 + signal`
(`130` for SIGINT). A stalled client can delay the drain by at most the
client timeout.

Under the hood, `http::SignalWatcher` (`signal_watcher.hpp`) blocks the
signals in every thread and receives them with `sigwait()` on a dedicated
thread, so the callback is ordinary code that may log and lock.
`TcpServer::stop()` requests an `http::StopSignal` (`stop_signal.hpp`), a
flag backed by a socket pair that the accept loop and idle connections
`poll()` on together with their own socket. Pass it to
`serve_connection(client, handler, keep_alive, limits, &server.stop_signal())`
to make connections shutdown-aware.

## Tests

The unit tests use a small built-in test framework (`tests/test_framework.hpp`),
so they need nothing beyond the compiler. They are built together with the
server and can be run directly or through CTest:

```bash
cmake -S . -B build && cmake --build build
./build/unit_tests                 # run every test
./build/unit_tests RequestReader   # only tests whose name contains the text
./build/unit_tests --list          # list test names
ctest --test-dir build             # same tests via CTest
```

```text
[  OK  ] parses a simple request line
[  OK  ] rejects unsupported HTTP versions
...
85 test(s), 85 passed, 0 failed (2 ms)
```

The suite is split by component:

| File                          | Covers                                                        |
|-------------------------------|---------------------------------------------------------------|
| `tests/http_request_test.cpp` | request line and header parsing, `RequestReader` limits       |
| `tests/router_test.cpp`       | exact and parameterised routes, 404/405, `Allow`, HEAD, fallback |
| `tests/url_test.cpp`          | `url_decode`, `url_encode`, `parse_query`, query parameters   |
| `tests/json_test.cpp`         | `Json` building, serializing, parsing errors, response helpers |
| `tests/demo_api_test.cpp`     | `TodoStore` and the `/api` endpoints (CRUD, validation, 404s) |

Writing a test:

```cpp
#include "test_framework.hpp"

TEST_CASE("splits the query string from the path") {
    const auto request = http::parse_request_line("GET /search?q=a+b HTTP/1.1");
    CHECK_EQ(request.path, "/search");
    CHECK_EQ(request.query_param("q"), "a b");
    CHECK_THROWS_AS(http::parse_request_line("GET / HTTP/2.0"), http::ParseError);
}
```

`CHECK`, `CHECK_FALSE`, `CHECK_EQ`, `CHECK_THROWS_AS` and `CHECK_NOTHROW`
record a failure and keep going; `REQUIRE` and `REQUIRE_EQ` stop the test.
Failed `CHECK_EQ`s print both values. The runner exits with `1` if any test
fails. Any `tests/*.cpp` file is picked up automatically; pass
`-DHTTP_SERVER_BUILD_TESTS=OFF` to CMake to skip building them.

The server code is compiled once into the `http_core` static library, which
both `http_server` and `unit_tests` link against.

## Project structure

```
include/   public headers
src/       implementation
tests/     unit tests and the test framework
public/    static files served by the demo
errors/    HTML templates for error pages
server.conf  default configuration
```

## License

MIT
