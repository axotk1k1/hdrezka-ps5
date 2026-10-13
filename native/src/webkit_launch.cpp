/*
 * HdRezka - WebKit UI launch.
 * Do not keep a VideoOut game framebuffer: that forces the modal browser into a window.
 */
#include "webkit_launch.hpp"
#include "crash_log.h"

#include <cstdint>

extern "C"
{
    int sceUserServiceInitialize(const void *parameters);
    int sceSystemServiceHideSplashScreen(void);
    int sceSystemServiceLaunchWebApp(const char *uri);
    int sceSystemServiceLaunchWebBrowser(const char *uri, void *reserved);
}

namespace
{
template <typename Fn>
uintptr_t get_symbol_address(Fn *symbol) noexcept
{
    uintptr_t addr = 0;
    asm volatile("movq %1, %0" : "=r"(addr) : "r"(reinterpret_cast<uintptr_t>(symbol)));
    return addr;
}
} // namespace

namespace hdrezka::ui
{
LaunchReport launch_system_browser(const char *url) noexcept
{
    LaunchReport report{};
    report.path = LaunchPath::variant_a_system_browser;
    if (url == nullptr || url[0] == '\0')
    {
        report.status = -1;
        report.detail = "EMPTY URL";
        return report;
    }

    const uintptr_t user_init_addr = get_symbol_address(&sceUserServiceInitialize);
    if (user_init_addr != 0)
    {
        crash_log_step("launch_system_browser: calling sceUserServiceInitialize (0x%lx)...", (unsigned long)user_init_addr);
        auto fn = reinterpret_cast<int (*)(const void *)>(user_init_addr);
        int user_rc = fn(nullptr);
        crash_log_step("launch_system_browser: sceUserServiceInitialize rc=%d", user_rc);
    }
    else
    {
        crash_log_step("launch_system_browser: sceUserServiceInitialize is NULL (unresolved)");
    }

    const uintptr_t hide_splash_addr = get_symbol_address(&sceSystemServiceHideSplashScreen);
    if (hide_splash_addr != 0)
    {
        crash_log_step("launch_system_browser: calling sceSystemServiceHideSplashScreen...");
        auto fn = reinterpret_cast<int (*)(void)>(hide_splash_addr);
        (void)fn();
    }

    const uintptr_t launch_webapp_addr = get_symbol_address(&sceSystemServiceLaunchWebApp);
    if (launch_webapp_addr != 0)
    {
        crash_log_step("launch_system_browser: calling sceSystemServiceLaunchWebApp (0x%lx) with %s...",
                       (unsigned long)launch_webapp_addr, url);
        auto fn = reinterpret_cast<int (*)(const char *)>(launch_webapp_addr);
        report.status = fn(url);
        crash_log_step("launch_system_browser: sceSystemServiceLaunchWebApp returned rc=%d", report.status);
        if (report.status == 0)
        {
            report.detail = "WEBAPP OK";
            return report;
        }
    }
    else
    {
        crash_log_step("launch_system_browser: sceSystemServiceLaunchWebApp is NULL (not present in firmware)");
    }

    const uintptr_t launch_browser_addr = get_symbol_address(&sceSystemServiceLaunchWebBrowser);
    if (launch_browser_addr != 0)
    {
        crash_log_step("launch_system_browser: calling sceSystemServiceLaunchWebBrowser (0x%lx) with %s...",
                       (unsigned long)launch_browser_addr, url);
        auto fn = reinterpret_cast<int (*)(const char *, void *)>(launch_browser_addr);
        report.status = fn(url, nullptr);
        crash_log_step("launch_system_browser: sceSystemServiceLaunchWebBrowser returned rc=%d", report.status);
        if (report.status == 0)
        {
            report.detail = "WEBBROWSER OK";
            return report;
        }
    }
    else
    {
        crash_log_step("launch_system_browser: sceSystemServiceLaunchWebBrowser is NULL (not present in firmware)");
    }

    report.status = -1;
    report.detail = "SYS WEBKIT NOT RESOLVED";
    return report;
}

LaunchReport launch_inprocess_webkit(const char * /*url*/) noexcept
{
    LaunchReport report{};
    report.path = LaunchPath::variant_b_inprocess;
    report.status = -1;
    report.detail = "INPROCESS WEBKIT NOT LINKED";
    return report;
}

LaunchReport launch_ui(const char *url) noexcept
{
    LaunchReport report = launch_system_browser(url);
    if (report.status == 0)
        return report;
    LaunchReport fallback = launch_inprocess_webkit(url);
    if (fallback.status == 0)
        return fallback;
    report.detail = "A FAILED; B UNAVAILABLE";
    return report;
}

const char *to_string(LaunchPath path) noexcept
{
    switch (path)
    {
    case LaunchPath::none:
        return "NONE";
    case LaunchPath::variant_a_system_browser:
        return "A SYSTEM WEBKIT";
    case LaunchPath::variant_b_inprocess:
        return "B INPROCESS WEBKIT";
    }
    return "UNKNOWN";
}
} // namespace hdrezka::ui
