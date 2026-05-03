#pragma once

#include "core/Instance.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace blockforge
{
class InstanceStore final
{
public:
    explicit InstanceStore(std::filesystem::path instancesDir);

    std::vector<Instance> list() const;
    Instance create(const std::string& name, const std::string& mcVersion, LoaderType loader, std::optional<std::string> loaderVersion = std::nullopt);

private:
    std::filesystem::path m_instancesDir;
};
}
