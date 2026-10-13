/*
 * HdRezka - probe bind/listen/connect on 127.0.0.1 inside the app sandbox.
 */
#include "sandbox_net.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hdrezka::net
{
namespace
{
sockaddr_in loopback_addr(unsigned short port) noexcept
{
    sockaddr_in address{};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

bool try_connect(unsigned short port, int &out_errno) noexcept
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        out_errno = errno;
        return false;
    }
    const sockaddr_in address = loopback_addr(port);
    errno = 0;
    const int rc = ::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    out_errno = errno;
    (void)::close(fd);
    // Connection refused means the stack accepted the address; nothing listens yet.
    return rc == 0 || out_errno == ECONNREFUSED;
}
} // namespace

ProbeReport probe_loopback_ui_port() noexcept
{
    ProbeReport report{};

    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        report.result = ProbeResult::create_failed;
        report.bind_errno = errno;
        return report;
    }

    int reuse = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    const sockaddr_in address = loopback_addr(ui_port);
    errno = 0;
    if (::bind(listen_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        report.bind_errno = errno;
        (void)::close(listen_fd);
        if (report.bind_errno == EADDRINUSE)
        {
            if (try_connect(ui_port, report.connect_errno))
            {
                report.result = ProbeResult::ok_connect_only;
                return report;
            }
            report.result = ProbeResult::connect_denied;
            return report;
        }
        report.result = ProbeResult::bind_denied;
        (void)try_connect(ui_port, report.connect_errno);
        return report;
    }

    if (::listen(listen_fd, 1) != 0)
    {
        report.bind_errno = errno;
        (void)::close(listen_fd);
        report.result = ProbeResult::bind_denied;
        return report;
    }

    const bool connected = try_connect(ui_port, report.connect_errno);
    (void)::close(listen_fd);

    if (!connected)
    {
        report.result = ProbeResult::connect_denied;
        return report;
    }

    report.result = ProbeResult::ok_listen_and_connect;
    return report;
}

const char *to_string(ProbeResult result) noexcept
{
    switch (result)
    {
    case ProbeResult::ok_listen_and_connect:
        return "LISTEN+CONNECT OK";
    case ProbeResult::ok_connect_only:
        return "CONNECT OK (PORT BUSY)";
    case ProbeResult::bind_denied:
        return "BIND DENIED (SANDBOX?)";
    case ProbeResult::connect_denied:
        return "CONNECT DENIED";
    case ProbeResult::create_failed:
        return "SOCKET CREATE FAILED";
    }
    return "UNKNOWN";
}

bool sockets_usable(ProbeResult result) noexcept
{
    return result == ProbeResult::ok_listen_and_connect ||
           result == ProbeResult::ok_connect_only;
}
} // namespace hdrezka::net
