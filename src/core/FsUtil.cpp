#include "core/FsUtil.h"

namespace blockforge
{
bool copyTree(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::error_code ec;
    std::filesystem::create_directories(to, ec);
    if (ec) return false;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(from, ec))
    {
        if (ec) return false;
        const auto rel = std::filesystem::relative(entry.path(), from, ec);
        if (ec) return false;
        const auto dst = to / rel;
        if (entry.is_directory())
        {
            std::filesystem::create_directories(dst, ec);
            if (ec) return false;
        }
        else if (entry.is_regular_file())
        {
            std::filesystem::create_directories(dst.parent_path(), ec);
            if (ec) return false;
            if (dst.filename() == "instance.bf" && std::filesystem::exists(dst))
            {
                continue;
            }
            std::filesystem::copy_file(entry.path(), dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }
    }
    return true;
}
}
