#pragma once

#include <filesystem>

namespace blockforge
{
struct AppPaths final
{
    static std::filesystem::path dataDir();
    static std::filesystem::path instancesDir();
};
}

