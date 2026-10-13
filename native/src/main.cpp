/*
 * HdRezka native title:
 *   HTTP+proxy in-process on 127.0.0.1:34178
 *   Do not take VideoOut — that windows the system WebKit overlay.
 *   DNS for outbound HTTPS bypasses console 127.0.0.1 (nanoDNS + 1.1.1.1).
 */

#include "crash_log.h"
#include "demo_renderer.hpp"
#include "lan_ip.hpp"
#include "sandbox_net.hpp"
#include "service_host.h"
#include "webkit_launch.hpp"

#include <array>
#include <cstdio>
#include <cstdint>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceSystemServiceHideSplashScreen(void);

namespace
{
std::array<char, 48> line_dns{};
std::array<char, 48> line_net{};
std::array<char, 48> line_http{};
std::array<char, 64> line_www{};
std::array<char, 48> line_ui{};
std::array<char, 64> line_url{};

void set_line48(std::array<char, 48> &line, const char *text) noexcept
{
    (void)std::snprintf(line.data(), line.size(), "%s", text != nullptr ? text : "");
}

void set_line64(std::array<char, 64> &line, const char *text) noexcept
{
    (void)std::snprintf(line.data(), line.size(), "%s", text != nullptr ? text : "");
}

void draw_scene(ps5::demo::Canvas &canvas) noexcept
{
    using ps5::demo::Color;

    canvas.clear(Color::background);
    canvas.rectangle(80, 40, 1760, 1000, Color::panel);
    canvas.text(120, 80, "HDREZKA", 12, Color::white);
    canvas.text(120, 200, "HTTP READY", 5, Color::cyan);
    canvas.text(120, 300, line_dns.data(), 4, Color::cyan);
    canvas.text(120, 370, line_net.data(), 4, Color::white);
    canvas.text(120, 440, line_http.data(), 4, Color::yellow);
    canvas.text(120, 510, line_www.data(), 3, Color::white);
    canvas.text(120, 580, line_ui.data(), 4, Color::cyan);
    canvas.text(120, 650, line_url.data(), 3, Color::white);
}

[[noreturn]] void keep_alive() noexcept
{
    (void)sceSystemServiceHideSplashScreen();
    crash_log_step("keep_alive: entered loop");
    unsigned long ticks = 0;
    for (;;)
    {
        (void)sceKernelUsleep(2'000'000);
        ++ticks;
        if (ticks % 5 == 0)
            crash_log_step("keep_alive: heartbeat %lu (alive %lu sec)", ticks, ticks * 2);
    }
}
} // namespace

int main()
{
    crash_log_install();
    crash_log_step("main: starting HdRezka");

    char dns[32]{};
    if (hdrezka::net::primary_dns(dns, sizeof(dns)))
    {
        std::array<char, 48> text{};
        (void)std::snprintf(text.data(), text.size(), "DNS %s", dns);
        set_line48(line_dns, text.data());
        crash_log_step("main: primary DNS resolved: %s", dns);
    }
    else
    {
        set_line48(line_dns, "DNS UNKNOWN");
        crash_log_step("main: primary DNS unknown");
    }

    char url[80]{};
    if (!hdrezka::net::ui_url(url, sizeof(url), hdrezka::net::ui_port))
        (void)std::snprintf(url, sizeof(url), "http://127.0.0.1:%u/",
                            static_cast<unsigned>(hdrezka::net::ui_port));
    set_line64(line_url, url);
    crash_log_step("main: UI target URL: %s", url);

    const auto net = hdrezka::net::probe_loopback_ui_port();
    {
        std::array<char, 48> text{};
        (void)std::snprintf(text.data(), text.size(), "NET %s E%d/%d",
                            hdrezka::net::to_string(net.result), net.bind_errno,
                            net.connect_errno);
        set_line48(line_net, text.data());
        crash_log_step("main: net probe %s (bind_err=%d, conn_err=%d)",
                       hdrezka::net::to_string(net.result), net.bind_errno, net.connect_errno);
    }

    crash_log_step("main: starting embedded HTTP server...");
    const int http_rc = hdrezka_start_embedded_server(hdrezka::net::ui_port);
    if (http_rc == 0)
    {
        set_line48(line_http, "HTTP INPROCESS OK");
        crash_log_step("main: HTTP server started successfully");
    }
    else
    {
        std::array<char, 48> text{};
        (void)std::snprintf(text.data(), text.size(), "HTTP FAIL RC=%d", http_rc);
        set_line48(line_http, text.data());
        crash_log_step("main: HTTP server start FAILED rc=%d", http_rc);
    }
    set_line64(line_www, hdrezka_www_path());
    crash_log_step("main: www path is %s", hdrezka_www_path());

    if (http_rc == 0)
    {
        crash_log_step("main: launching UI...");
        const auto ui = hdrezka::ui::launch_ui(url);
        crash_log_step("main: UI launch result: path=%s rc=%d detail=%s",
                       hdrezka::ui::to_string(ui.path), ui.status, ui.detail);
        if (ui.status == 0)
            keep_alive();
        std::array<char, 48> text{};
        (void)std::snprintf(text.data(), text.size(), "UI %s RC=%d %s",
                            hdrezka::ui::to_string(ui.path), ui.status, ui.detail);
        set_line48(line_ui, text.data());
        crash_log_step("main: fallback to demo renderer (UI launch failed)");
        ps5::demo::run(draw_scene, "HDREZKA UI FAIL");
    }

    set_line48(line_ui, "UI SKIPPED (NO HTTP)");
    crash_log_step("main: fallback to demo renderer (no HTTP)");
    ps5::demo::run(draw_scene, "HDREZKA NO HTTP");
}
