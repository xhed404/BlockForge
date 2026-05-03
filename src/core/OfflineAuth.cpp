#include "core/OfflineAuth.h"

#include "core/Md5.h"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace blockforge
{
static std::string formatUuid(const std::array<std::uint8_t, 16>& bytes)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i)
    {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) ss << "-";
    }
    return ss.str();
}

OfflineProfile makeOfflineProfile(const std::string& name)
{
    static constexpr std::string_view prefix = "OfflinePlayer:";
    std::vector<std::uint8_t> buf;
    buf.reserve(prefix.size() + name.size());
    buf.insert(buf.end(), prefix.begin(), prefix.end());
    buf.insert(buf.end(), name.begin(), name.end());

    auto digest = md5(buf);

    digest[6] = static_cast<std::uint8_t>((digest[6] & 0x0F) | 0x30);
    digest[8] = static_cast<std::uint8_t>((digest[8] & 0x3F) | 0x80);

    OfflineProfile profile;
    profile.name = name;
    profile.uuid = formatUuid(digest);
    return profile;
}
}

