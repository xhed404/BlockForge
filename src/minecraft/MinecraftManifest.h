#pragma once

#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>

namespace blockforge
{
class MinecraftManifest final
{
public:
    explicit MinecraftManifest(QJsonDocument doc);

    QUrl versionUrl(const QString& versionId) const;

    static QUrl defaultManifestUrl();

private:
    QJsonDocument m_doc;
};
}

