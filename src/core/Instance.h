#pragma once

#include <optional>
#include <string>

namespace blockforge
{
enum class LoaderType
{
    Vanilla,
    Fabric,
    Forge,
};

struct Instance final
{
    std::string id;
    std::string name;
    std::string minecraftVersion;
    LoaderType loaderType = LoaderType::Vanilla;
    std::optional<std::string> loaderVersion;
};

std::string toString(LoaderType type);
std::optional<LoaderType> loaderTypeFromString(const std::string& s);
}

