/*
 * HdRezka - stream payload ELF to the local elfldr on 127.0.0.1:9021.
 */
#pragma once

namespace hdrezka::payload
{
enum class LoadResult
{
    ok,
    missing_file,
    loader_unreachable,
    stream_failed,
};

[[nodiscard]] LoadResult load_elf(const char *path = "/app0/rezka-ps5.elf") noexcept;
[[nodiscard]] const char *to_string(LoadResult result) noexcept;
} // namespace hdrezka::payload
