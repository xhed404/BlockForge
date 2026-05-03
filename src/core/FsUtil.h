#pragma once

#include <filesystem>

namespace blockforge
{
bool copyTree(const std::filesystem::path& from, const std::filesystem::path& to);
}

