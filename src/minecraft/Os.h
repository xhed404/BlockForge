#pragma once

#include <string>

namespace blockforge
{
enum class OsName
{
    Windows,
    Linux,
    Osx,
    Unknown,
};

OsName currentOs();
std::string toString(OsName os);
std::string currentArch();
}
