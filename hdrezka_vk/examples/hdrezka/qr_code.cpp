// QR code encoder: a C++ port of the encoder in qrcode-terminal (MIT License,
// Copyright (c) 2009 Kazuhiko Arase, qrcode.js, and the qrcode-terminal
// authors), for the one case the title needs: byte mode, error correction L,
// versions 1 to 10. The steps and their order are the original's, so the same
// text gives the same modules.

#include "qr_code.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace qr
{
namespace
{

constexpr int kModeByte = 4; // MODE_8BIT_BYTE
constexpr int kLevelL = 1;   // error correction L in the format bits
constexpr int kMaxVersion = 10;
constexpr int kPad0 = 0xEC;
constexpr int kPad1 = 0x11;

// GF(256) with the generator x^8 + x^4 + x^3 + x^2 + 1, for Reed-Solomon.
struct Galois
{
    int exp[256] = {};
    int log[256] = {};

    Galois()
    {
        for (int i = 0; i < 8; ++i)
            exp[i] = 1 << i;
        for (int i = 8; i < 256; ++i)
            exp[i] = exp[i - 4] ^ exp[i - 5] ^ exp[i - 6] ^ exp[i - 8];
        for (int i = 0; i < 255; ++i)
            log[exp[i]] = i;
    }

    int gexp(int n) const
    {
        while (n < 0)
            n += 255;
        while (n >= 256)
            n -= 255;
        return exp[n];
    }

    int glog(int n) const { return log[n]; } // n >= 1
};

const Galois &gf()
{
    static const Galois field;
    return field;
}

// A polynomial over GF(256), highest power first, leading zeros dropped.
using Poly = std::vector<int>;

// The original's QRPolynomial(num, shift): leading zeros dropped, then
// `shift` zeros appended.
Poly make_poly(const std::vector<int> &num, int shift)
{
    std::size_t offset = 0;
    while (offset < num.size() && num[offset] == 0)
        ++offset;
    Poly out(num.begin() + static_cast<std::ptrdiff_t>(offset), num.end());
    out.insert(out.end(), static_cast<std::size_t>(shift), 0);
    return out;
}

Poly multiply(const Poly &a, const Poly &b)
{
    const Galois &g = gf();
    Poly num(a.size() + b.size() - 1, 0);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            if (a[i] != 0 && b[j] != 0)
                num[i + j] ^= g.gexp(g.glog(a[i]) + g.glog(b[j]));
    return make_poly(num, 0);
}

// The remainder of a divided by b.
Poly remainder(Poly a, const Poly &b)
{
    const Galois &g = gf();
    while (static_cast<int>(a.size()) - static_cast<int>(b.size()) >= 0)
    {
        const int ratio = g.glog(a[0]) - g.glog(b[0]);
        Poly num = a;
        for (std::size_t x = 0; x < b.size(); ++x)
            num[x] ^= g.gexp(g.glog(b[x]) + ratio);
        a = make_poly(num, 0);
    }
    return a;
}

// The Reed-Solomon generator with `count` error correction codewords.
Poly generator(int count)
{
    const Galois &g = gf();
    Poly a{1};
    for (int i = 0; i < count; ++i)
        a = multiply(a, Poly{1, g.gexp(i)});
    return a;
}

// Codewords per block for error correction L, versions 1 to 10 (qrcode-terminal's
// RS_BLOCK_TABLE). The first group holds count1 blocks of total1 codewords with
// data1 of them data; the second group, when count2 is not zero, does the same.
struct BlockGroups
{
    int count1, total1, data1, count2, total2, data2;
};

constexpr BlockGroups kBlocksL[kMaxVersion] = {
    {1, 26, 19, 0, 0, 0},   {1, 44, 34, 0, 0, 0},   {1, 70, 55, 0, 0, 0},
    {1, 100, 80, 0, 0, 0},  {1, 134, 108, 0, 0, 0}, {2, 86, 68, 0, 0, 0},
    {2, 98, 78, 0, 0, 0},   {2, 121, 97, 0, 0, 0},  {2, 146, 116, 0, 0, 0},
    {2, 86, 68, 2, 87, 69},
};

// Alignment pattern centres, versions 1 to 10.
const std::vector<int> &alignment(int version)
{
    static const std::vector<int> kCentres[kMaxVersion] = {
        {}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42}, {6, 26, 46}, {6, 28, 50},
    };
    return kCentres[version - 1];
}

// BCH codes of the format and version information.
constexpr int kG15 = (1 << 10) | (1 << 8) | (1 << 5) | (1 << 4) | (1 << 2) | (1 << 1) | (1 << 0);
constexpr int kG18 = (1 << 12) | (1 << 11) | (1 << 10) | (1 << 9) | (1 << 8) | (1 << 5) | (1 << 2) | (1 << 0);
constexpr int kG15Mask = (1 << 14) | (1 << 12) | (1 << 10) | (1 << 4) | (1 << 1);

int bch_digit(int data)
{
    unsigned value = static_cast<unsigned>(data);
    int digit = 0;
    while (value != 0)
    {
        ++digit;
        value >>= 1;
    }
    return digit;
}

int bch_type_info(int data)
{
    int d = data << 10;
    while (bch_digit(d) - bch_digit(kG15) >= 0)
        d ^= kG15 << (bch_digit(d) - bch_digit(kG15));
    return ((data << 10) | d) ^ kG15Mask;
}

int bch_type_number(int data)
{
    int d = data << 12;
    while (bch_digit(d) - bch_digit(kG18) >= 0)
        d ^= kG18 << (bch_digit(d) - bch_digit(kG18));
    return (data << 12) | d;
}

bool mask_bit(int pattern, int i, int j)
{
    switch (pattern)
    {
    case 0:
        return (i + j) % 2 == 0;
    case 1:
        return i % 2 == 0;
    case 2:
        return j % 3 == 0;
    case 3:
        return (i + j) % 3 == 0;
    case 4:
        return (i / 2 + j / 3) % 2 == 0;
    case 5:
        return (i * j) % 2 + (i * j) % 3 == 0;
    case 6:
        return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
    default:
        return ((i * j) % 3 + (i + j) % 2) % 2 == 0;
    }
}

// The modules while they are being drawn: -1 is still free, 0 light, 1 dark.
struct Matrix
{
    int size = 0;
    std::vector<signed char> m;

    signed char &at(int row, int col) { return m[static_cast<std::size_t>(row) * size + col]; }
    bool dark(int row, int col) const { return m[static_cast<std::size_t>(row) * size + col] == 1; }
};

void position_probe(Matrix &x, int row, int col)
{
    for (int r = -1; r <= 7; ++r)
    {
        if (row + r <= -1 || x.size <= row + r)
            continue;
        for (int c = -1; c <= 7; ++c)
        {
            if (col + c <= -1 || x.size <= col + c)
                continue;
            const bool dark = (0 <= r && r <= 6 && (c == 0 || c == 6)) ||
                              (0 <= c && c <= 6 && (r == 0 || r == 6)) ||
                              (2 <= r && r <= 4 && 2 <= c && c <= 4);
            x.at(row + r, col + c) = dark ? 1 : 0;
        }
    }
}

void alignment_patterns(Matrix &x, int version)
{
    const std::vector<int> &centres = alignment(version);
    for (const int row : centres)
        for (const int col : centres)
        {
            if (x.at(row, col) != -1)
                continue;
            for (int r = -2; r <= 2; ++r)
                for (int c = -2; c <= 2; ++c)
                    x.at(row + r, col + c) =
                        (std::abs(r) == 2 || std::abs(c) == 2 || (r == 0 && c == 0)) ? 1 : 0;
        }
}

void timing_patterns(Matrix &x)
{
    for (int r = 8; r < x.size - 8; ++r)
    {
        if (x.at(r, 6) != -1)
            continue;
        x.at(r, 6) = (r % 2 == 0) ? 1 : 0;
    }
    for (int c = 8; c < x.size - 8; ++c)
    {
        if (x.at(6, c) != -1)
            continue;
        x.at(6, c) = (c % 2 == 0) ? 1 : 0;
    }
}

void format_info(Matrix &x, bool test, int mask)
{
    const int bits = bch_type_info((kLevelL << 3) | mask);
    const auto bit = [&](int v) -> signed char { return (!test && ((bits >> v) & 1) == 1) ? 1 : 0; };
    for (int v = 0; v < 15; ++v)
    {
        if (v < 6)
            x.at(v, 8) = bit(v);
        else if (v < 8)
            x.at(v + 1, 8) = bit(v);
        else
            x.at(x.size - 15 + v, 8) = bit(v);
    }
    for (int h = 0; h < 15; ++h)
    {
        if (h < 8)
            x.at(8, x.size - h - 1) = bit(h);
        else if (h < 9)
            x.at(8, 15 - h - 1 + 1) = bit(h);
        else
            x.at(8, 15 - h - 1) = bit(h);
    }
    x.at(x.size - 8, 8) = test ? 0 : 1;
}

void version_info(Matrix &x, bool test, int version)
{
    const int bits = bch_type_number(version);
    const auto bit = [&](int i) -> signed char { return (!test && ((bits >> i) & 1) == 1) ? 1 : 0; };
    for (int i = 0; i < 18; ++i)
        x.at(i / 3, i % 3 + x.size - 8 - 3) = bit(i);
    for (int i = 0; i < 18; ++i)
        x.at(i % 3 + x.size - 8 - 3, i / 3) = bit(i);
}

// Places the codewords in the free modules, zigzagging up and down from the
// bottom right corner, and applies the mask.
void map_data(Matrix &x, const std::vector<int> &codewords, int mask)
{
    int inc = -1;
    int row = x.size - 1;
    int bit_index = 7;
    std::size_t byte_index = 0;
    for (int col = x.size - 1; col > 0; col -= 2)
    {
        if (col == 6)
            --col;
        while (true)
        {
            for (int c = 0; c < 2; ++c)
            {
                if (x.at(row, col - c) != -1)
                    continue;
                bool dark = false;
                if (byte_index < codewords.size())
                    dark = ((codewords[byte_index] >> bit_index) & 1) == 1;
                if (mask_bit(mask, row, col - c))
                    dark = !dark;
                x.at(row, col - c) = dark ? 1 : 0;
                if (--bit_index == -1)
                {
                    ++byte_index;
                    bit_index = 7;
                }
            }
            row += inc;
            if (row < 0 || x.size <= row)
            {
                row -= inc;
                inc = -inc;
                break;
            }
        }
    }
}

// The matrix for one mask. With test set, the format and version information
// stay light, as the original does while it scores the masks.
Matrix build(int version, const std::vector<int> &codewords, bool test, int mask)
{
    Matrix x;
    x.size = version * 4 + 17;
    x.m.assign(static_cast<std::size_t>(x.size) * x.size, -1);
    position_probe(x, 0, 0);
    position_probe(x, x.size - 7, 0);
    position_probe(x, 0, x.size - 7);
    alignment_patterns(x, version);
    timing_patterns(x);
    format_info(x, test, mask);
    if (version >= 7)
        version_info(x, test, version);
    map_data(x, codewords, mask);
    return x;
}

// The penalty score that picks the mask: runs, 2x2 blocks, finder-like
// patterns and the balance of dark and light.
double lost_point(const Matrix &x)
{
    const int n = x.size;
    double lost = 0.0;
    for (int row = 0; row < n; ++row)
        for (int col = 0; col < n; ++col)
        {
            int same = 0;
            const bool dark = x.dark(row, col);
            for (int r = -1; r <= 1; ++r)
            {
                if (row + r < 0 || n <= row + r)
                    continue;
                for (int c = -1; c <= 1; ++c)
                {
                    if (col + c < 0 || n <= col + c)
                        continue;
                    if (r == 0 && c == 0)
                        continue;
                    if (dark == x.dark(row + r, col + c))
                        ++same;
                }
            }
            if (same > 5)
                lost += 3 + same - 5;
        }

    for (int row = 0; row < n - 1; ++row)
        for (int col = 0; col < n - 1; ++col)
        {
            const int count = static_cast<int>(x.dark(row, col)) + static_cast<int>(x.dark(row + 1, col)) +
                              static_cast<int>(x.dark(row, col + 1)) + static_cast<int>(x.dark(row + 1, col + 1));
            if (count == 0 || count == 4)
                lost += 3;
        }

    for (int row = 0; row < n; ++row)
        for (int col = 0; col < n - 6; ++col)
            if (x.dark(row, col) && !x.dark(row, col + 1) && x.dark(row, col + 2) && x.dark(row, col + 3) &&
                x.dark(row, col + 4) && !x.dark(row, col + 5) && x.dark(row, col + 6))
                lost += 40;
    for (int col = 0; col < n; ++col)
        for (int row = 0; row < n - 6; ++row)
            if (x.dark(row, col) && !x.dark(row + 1, col) && x.dark(row + 2, col) && x.dark(row + 3, col) &&
                x.dark(row + 4, col) && !x.dark(row + 5, col) && x.dark(row + 6, col))
                lost += 40;

    int dark_count = 0;
    for (int col = 0; col < n; ++col)
        for (int row = 0; row < n; ++row)
            if (x.dark(row, col))
                ++dark_count;
    const double ratio = std::fabs(100.0 * dark_count / n / n - 50.0) / 5.0;
    lost += ratio * 10.0;
    return lost;
}

// The data codewords with their error correction, interleaved as they are
// placed. Returns false when the text does not fit in this version.
bool make_codewords(const std::string &text, int version, std::vector<int> &out)
{
    const BlockGroups &groups = kBlocksL[version - 1];
    struct Block
    {
        int data;
        int total;
    };
    std::vector<Block> blocks;
    for (int i = 0; i < groups.count1; ++i)
        blocks.push_back({groups.data1, groups.total1});
    for (int i = 0; i < groups.count2; ++i)
        blocks.push_back({groups.data2, groups.total2});
    int total_data = 0;
    for (const Block &b : blocks)
        total_data += b.data;

    // Byte mode: the mode, the length, then the bytes.
    std::vector<int> bytes;
    std::size_t bit_count = 0;
    const auto put_bit = [&](bool bit) {
        if (bit_count % 8 == 0)
            bytes.push_back(0);
        if (bit)
            bytes.back() |= 0x80 >> (bit_count % 8);
        ++bit_count;
    };
    const auto put = [&](int value, int length) {
        for (int i = 0; i < length; ++i)
            put_bit(((value >> (length - i - 1)) & 1) == 1);
    };
    const int length_bits = version < 10 ? 8 : 16;
    put(kModeByte, 4);
    put(static_cast<int>(text.size()), length_bits);
    for (const char ch : text)
        put(static_cast<unsigned char>(ch), 8);

    const std::size_t capacity = static_cast<std::size_t>(total_data) * 8;
    if (bit_count > capacity)
        return false;
    if (bit_count + 4 <= capacity)
        put(0, 4);
    while (bit_count % 8 != 0)
        put_bit(false);
    while (bit_count < capacity)
    {
        put(kPad0, 8);
        if (bit_count >= capacity)
            break;
        put(kPad1, 8);
    }

    std::vector<std::vector<int>> data_blocks;
    std::vector<std::vector<int>> ec_blocks;
    std::size_t offset = 0;
    std::size_t max_data = 0;
    std::size_t max_ec = 0;
    for (const Block &b : blocks)
    {
        const int ec_count = b.total - b.data;
        max_data = std::max(max_data, static_cast<std::size_t>(b.data));
        max_ec = std::max(max_ec, static_cast<std::size_t>(ec_count));

        std::vector<int> data(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                              bytes.begin() + static_cast<std::ptrdiff_t>(offset + b.data));
        offset += static_cast<std::size_t>(b.data);

        const Poly rs = generator(ec_count);
        const Poly rest = remainder(make_poly(data, static_cast<int>(rs.size()) - 1), rs);
        std::vector<int> ec(rs.size() - 1, 0);
        for (std::size_t x = 0; x < ec.size(); ++x)
        {
            const int index = static_cast<int>(x + rest.size()) - static_cast<int>(ec.size());
            ec[x] = index >= 0 ? rest[static_cast<std::size_t>(index)] : 0;
        }
        data_blocks.push_back(data);
        ec_blocks.push_back(ec);
    }

    out.clear();
    for (std::size_t z = 0; z < max_data; ++z)
        for (const auto &block : data_blocks)
            if (z < block.size())
                out.push_back(block[z]);
    for (std::size_t x = 0; x < max_ec; ++x)
        for (const auto &block : ec_blocks)
            if (x < block.size())
                out.push_back(block[x]);
    return true;
}

} // namespace

bool encode(const std::string &text, Code &out)
{
    std::vector<int> codewords;
    int version = 0;
    for (int v = 1; v <= kMaxVersion && version == 0; ++v)
        if (make_codewords(text, v, codewords))
            version = v;
    if (version == 0)
        return false;

    int best_mask = 0;
    double best_lost = 0.0;
    for (int mask = 0; mask < 8; ++mask)
    {
        const double lost = lost_point(build(version, codewords, true, mask));
        if (mask == 0 || best_lost > lost)
        {
            best_lost = lost;
            best_mask = mask;
        }
    }

    const Matrix x = build(version, codewords, false, best_mask);
    out.size = x.size;
    out.dark.assign(static_cast<std::size_t>(x.size) * x.size, 0);
    for (std::size_t i = 0; i < out.dark.size(); ++i)
        out.dark[i] = x.m[i] == 1 ? 1 : 0;
    return true;
}

} // namespace qr
