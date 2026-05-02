#pragma once

#include "core/Instance.h"

#include <QObject>

#if defined(BLOCKFORGE_BUILD_MC)
#include "minecraft/MinecraftLauncher.h"
#endif

namespace blockforge
{
class LaunchWorker final : public QObject
{
    Q_OBJECT

public:
    LaunchWorker();

    void configure(QString dataDir, Instance instance
#if defined(BLOCKFORGE_BUILD_MC)
                   ,
                   AuthSession offlineSession,
                   LaunchOptions options
#endif
    );

public slots:
    void run();

signals:
    void logLine(const QString& line);
#if defined(BLOCKFORGE_BUILD_MC)
    void readyToLaunch(const LaunchCommand& cmd);
#endif
    void failed(const QString& error);
    void finished();

private:
    QString m_dataDir;
    Instance m_instance;
#if defined(BLOCKFORGE_BUILD_MC)
    AuthSession m_offlineSession;
    LaunchOptions m_options;
#endif
};
}
