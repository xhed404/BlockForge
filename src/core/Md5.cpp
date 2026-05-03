#include "core/Md5.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace blockforge
{
static constexpr std::uint32_t bswap32(std::uint32_t v)
{
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static constexpr std::uint64_t bswap64(std::uint64_t v)
{
    return ((v & 0x00000000000000FFull) << 56) | ((v & 0x000000000000FF00ull) << 40) | ((v & 0x0000000000FF0000ull) << 24) |
           ((v & 0x00000000FF000000ull) << 8) | ((v & 0x000000FF00000000ull) >> 8) | ((v & 0x0000FF0000000000ull) >> 24) |
           ((v & 0x00FF000000000000ull) >> 40) | ((v & 0xFF00000000000000ull) >> 56);
}

static constexpr std::uint32_t s_shifts[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static constexpr std::uint32_t s_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static constexpr std::uint32_t f(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) | (~x & z); }
static constexpr std::uint32_t g(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & z) | (y & ~z); }
static constexpr std::uint32_t h(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return x ^ y ^ z; }
static constexpr std::uint32_t i(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return y ^ (x | ~z); }

static void processBlock(const std::uint8_t block[64], std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d)
{
    std::uint32_t m[16];
    for (int j = 0; j < 16; ++j)
    {
        std::uint32_t v = 0;
        std::memcpy(&v, block + (j * 4), 4);
        if constexpr (std::endian::native == std::endian::big)
        {
            v = bswap32(v);
        }
        m[j] = v;
    }

    std::uint32_t aa = a;
    std::uint32_t bb = b;
    std::uint32_t cc = c;
    std::uint32_t dd = d;

    for (int j = 0; j < 64; ++j)
    {
        std::uint32_t fval = 0;
        int gidx = 0;
        if (j < 16)
        {
            fval = f(bb, cc, dd);
            gidx = j;
        }
        else if (j < 32)
        {
            fval = g(bb, cc, dd);
            gidx = (5 * j + 1) % 16;
        }
        else if (j < 48)
        {
            fval = h(bb, cc, dd);
            gidx = (3 * j + 5) % 16;
        }
        else
        {
            fval = i(bb, cc, dd);
            gidx = (7 * j) % 16;
        }

        const std::uint32_t temp = dd;
        dd = cc;
        cc = bb;

        const std::uint32_t x = aa + fval + s_k[j] + m[gidx];
        bb = bb + std::rotl(x, static_cast<int>(s_shifts[j]));
        aa = temp;
    }

    a += aa;
    b += bb;
    c += cc;
    d += dd;
}

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data)
{
    std::uint32_t a = 0x67452301;
    std::uint32_t b = 0xefcdab89;
    std::uint32_t c = 0x98badcfe;
    std::uint32_t d = 0x10325476;

    std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8ull;

    std::size_t offset = 0;
    while (offset + 64 <= data.size())
    {
        processBlock(data.data() + offset, a, b, c, d);
        offset += 64;
    }

    std::uint8_t block[64] = {};
    const std::size_t rem = data.size() - offset;
    if (rem > 0)
    {
        std::memcpy(block, data.data() + offset, rem);
    }
    block[rem] = 0x80;

    if (rem >= 56)
    {
        processBlock(block, a, b, c, d);
        std::memset(block, 0, 64);
    }

    std::uint64_t leBitLen = bitLen;
    if constexpr (std::endian::native == std::endian::big)
    {
        leBitLen = bswap64(leBitLen);
    }
    std::memcpy(block + 56, &leBitLen, 8);
    processBlock(block, a, b, c, d);

    std::array<std::uint8_t, 16> out{};
    std::uint32_t words[4] = {a, b, c, d};
    for (int j = 0; j < 4; ++j)
    {
        std::uint32_t w = words[j];
        if constexpr (std::endian::native == std::endian::big)
        {
            w = bswap32(w);
        }
        std::memcpy(out.data() + (j * 4), &w, 4);
    }
    return out;
}
}
