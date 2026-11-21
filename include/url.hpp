#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace http {

// Query parameters in key order. A multimap keeps repeated keys,
// e.g. "tag=a&tag=b".
using QueryParams = std::multimap<std::string, std::string>;

// Decodes percent-escapes ("%20" -> ' '). When plus_as_space is true, '+'
// is decoded as a space (application/x-www-form-urlencoded rules, used for
// query strings). Returns std::nullopt for malformed escapes such as "%G1"
// or a truncated "%2".
std::optional<std::string> url_decode(std::string_view text, bool plus_as_space = false);

// Percent-encodes everything except unreserved characters (RFC 3986).
std::string url_encode(std::string_view text);

// Parses "a=1&b=hello+world&flag" into {a:1, b:"hello world", flag:""}.
// Empty pairs ("a=1&&b=2") are skipped. Returns std::nullopt if any key or
// value contains a malformed escape.
std::optional<QueryParams> parse_query(std::string_view query);

} // namespace http
