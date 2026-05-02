#pragma once

#include "auth/AccountStore.h"
#include "minecraft/HttpClient.h"

#include <QtCore/QString>

namespace blockforge
{
struct DeviceCode final
{
    QString deviceCode;
    QString userCode;
    QString verificationUri;
    int intervalSec = 5;
    int expiresInSec = 900;
    QString message;
};

struct MsTokens final
{
    QString accessToken;
    QString refreshToken;
    int expiresInSec = 0;
};

class MicrosoftAuth final
{
public:
    explicit MicrosoftAuth(QString dataDir);

    DeviceCode requestDeviceCode(const QString& clientId, QString& error);
    MsTokens pollDeviceCodeToken(const QString& clientId, const DeviceCode& code, QString& error);
    MsTokens refreshMsToken(const QString& clientId, const QString& refreshToken, QString& error);

    Account exchangeForMinecraftAccount(const MsTokens& ms, QString& error);

private:
    QString m_dataDir;
    HttpClient m_http;
};
}

