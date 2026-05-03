#pragma once

#include "minecraft/HttpClient.h"

#include <functional>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>

namespace blockforge
{
struct VersionInstallResult final
{
    bool ok = false;
    QString error;
};

struct VersionMeta final
{
    QString id;
    QString jarId;
    QString mainClass;
    QString assetsId;
    QUrl assetIndexUrl;
    QUrl clientUrl;
    QString clientSha1;
    int javaMajor = 8;
    QJsonDocument raw;
};

class MinecraftInstaller final
{
public:
    explicit MinecraftInstaller(QString dataDir);

    void setProgressCallback(std::function<void(const QString& phase, int current, int total)> cb);

    VersionInstallResult installVersion(const QString& versionId);
    VersionInstallResult installFabric(const QString& minecraftVersion, const QString& loaderVersion, QString& outVersionId);
    VersionInstallResult installForge(const QString& minecraftVersion, const QString& forgeVersion, QString& outVersionId);
    VersionInstallResult downloadAssets(const QString& versionId, int limit);
    VersionInstallResult prepareNatives(const QString& versionId, const QString& instanceId);
    VersionMeta readVersionMeta(const QString& versionId, QString& error);

    QString versionsDir() const;
    QString librariesDir() const;
    QString assetsDir() const;

private:
    QString m_dataDir;
    HttpClient m_http;
    std::function<void(const QString& phase, int current, int total)> m_progress;

    VersionInstallResult ensureManifest();
    QJsonDocument loadJsonFile(const QString& path, QString& error) const;
    VersionInstallResult saveJsonFile(const QString& path, const QByteArray& bytes);
    QString sha1OfFile(const QString& path) const;

    QJsonObject resolveVersionObject(const QString& versionId, QString& error) const;
};
}
