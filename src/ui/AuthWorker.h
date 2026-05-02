#pragma once

#include <QObject>

#if defined(BLOCKFORGE_BUILD_MC)
#include "auth/MicrosoftAuth.h"
#endif

namespace blockforge
{
class AuthWorker final : public QObject
{
    Q_OBJECT

public:
    AuthWorker();
    void configure(QString dataDir, QString clientId);

public slots:
    void run();

signals:
    void logLine(const QString& line);
#if defined(BLOCKFORGE_BUILD_MC)
    void deviceCodeReady(const DeviceCode& code);
    void accountReady(const QString& displayName);
#endif
    void failed(const QString& error);
    void finished();

private:
    QString m_dataDir;
    QString m_clientId;
};
}

