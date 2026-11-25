#include "connection.hpp"

#include <optional>
#include <string>
#include <utility>

#include "logger.hpp"
#include "status.hpp"

namespace http {

namespace {

void send_response(Socket& client, HttpResponse response, bool keep_open,
                   const KeepAliveOptions& keep_alive, std::size_t remaining,
                   bool head_only = false) {
    if (keep_open) {
        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(keep_alive.idle_timeout).count();
        response.set_header("Connection", "keep-alive");
        response.set_header("Keep-Alive", "timeout=" + std::to_string(seconds) +
                                              ", max=" + std::to_string(remaining));
    } else {
        response.set_header("Connection", "close");
    }
    client.send_all(response.serialize(head_only));
}

} // namespace

std::size_t serve_connection(Socket& client, const RequestHandler& handler,
                             const KeepAliveOptions& keep_alive, const RequestLimits& limits,
                             const StopSignal* stop) {
    RequestReader reader(client, limits);
    const std::size_t max_requests =
        keep_alive.enabled && keep_alive.max_requests > 0 ? keep_alive.max_requests : 1;
    std::size_t served = 0;

    while (served < max_requests) {
        // Wait for the next request in a way a shutdown can interrupt. Bytes
        // of a pipelined request that already arrived are served regardless.
        if (stop != nullptr && !reader.has_buffered_data()) {
            const WaitResult wait = wait_readable(client, client.timeout(), stop);
            if (wait == WaitResult::Stopped) {
                log_debug(client.peer_address(), " idle, closing for shutdown");
                break;
            }
            if (wait == WaitResult::Timeout) {
                break; // idle for longer than the timeout
            }
        }

        std::optional<HttpRequest> request;
        try {
            request = reader.next();
        } catch (const RequestTooLarge& error) {
            log_warn(client.peer_address(), " request too large: ", error.what());
            send_response(client, HttpResponse::error(status::kPayloadTooLarge, error.what()),
                          false, keep_alive, 0);
            break;
        } catch (const ParseError& error) {
            log_warn(client.peer_address(), " bad request: ", error.what());
            send_response(client, HttpResponse::error(status::kBadRequest, error.what()), false,
                          keep_alive, 0);
            break;
        }
        if (!request) {
            break; // client closed the connection or stayed idle too long
        }

        ++served;
        HttpResponse response = handler(*request);
        // Checked after the handler ran, so a shutdown that started meanwhile
        // already shows up as "Connection: close" on this response.
        const bool stopping = stop != nullptr && stop->requested();
        const bool keep_open =
            keep_alive.enabled && served < max_requests && request->keep_alive() && !stopping;
        send_response(client, std::move(response), keep_open, keep_alive, max_requests - served,
                      request->method == Method::Head);
        if (!keep_open) {
            break;
        }

        // The first request used the server's client timeout; between
        // requests only the (usually shorter) keep-alive idle time applies,
        // so idle connections don't hold pool workers for long.
        if (served == 1) {
            client.set_timeout(keep_alive.idle_timeout);
        }
    }
    log_debug(client.peer_address(), " closed after ", served, " request(s)");
    return served;
}

} // namespace http
