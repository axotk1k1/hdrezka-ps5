/*
 * HdRezka - console LAN IP / DNS via sceNetCtl (DNS may be 127.0.0.1).
 */
#pragma once

#include <cstddef>

namespace hdrezka::net
{
// Fills dotted IPv4 into out (e.g. "192.168.1.10"). Returns false on failure.
[[nodiscard]] bool lan_ipv4(char *out, std::size_t out_size) noexcept;

// Primary DNS string from NetCtl (often "127.0.0.1" on jailbroken consoles).
[[nodiscard]] bool primary_dns(char *out, std::size_t out_size) noexcept;

// Build http://<lan>:port/ into out. Never uses 127.0.0.1 (WebKit + local DNS).
[[nodiscard]] bool ui_url(char *out, std::size_t out_size, unsigned short port) noexcept;
} // namespace hdrezka::net
