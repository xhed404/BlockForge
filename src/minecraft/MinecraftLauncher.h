#pragma once

#include "core/OfflineAuth.h"
#include "core/Instance.h"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <vector>

namespace blockforge
{
struct AuthSession final
{
    QString playerName;
    QString uuid;
    QString accessToken;
    QString userType;
    QString xuid;
    QString clientId;
};

struct LaunchCommand final
{
    QString program;
    QStringList args;
};

struct LaunchOptions final
{
    QString javaPath;
    int maxRamMb = 4096;
    int resolutionWidth = 0;
    int resolutionHeight = 0;
    QStringList extraJvmArgs;
};

class MinecraftLauncher final
{
public:
    explicit MinecraftLauncher(QString dataDir);

    LaunchCommand buildLaunchCommand(const Instance& instance, const AuthSession& session, const LaunchOptions& options, QString& error) const;

private:
    QString m_dataDir;
};
}
