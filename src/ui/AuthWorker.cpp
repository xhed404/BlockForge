#include "ui/AuthWorker.h"

#if defined(BLOCKFORGE_BUILD_MC)
#include "auth/AccountStore.h"
#include "auth/MicrosoftAuth.h"
#endif

#include <QtCore/QDateTime>
#include <QtCore/QThread>

namespace blockforge
{
AuthWorker::AuthWorker() = default;

void AuthWorker::configure(QString dataDir, QString clientId)
{
    m_dataDir = std::move(dataDir);
    m_clientId = std::move(clientId);
}

void AuthWorker::run()
{
#if !defined(BLOCKFORGE_BUILD_MC)
    emit failed("Minecraft integration disabled at build time");
    emit finished();
#else
    if (m_clientId.isEmpty())
    {
        emit failed("Microsoft client id is empty");
        emit finished();
        return;
    }

    MicrosoftAuth auth(m_dataDir);
    QString error;
    const auto code = auth.requestDeviceCode(m_clientId, error);
    if (!error.isEmpty())
    {
        emit failed(error);
        emit finished();
        return;
    }

    emit deviceCodeReady(code);
    emit logLine(code.message);

    const auto deadline = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(code.expiresInSec) * 1000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
    {
        QThread::sleep(static_cast<unsigned long>(std::max(1, code.intervalSec)));
        error.clear();
        const auto ms = auth.pollDeviceCodeToken(m_clientId, code, error);
        if (error == "authorization_pending" || error == "slow_down")
        {
            continue;
        }
        if (!error.isEmpty())
        {
            emit failed(error);
            emit finished();
            return;
        }

        const auto acc = auth.exchangeForMinecraftAccount(ms, error);
        if (!error.isEmpty())
        {
            emit failed(error);
            emit finished();
            return;
        }

        AccountStore store(m_dataDir);
        QString se;
        if (!store.upsertAccount(acc, se))
        {
            emit failed(se);
            emit finished();
            return;
        }

        emit accountReady(acc.displayName);
        emit finished();
        return;
    }

    emit failed("Device code expired");
    emit finished();
#endif
}
}

