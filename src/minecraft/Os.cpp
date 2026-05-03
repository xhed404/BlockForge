#include "minecraft/Os.h"

#include <QtCore/QString>
#include <QtCore/QSysInfo>

namespace blockforge
{
OsName currentOs()
{
    const auto type = QSysInfo::productType().toLower();
    if (type == "windows") return OsName::Windows;
    if (type == "osx" || type == "macos") return OsName::Osx;
    if (type == "linux" || type == "ubuntu" || type == "debian" || type == "fedora") return OsName::Linux;
    return OsName::Unknown;
}

std::string toString(OsName os)
{
    switch (os)
    {
        case OsName::Windows: return "windows";
        case OsName::Linux: return "linux";
        case OsName::Osx: return "osx";
        case OsName::Unknown: return "unknown";
    }
    return "unknown";
}

std::string currentArch()
{
    return (sizeof(void*) >= 8) ? "64" : "32";
}
}
