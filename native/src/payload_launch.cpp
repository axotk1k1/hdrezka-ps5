/*
 * HdRezka - push rezka-ps5.elf to elfldr (:9021) so HTTP :34178 can serve UI.
 */
#include "payload_launch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

extern "C"
{
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetSetsockopt(int socket, int level, int option, const void *value, unsigned length);
    int sceNetConnect(int socket, const void *address, unsigned length);
    int sceNetSend(int socket, const void *data, std::size_t length, int flags);
    int sceNetSocketClose(int socket);
}

namespace hdrezka::payload
{
namespace
{
struct NetSockaddrIn
{
    unsigned char len;
    unsigned char family;
    std::uint16_t port;
    std::uint32_t addr;
    std::uint32_t zero0;
    std::uint64_t zero1;
};

bool send_all(int socket, const void *data, std::size_t size) noexcept
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::size_t sent = 0;
    while (sent < size)
    {
        const int chunk =
            sceNetSend(socket, bytes + sent, size - sent, 0);
        if (chunk <= 0)
            return false;
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}
} // namespace

LoadResult load_elf(const char *path) noexcept
{
    if (path == nullptr || path[0] == '\0')
        return LoadResult::missing_file;

    const int file = ::open(path, O_RDONLY);
    if (file < 0)
        return LoadResult::missing_file;

    const int socket = sceNetSocket("hdrezka_payload", 2, 1, 6);
    if (socket < 0)
    {
        (void)::close(file);
        return LoadResult::loader_unreachable;
    }

    constexpr int socket_level = 0xffff;
    constexpr int timeout_us = 5'000'000;
    bool configured = true;
    for (const int option : {0x1105, 0x1106, 0x1109})
        configured &=
            sceNetSetsockopt(socket, socket_level, option, &timeout_us, sizeof(timeout_us)) >= 0;

    constexpr std::uint16_t port = 9021;
    const NetSockaddrIn endpoint{sizeof(NetSockaddrIn),
                                 2,
                                 static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                                 0x0100007f,
                                 0,
                                 0};

    LoadResult result = LoadResult::loader_unreachable;
    if (configured && sceNetConnect(socket, &endpoint, sizeof(endpoint)) >= 0)
    {
        std::array<std::uint8_t, 4096> buffer{};
        bool streamed = true;
        for (;;)
        {
            const auto count = ::read(file, buffer.data(), buffer.size());
            if (count == 0)
                break;
            if (count < 0 ||
                !send_all(socket, buffer.data(), static_cast<std::size_t>(count)))
            {
                streamed = false;
                break;
            }
        }
        result = streamed ? LoadResult::ok : LoadResult::stream_failed;
    }

    (void)sceNetSocketClose(socket);
    (void)::close(file);
    return result;
}

const char *to_string(LoadResult result) noexcept
{
    switch (result)
    {
    case LoadResult::ok:
        return "PAYLOAD LOADED :9021";
    case LoadResult::missing_file:
        return "PAYLOAD MISSING";
    case LoadResult::loader_unreachable:
        return "ELFLDR :9021 DOWN";
    case LoadResult::stream_failed:
        return "PAYLOAD STREAM FAIL";
    }
    return "UNKNOWN";
}
} // namespace hdrezka::payload
