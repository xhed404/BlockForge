#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace blockforge
{
class Settings final
{
public:
    explicit Settings(std::filesystem::path path);

    std::optional<std::string> get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);

private:
    std::filesystem::path m_path;
};

Settings appSettings();
}

