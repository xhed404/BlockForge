#include "auth/AccountStore.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace blockforge
{
AccountStore::AccountStore(QString dataDir)
    : m_dataDir(std::move(dataDir))
{
}

QString AccountStore::path() const
{
    return QDir(m_dataDir).filePath("accounts.json");
}

QJsonObject AccountStore::readRoot(QString& error) const
{
    QFile f(path());
    if (!f.exists())
    {
        return QJsonObject{
            {"activeAccountId", ""},
            {"accounts", QJsonArray{}},
        };
    }
    if (!f.open(QIODevice::ReadOnly))
    {
        error = "Cannot read accounts.json";
        return {};
    }
    const auto bytes = f.readAll();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (doc.isNull() || !doc.isObject())
    {
        error = "Invalid accounts.json";
        return {};
    }
    return doc.object();
}

bool AccountStore::writeRoot(const QJsonObject& root, QString& error) const
{
    QFileInfo fi(path());
    QDir().mkpath(fi.absolutePath());

    QFile f(path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        error = "Cannot write accounts.json";
        return false;
    }
    const QJsonDocument doc(root);
    f.write(doc.toJson(QJsonDocument::Indented));
    f.flush();
    f.close();

    QFile::setPermissions(path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool AccountStore::hasActiveAccount() const
{
    QString error;
    const auto root = readRoot(error);
    if (!error.isEmpty()) return false;
    return !root.value("activeAccountId").toString().isEmpty();
}

QList<Account> AccountStore::listAccounts(QString& error) const
{
    const auto root = readRoot(error);
    if (!error.isEmpty()) return {};
    const auto arr = root.value("accounts").toArray();

    QList<Account> out;
    out.reserve(arr.size());
    for (const auto& v : arr)
    {
        const auto o = v.toObject();
        Account a;
        a.accountId = o.value("accountId").toString();
        a.displayName = o.value("displayName").toString();
        a.uuid = o.value("uuid").toString();
        a.msRefreshToken = o.value("msRefreshToken").toString();
        a.mcAccessToken = o.value("mcAccessToken").toString();
        a.mcExpiresAtMs = static_cast<qint64>(o.value("mcExpiresAtMs").toDouble(0));
        a.xuid = o.value("xuid").toString();
        a.userType = o.value("userType").toString("msa");
        if (!a.accountId.isEmpty())
        {
            out.push_back(a);
        }
    }
    return out;
}

Account AccountStore::activeAccount(QString& error) const
{
    const auto root = readRoot(error);
    if (!error.isEmpty()) return {};
    const auto activeId = root.value("activeAccountId").toString();
    if (activeId.isEmpty())
    {
        error = "No active account";
        return {};
    }
    const auto arr = root.value("accounts").toArray();
    for (const auto& v : arr)
    {
        const auto o = v.toObject();
        if (o.value("accountId").toString() != activeId) continue;
        Account a;
        a.accountId = activeId;
        a.displayName = o.value("displayName").toString();
        a.uuid = o.value("uuid").toString();
        a.msRefreshToken = o.value("msRefreshToken").toString();
        a.mcAccessToken = o.value("mcAccessToken").toString();
        a.mcExpiresAtMs = static_cast<qint64>(o.value("mcExpiresAtMs").toDouble(0));
        a.xuid = o.value("xuid").toString();
        a.userType = o.value("userType").toString("msa");
        return a;
    }
    error = "Active account not found";
    return {};
}

bool AccountStore::setActiveAccount(const QString& accountId, QString& error)
{
    auto root = readRoot(error);
    if (!error.isEmpty()) return false;
    root["activeAccountId"] = accountId;
    return writeRoot(root, error);
}

bool AccountStore::clearActiveAccount(QString& error)
{
    auto root = readRoot(error);
    if (!error.isEmpty()) return false;
    root["activeAccountId"] = "";
    return writeRoot(root, error);
}

bool AccountStore::upsertAccount(const Account& account, QString& error)
{
    auto root = readRoot(error);
    if (!error.isEmpty()) return false;

    auto arr = root.value("accounts").toArray();
    QJsonArray out;
    bool replaced = false;
    for (const auto& v : arr)
    {
        const auto o = v.toObject();
        if (o.value("accountId").toString() != account.accountId)
        {
            out.push_back(o);
            continue;
        }
        replaced = true;
    }

    QJsonObject o;
    o["accountId"] = account.accountId;
    o["displayName"] = account.displayName;
    o["uuid"] = account.uuid;
    o["msRefreshToken"] = account.msRefreshToken;
    o["mcAccessToken"] = account.mcAccessToken;
    o["mcExpiresAtMs"] = static_cast<double>(account.mcExpiresAtMs);
    o["xuid"] = account.xuid;
    o["userType"] = account.userType.isEmpty() ? "msa" : account.userType;
    out.push_back(o);

    root["accounts"] = out;
    if (root.value("activeAccountId").toString().isEmpty() || !replaced)
    {
        root["activeAccountId"] = account.accountId;
    }

    return writeRoot(root, error);
}

bool AccountStore::removeAccount(const QString& accountId, QString& error)
{
    auto root = readRoot(error);
    if (!error.isEmpty()) return false;

    const auto active = root.value("activeAccountId").toString();
    auto arr = root.value("accounts").toArray();
    QJsonArray out;
    for (const auto& v : arr)
    {
        const auto o = v.toObject();
        if (o.value("accountId").toString() == accountId)
        {
            continue;
        }
        out.push_back(o);
    }
    root["accounts"] = out;

    if (active == accountId)
    {
        root["activeAccountId"] = out.isEmpty() ? "" : out.first().toObject().value("accountId").toString();
    }

    return writeRoot(root, error);
}
}
