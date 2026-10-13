/*
 * HdRezka - sandbox loopback probe for ShadowMount native titles.
 */
#pragma once

namespace hdrezka::net
{
inline constexpr unsigned short ui_port = 34178;

enum class ProbeResult
{
    ok_listen_and_connect,
    ok_connect_only, // port already served (payload already up)
    bind_denied,
    connect_denied,
    create_failed,
};

struct ProbeReport
{
    ProbeResult result = ProbeResult::create_failed;
    int bind_errno = 0;
    int connect_errno = 0;
};

// Can this sandboxed title use 127.0.0.1:ui_port (listen and/or connect)?
[[nodiscard]] ProbeReport probe_loopback_ui_port() noexcept;

[[nodiscard]] const char *to_string(ProbeResult result) noexcept;
[[nodiscard]] bool sockets_usable(ProbeResult result) noexcept;
} // namespace hdrezka::net
