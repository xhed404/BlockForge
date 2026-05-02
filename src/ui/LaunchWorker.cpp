#include "ui/LaunchWorker.h"

#if defined(BLOCKFORGE_BUILD_MC)
#include "auth/AccountStore.h"
#include "auth/MicrosoftAuth.h"
#include "core/Settings.h"
#include "minecraft/MinecraftInstaller.h"
#include "minecraft/MinecraftLauncher.h"
#endif

namespace blockforge
{
LaunchWorker::LaunchWorker() = default;

void LaunchWorker::configure(QString dataDir, Instance instance
#if defined(BLOCKFORGE_BUILD_MC)
                             ,
                             AuthSession offlineSession,
                             LaunchOptions options
#endif
)
{
    m_dataDir = std::move(dataDir);
    m_instance = std::move(instance);
#if defined(BLOCKFORGE_BUILD_MC)
    m_offlineSession = std::move(offlineSession);
    m_options = std::move(options);
#endif
}

void LaunchWorker::run()
{
#if !defined(BLOCKFORGE_BUILD_MC)
    emit failed("Minecraft integration disabled at build time");
    emit finished();
#else
    AuthSession session = m_offlineSession;
    AccountStore store(m_dataDir);
    if (store.hasActiveAccount())
    {
        QString err;
        auto acc = store.activeAccount(err);
        if (!err.isEmpty())
        {
            emit logLine(QString("Account error: %1").arg(err));
        }
        else
        {
            const auto now = QDateTime::currentMSecsSinceEpoch();
            const bool tokenOk = !acc.mcAccessToken.isEmpty() && (acc.mcExpiresAtMs - now) > 60000;
            if (!tokenOk)
            {
                auto settings = appSettings();
                const auto clientId = settings.get("ms.clientId").value_or("");
                if (!clientId.empty())
                {
                    emit logLine("Refreshing Microsoft account...");
                    MicrosoftAuth auth(m_dataDir);
                    QString e;
                    const auto ms = auth.refreshMsToken(QString::fromStdString(clientId), acc.msRefreshToken, e);
                    if (!e.isEmpty())
                    {
                        emit logLine(QString("Refresh failed: %1").arg(e));
                    }
                    else
                    {
                        const auto newAcc = auth.exchangeForMinecraftAccount(ms, e);
                        if (!e.isEmpty())
                        {
                            emit logLine(QString("Minecraft token failed: %1").arg(e));
                        }
                        else
                        {
                            QString se;
                            store.upsertAccount(newAcc, se);
                            if (!se.isEmpty())
                            {
                                emit logLine(QString("Account save failed: %1").arg(se));
                            }
                            acc = newAcc;
                        }
                    }
                }
                else
                {
                    emit logLine("Microsoft client id is not set, cannot refresh token.");
                }
            }

            if (!acc.mcAccessToken.isEmpty() && !acc.uuid.isEmpty() && !acc.displayName.isEmpty())
            {
                session.playerName = acc.displayName;
                session.uuid = acc.uuid;
                session.accessToken = acc.mcAccessToken;
                session.userType = acc.userType.isEmpty() ? "msa" : acc.userType;
                session.xuid = acc.xuid;
                session.clientId = "0";
                emit logLine(QString("Using online account: %1").arg(acc.displayName));
            }
        }
    }

    emit logLine("Preparing version...");
    MinecraftInstaller installer(m_dataDir);
    QString version = QString::fromStdString(m_instance.minecraftVersion);
    if (m_instance.loaderType == LoaderType::Fabric)
    {
        emit logLine("Installing Fabric...");
        QString fabricId;
        const auto r = installer.installFabric(QString::fromStdString(m_instance.minecraftVersion),
                                               m_instance.loaderVersion.has_value() ? QString::fromStdString(*m_instance.loaderVersion) : QString(),
                                               fabricId);
        if (!r.ok)
        {
            emit failed(r.error);
            emit finished();
            return;
        }
        version = fabricId;
    }
    else if (m_instance.loaderType == LoaderType::Forge)
    {
        emit logLine("Installing Forge...");
        QString forgeId;
        const auto r = installer.installForge(QString::fromStdString(m_instance.minecraftVersion),
                                              m_instance.loaderVersion.has_value() ? QString::fromStdString(*m_instance.loaderVersion) : QString(),
                                              forgeId);
        if (!r.ok)
        {
            emit failed(r.error);
            emit finished();
            return;
        }
        version = forgeId;
    }

    auto r = installer.installVersion(version);
    if (!r.ok)
    {
        emit failed(r.error);
        emit finished();
        return;
    }

    emit logLine("Downloading assets...");
    r = installer.downloadAssets(version, 0);
    if (!r.ok)
    {
        emit failed(r.error);
        emit finished();
        return;
    }

    emit logLine("Preparing natives...");
    r = installer.prepareNatives(version, QString::fromStdString(m_instance.id));
    if (!r.ok)
    {
        emit failed(r.error);
        emit finished();
        return;
    }

    emit logLine("Building launch command...");
    MinecraftLauncher launcher(m_dataDir);
    QString error;
    auto inst = m_instance;
    inst.minecraftVersion = version.toStdString();
    const auto cmd = launcher.buildLaunchCommand(inst, session, m_options, error);
    if (!error.isEmpty())
    {
        emit failed(error);
        emit finished();
        return;
    }

    emit readyToLaunch(cmd);
    emit finished();
#endif
}
}
