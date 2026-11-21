# Roadmap

Development log of the server, one step at a time.

## Milestone 1 — Networking basics
- [x] 01. Project scaffolding: CMake, README, main entry point
- [x] 02. RAII `Socket` wrapper class (create, close, move-only)
- [x] 03. TCP server: bind, listen, accept loop returning a fixed response
- [x] 04. `HttpRequest` model + request-line parser (method, path, version)
- [x] 05. Header parsing + reading the body via `Content-Length`

## Milestone 2 — Responses and routing
- [x] 06. `HttpResponse` builder (status, headers, body, serialization)
- [x] 07. Status code table with reason phrases
- [x] 08. `Router` with method + exact path matching
- [x] 09. Path parameters (`/users/:id`)
- [x] 10. Query string parsing and URL decoding

## Milestone 3 — Static files
- [x] 11. Static file handler with MIME type detection
- [ ] 12. Path traversal protection (`../` attacks)
- [ ] 13. Custom error pages (404, 405, 500)
- [ ] 14. Logger with timestamps and levels

## Milestone 4 — Concurrency and robustness
- [ ] 15. `ThreadPool` implementation
- [ ] 16. Handle connections on the thread pool
- [ ] 17. HTTP keep-alive connections
- [ ] 18. Config file loading (port, document root, threads)
- [ ] 19. Command-line arguments
- [ ] 20. Graceful shutdown on SIGINT/SIGTERM

## Milestone 5 — Quality and polish
- [ ] 21. Minimal test framework + request parser tests
- [ ] 22. Router and URL-decoding tests
- [ ] 23. JSON response helper + demo API endpoints
- [ ] 24. Demo website in `public/`
- [ ] 25. GitHub Actions CI (build + tests)
- [ ] 26. Architecture docs and final README polish
