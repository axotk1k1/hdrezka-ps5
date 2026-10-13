/*
 * HdRezka - Phase 1 UI launch: system WebKit (A) or in-process WebKit (B).
 */
#pragma once

namespace hdrezka::ui
{
enum class LaunchPath
{
    none,
    variant_a_system_browser, // sceSystemServiceLaunchWebBrowser
    variant_b_inprocess,      // libSceWebKit / WebCore in-process
};

struct LaunchReport
{
    LaunchPath path = LaunchPath::none;
    int status = -1;
    const char *detail = "";
};

// Variant A: modal system browser / WebKit deeplink (not system("open")).
// url must be LAN http://x.x.x.x:34178/ — never 127.0.0.1 (DNS uses loopback).
[[nodiscard]] LaunchReport launch_system_browser(const char *url) noexcept;

// Variant B: in-process WebKit. Returns unavailable until libSceWebKit is wired.
[[nodiscard]] LaunchReport launch_inprocess_webkit(const char *url) noexcept;

// Prefer A; if A fails, attempt B.
[[nodiscard]] LaunchReport launch_ui(const char *url) noexcept;

[[nodiscard]] const char *to_string(LaunchPath path) noexcept;
} // namespace hdrezka::ui
