/*
 * HdRezka - UI URL like websrv: always http://127.0.0.1:<port>/
 * (HTTP runs in-process on the console; LAN IP is only for diagnostics.)
 */
#include "lan_ip.hpp"

#include <cstdio>
#include <cstring>

extern "C"
{
    int sceNetCtlInit(void);
    int sceNetCtlTerm(void);
    int sceNetCtlGetInfo(int code, void *info);
}

namespace hdrezka::net
{
namespace
{
constexpr int kInfoIpAddress = 14;
constexpr int kInfoPrimaryDns = 17;
constexpr std::size_t kIpv4StrLen = 16;

bool get_info_string(int code, char *out, std::size_t out_size, bool allow_loopback) noexcept
{
    if (out == nullptr || out_size < 2)
        return false;
    out[0] = '\0';

    alignas(8) char info[256]{};
    (void)sceNetCtlInit();
    if (sceNetCtlGetInfo(code, info) != 0)
    {
        (void)sceNetCtlTerm();
        return false;
    }
    (void)sceNetCtlTerm();

    info[kIpv4StrLen - 1] = '\0';
    if (info[0] == '\0')
        return false;
    if (!allow_loopback &&
        (std::strcmp(info, "127.0.0.1") == 0 || std::strcmp(info, "0.0.0.0") == 0))
        return false;

    (void)std::snprintf(out, out_size, "%s", info);
    return true;
}
} // namespace

bool lan_ipv4(char *out, std::size_t out_size) noexcept
{
    return get_info_string(kInfoIpAddress, out, out_size, false);
}

bool primary_dns(char *out, std::size_t out_size) noexcept
{
    return get_info_string(kInfoPrimaryDns, out, out_size, true);
}

bool ui_url(char *out, std::size_t out_size, unsigned short port) noexcept
{
    // Same pattern as websrv FAKE00000 deeplinkUri: http://127.0.0.1:8080
    const int n = std::snprintf(out, out_size, "http://127.0.0.1:%u/",
                                static_cast<unsigned>(port));
    return n > 0 && static_cast<std::size_t>(n) < out_size;
}
} // namespace hdrezka::net
