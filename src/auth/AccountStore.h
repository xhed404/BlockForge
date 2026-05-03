#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

namespace blockforge
{
struct Account final
{
    QString accountId;
    QString displayName;
    QString uuid;

    QString msRefreshToken;
    QString mcAccessToken;
    qint64 mcExpiresAtMs = 0;

    QString xuid;
    QString userType;
};

class AccountStore final
{
public:
    explicit AccountStore(QString dataDir);

    Account activeAccount(QString& error) const;
    bool hasActiveAccount() const;
    QList<Account> listAccounts(QString& error) const;
    bool setActiveAccount(const QString& accountId, QString& error);
    bool upsertAccount(const Account& account, QString& error);
    bool removeAccount(const QString& accountId, QString& error);
    bool clearActiveAccount(QString& error);

private:
    QString m_dataDir;

    QString path() const;
    QJsonObject readRoot(QString& error) const;
    bool writeRoot(const QJsonObject& root, QString& error) const;
};
}
