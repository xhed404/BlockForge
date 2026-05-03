#pragma once

#include <string>

namespace blockforge
{
struct OfflineProfile final
{
    std::string name;
    std::string uuid;
};

OfflineProfile makeOfflineProfile(const std::string& name);
}

