#include "request_reader.hpp"

#include <string>

namespace http {

namespace {
constexpr std::string_view kHeadTerminator = "\r\n\r\n";
}

std::optional<HttpRequest> read_request(const Socket& socket, const RequestLimits& limits) {
    std::string buffer;
    std::size_t head_end = std::string::npos;

    while ((head_end = buffer.find(kHeadTerminator)) == std::string::npos) {
        if (buffer.size() > limits.max_head_bytes) {
            throw RequestTooLarge("request head too large");
        }
        const std::string chunk = socket.receive();
        if (chunk.empty()) {
            if (buffer.empty()) {
                return std::nullopt;
            }
            throw ParseError("connection closed before end of headers");
        }
        buffer += chunk;
    }

    if (head_end > limits.max_head_bytes) {
        throw RequestTooLarge("request head too large");
    }

    HttpRequest request = parse_request_head(std::string_view(buffer).substr(0, head_end));

    if (request.header("Transfer-Encoding")) {
        throw ParseError("chunked transfer encoding is not supported");
    }

    const std::size_t body_length = request.content_length();
    if (body_length > limits.max_body_bytes) {
        throw RequestTooLarge("request body too large");
    }

    std::string body = buffer.substr(head_end + kHeadTerminator.size());
    while (body.size() < body_length) {
        const std::string chunk = socket.receive();
        if (chunk.empty()) {
            throw ParseError("connection closed before end of body");
        }
        body += chunk;
    }
    body.resize(body_length); // ignore pipelined bytes for now
    request.body = std::move(body);
    return request;
}

} // namespace http
