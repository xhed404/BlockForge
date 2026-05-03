#include "core/InstanceStore.h"

#include <chrono>
#include <fstream>
#include <random>
#include <sstream>

namespace blockforge
{
static std::string randomId()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream ss;
    ss << std::hex << dist(rng);
    return ss.str();
}

static std::filesystem::path metaPath(const std::filesystem::path& instanceDir)
{
    return instanceDir / "instance.bf";
}

static void writeMeta(const std::filesystem::path& instanceDir, const Instance& instance)
{
    std::filesystem::create_directories(instanceDir);
    std::ofstream out(metaPath(instanceDir), std::ios::binary);
    out << "id=" << instance.id << "\n";
    out << "name=" << instance.name << "\n";
    out << "minecraftVersion=" << instance.minecraftVersion << "\n";
    out << "loaderType=" << toString(instance.loaderType) << "\n";
    if (instance.loaderVersion.has_value())
    {
        out << "loaderVersion=" << *instance.loaderVersion << "\n";
    }
}

static std::optional<std::string> getValue(const std::string& line, const std::string& key)
{
    const auto prefix = key + "=";
    if (line.rfind(prefix, 0) != 0)
    {
        return std::nullopt;
    }
    return line.substr(prefix.size());
}

static std::optional<Instance> readMeta(const std::filesystem::path& instanceDir)
{
    std::ifstream in(metaPath(instanceDir), std::ios::binary);
    if (!in.good())
    {
        return std::nullopt;
    }

    Instance instance;
    std::string line;
    while (std::getline(in, line))
    {
        if (auto v = getValue(line, "id")) instance.id = *v;
        else if (auto v = getValue(line, "name")) instance.name = *v;
        else if (auto v = getValue(line, "minecraftVersion")) instance.minecraftVersion = *v;
        else if (auto v = getValue(line, "loaderType"))
        {
            if (auto t = loaderTypeFromString(*v)) instance.loaderType = *t;
        }
        else if (auto v = getValue(line, "loaderVersion")) instance.loaderVersion = *v;
    }

    if (instance.id.empty() || instance.name.empty() || instance.minecraftVersion.empty())
    {
        return std::nullopt;
    }

    return instance;
}

InstanceStore::InstanceStore(std::filesystem::path instancesDir)
    : m_instancesDir(std::move(instancesDir))
{
}

std::vector<Instance> InstanceStore::list() const
{
    std::vector<Instance> out;
    if (!std::filesystem::exists(m_instancesDir))
    {
        return out;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_instancesDir))
    {
        if (!entry.is_directory())
        {
            continue;
        }
        if (auto instance = readMeta(entry.path()))
        {
            out.push_back(*instance);
        }
    }
    return out;
}

Instance InstanceStore::create(const std::string& name, const std::string& mcVersion, LoaderType loader, std::optional<std::string> loaderVersion)
{
    Instance instance;
    instance.id = randomId();
    instance.name = name;
    instance.minecraftVersion = mcVersion;
    instance.loaderType = loader;
    instance.loaderVersion = std::move(loaderVersion);

    const auto dir = m_instancesDir / instance.id;
    writeMeta(dir, instance);
    return instance;
}
}
