#include "core/Instance.h"

namespace blockforge
{
std::string toString(LoaderType type)
{
    switch (type)
    {
        case LoaderType::Vanilla: return "vanilla";
        case LoaderType::Fabric: return "fabric";
        case LoaderType::Forge: return "forge";
    }
    return "vanilla";
}

std::optional<LoaderType> loaderTypeFromString(const std::string& s)
{
    if (s == "vanilla") return LoaderType::Vanilla;
    if (s == "fabric") return LoaderType::Fabric;
    if (s == "forge") return LoaderType::Forge;
    return std::nullopt;
}
}

