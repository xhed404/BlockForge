#include "minecraft/MinecraftManifest.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace blockforge
{
MinecraftManifest::MinecraftManifest(QJsonDocument doc)
    : m_doc(std::move(doc))
{
}

QUrl MinecraftManifest::versionUrl(const QString& versionId) const
{
    const auto root = m_doc.object();
    const auto versions = root.value("versions").toArray();
    for (const auto& v : versions)
    {
        const auto obj = v.toObject();
        if (obj.value("id").toString() == versionId)
        {
            return QUrl(obj.value("url").toString());
        }
    }
    return {};
}

QUrl MinecraftManifest::defaultManifestUrl()
{
    return QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
}
}

