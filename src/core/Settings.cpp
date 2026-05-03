#include "core/Settings.h"

#include "core/AppPaths.h"

#include <fstream>
#include <map>

namespace blockforge
{
static std::map<std::string, std::string> readAll(const std::filesystem::path& path)
{
    std::map<std::string, std::string> out;
    std::ifstream in(path, std::ios::binary);
    if (!in.good())
    {
        return out;
    }

    std::string line;
    while (std::getline(in, line))
    {
        const auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0)
        {
            continue;
        }
        const auto key = line.substr(0, pos);
        const auto val = line.substr(pos + 1);
        out[key] = val;
    }
    return out;
}

static void writeAll(const std::filesystem::path& path, const std::map<std::string, std::string>& data)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& [k, v] : data)
    {
        out << k << "=" << v << "\n";
    }
}

Settings::Settings(std::filesystem::path path)
    : m_path(std::move(path))
{
}

std::optional<std::string> Settings::get(const std::string& key) const
{
    auto data = readAll(m_path);
    const auto it = data.find(key);
    if (it == data.end())
    {
        if (key == "offline.name")
        {
            return std::string("BlockForgePlayer");
        }
        if (key == "java.maxRamMb")
        {
            return std::string("4096");
        }
        if (key == "ms.clientId")
        {
            return std::string("f8cdef31-a31e-4b4a-93e4-5f571e91255a");
        }
        return std::nullopt;
    }
    return it->second;
}

void Settings::set(const std::string& key, const std::string& value)
{
    auto data = readAll(m_path);
    data[key] = value;
    writeAll(m_path, data);
}

Settings appSettings()
{
    return Settings(AppPaths::dataDir() / "settings.bf");
}
}
