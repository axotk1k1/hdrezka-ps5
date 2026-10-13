#pragma once

#include <string>
#include <vector>

namespace qr
{

// A QR code, byte mode, error correction L, versions 1 to 10 (up to 271 bytes).
// The modules are row-major: dark[y * size + x] is nonzero for a dark module.
struct Code
{
    int size = 0;
    std::vector<unsigned char> dark;

    bool at(int x, int y) const { return dark[static_cast<std::size_t>(y) * size + x] != 0; }
};

// Returns false when the text does not fit in version 10.
bool encode(const std::string &text, Code &out);

} // namespace qr
