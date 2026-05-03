#include "core/AppPaths.h"

#include <cstdlib>

namespace blockforge
{
static std::filesystem::path homeDir()
{
    if (const char* v = std::getenv("BLOCKFORGE_DATA_DIR"); v && *v)
    {
        return std::filesystem::path(v);
    }

    if (const char* v = std::getenv("APPDATA"); v && *v)
    {
        return std::filesystem::path(v) / "BlockForge";
    }

    if (const char* v = std::getenv("HOME"); v && *v)
    {
        return std::filesystem::path(v) / ".blockforge";
    }

    return std::filesystem::current_path() / ".blockforge";
}

std::filesystem::path AppPaths::dataDir()
{
    return homeDir();
}

std::filesystem::path AppPaths::instancesDir()
{
    return dataDir() / "instances";
}
}

