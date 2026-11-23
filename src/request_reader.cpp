#include "request_reader.hpp"

#include <cerrno>
#include <string_view>
#include <system_error>
#include <utility>

namespace http {

namespace {

constexpr std::string_view kHeadTerminator = "\r\n\r\n";

bool is_timeout(const std::system_error& error) noexcept {
    const int code = error.code().value();
    return code == EAGAIN || code == EWOULDBLOCK;
}

} // namespace

RequestReader::RequestReader(const Socket& socket, RequestLimits limits)
    : socket_(&socket), limits_(limits) {}

bool RequestReader::fill() {
    const std::string chunk = socket_->receive();
    if (chunk.empty()) {
        return false;
    }
    buffer_ += chunk;
    return true;
}

std::optional<HttpRequest> RequestReader::next() {
    // Tolerate stray CRLFs between requests (RFC 9112, section 2.2).
    while (buffer_.compare(0, 2, "\r\n") == 0) {
        buffer_.erase(0, 2);
    }

    std::size_t head_end = std::string::npos;
    while ((head_end = buffer_.find(kHeadTerminator)) == std::string::npos) {
        if (buffer_.size() > limits_.max_head_bytes) {
            throw RequestTooLarge("request head too large");
        }
        const bool idle = buffer_.empty();
        bool received = false;
        try {
            received = fill();
        } catch (const std::system_error& error) {
            if (idle && is_timeout(error)) {
                return std::nullopt; // idle keep-alive connection timed out
            }
            throw;
        }
        if (!received) {
            if (idle) {
                return std::nullopt;
            }
            throw ParseError("connection closed before end of headers");
        }
    }

    if (head_end > limits_.max_head_bytes) {
        throw RequestTooLarge("request head too large");
    }

    HttpRequest request = parse_request_head(std::string_view(buffer_).substr(0, head_end));

    if (request.header("Transfer-Encoding")) {
        throw ParseError("chunked transfer encoding is not supported");
    }

    const std::size_t body_length = request.content_length();
    if (body_length > limits_.max_body_bytes) {
        throw RequestTooLarge("request body too large");
    }

    const std::size_t body_start = head_end + kHeadTerminator.size();
    while (buffer_.size() - body_start < body_length) {
        if (!fill()) {
            throw ParseError("connection closed before end of body");
        }
    }

    request.body = buffer_.substr(body_start, body_length);
    buffer_.erase(0, body_start + body_length); // keep pipelined bytes
    return request;
}

} // namespace http
