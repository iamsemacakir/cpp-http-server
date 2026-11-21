#include <iostream>

#include "version.hpp"

int main() {
    std::cout << http::kServerName << " v" << http::kServerVersion << '\n';
    std::cout << "A tiny HTTP/1.1 server written from scratch in C++17.\n";
    std::cout << "Networking is coming soon - see ROADMAP.md.\n";
    return 0;
}
