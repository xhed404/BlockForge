#include "auth/MicrosoftAuth.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace blockforge
{
static QString dashUuid(const QString& raw)
{
    const auto s = raw.trimmed();
    if (s.size() == 36) return s;
    if (s.size() != 32) return s;
    return QString("%1-%2-%3-%4-%5")
        .arg(s.mid(0, 8), s.mid(8, 4), s.mid(12, 4), s.mid(16, 4), s.mid(20, 12));
}

MicrosoftAuth::MicrosoftAuth(QString dataDir)
    : m_dataDir(std::move(dataDir))
{
}

DeviceCode MicrosoftAuth::requestDeviceCode(const QString& clientId, QString& error)
{
    QByteArray out;
    const auto res = m_http.postForm(QUrl("https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"),
                                     {
                                         {"client_id", clientId.toUtf8()},
                                         {"scope", "XboxLive.signin offline_access"},
                                     },
                                     out);
    if (!res.ok)
    {
        error = res.error;
        return {};
    }

    const auto doc = QJsonDocument::fromJson(out);
    const auto obj = doc.object();

    DeviceCode code;
    code.deviceCode = obj.value("device_code").toString();
    code.userCode = obj.value("user_code").toString();
    code.verificationUri = obj.value("verification_uri").toString();
    code.intervalSec = obj.value("interval").toInt(5);
    code.expiresInSec = obj.value("expires_in").toInt(900);
    code.message = obj.value("message").toString();

    if (code.deviceCode.isEmpty() || code.userCode.isEmpty())
    {
        error = "Invalid device code response";
    }
    return code;
}

MsTokens MicrosoftAuth::pollDeviceCodeToken(const QString& clientId, const DeviceCode& code, QString& error)
{
    QByteArray out;
    const auto res = m_http.postForm(QUrl("https://login.microsoftonline.com/consumers/oauth2/v2.0/token"),
                                     {
                                         {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                                         {"client_id", clientId.toUtf8()},
                                         {"device_code", code.deviceCode.toUtf8()},
                                     },
                                     out);
    if (!res.ok)
    {
        error = res.error;
        return {};
    }

    const auto doc = QJsonDocument::fromJson(out);
    const auto obj = doc.object();
    if (obj.contains("error"))
    {
        error = obj.value("error").toString();
        return {};
    }

    MsTokens t;
    t.accessToken = obj.value("access_token").toString();
    t.refreshToken = obj.value("refresh_token").toString();
    t.expiresInSec = obj.value("expires_in").toInt(0);
    if (t.accessToken.isEmpty() || t.refreshToken.isEmpty())
    {
        error = "Invalid token response";
    }
    return t;
}

MsTokens MicrosoftAuth::refreshMsToken(const QString& clientId, const QString& refreshToken, QString& error)
{
    QByteArray out;
    const auto res = m_http.postForm(QUrl("https://login.microsoftonline.com/consumers/oauth2/v2.0/token"),
                                     {
                                         {"grant_type", "refresh_token"},
                                         {"client_id", clientId.toUtf8()},
                                         {"refresh_token", refreshToken.toUtf8()},
                                         {"scope", "XboxLive.signin offline_access"},
                                     },
                                     out);
    if (!res.ok)
    {
        error = res.error;
        return {};
    }

    const auto doc = QJsonDocument::fromJson(out);
    const auto obj = doc.object();
    MsTokens t;
    t.accessToken = obj.value("access_token").toString();
    t.refreshToken = obj.value("refresh_token").toString(refreshToken);
    t.expiresInSec = obj.value("expires_in").toInt(0);
    if (t.accessToken.isEmpty())
    {
        error = "Invalid refresh response";
    }
    return t;
}

static QString xboxUserToken(HttpClient& http, const QString& msAccessToken, QString& error)
{
    QJsonObject props;
    props["AuthMethod"] = "RPS";
    props["SiteName"] = "user.auth.xboxlive.com";
    props["RpsTicket"] = QString("d=%1").arg(msAccessToken);

    QJsonObject body;
    body["Properties"] = props;
    body["RelyingParty"] = "http://auth.xboxlive.com";
    body["TokenType"] = "JWT";

    QByteArray out;
    const auto res = http.postJson(QUrl("https://user.auth.xboxlive.com/user/authenticate"), QJsonDocument(body).toJson(QJsonDocument::Compact), out,
                                  {
                                      {"Accept", "application/json"},
                                  });
    if (!res.ok)
    {
        error = res.error;
        return {};
    }
    const auto doc = QJsonDocument::fromJson(out);
    return doc.object().value("Token").toString();
}

struct XstsResult final
{
    QString token;
    QString uhs;
    QString xuid;
};

static XstsResult xboxXsts(HttpClient& http, const QString& userToken, QString& error)
{
    QJsonObject props;
    props["SandboxId"] = "RETAIL";
    props["UserTokens"] = QJsonArray{userToken};

    QJsonObject body;
    body["Properties"] = props;
    body["RelyingParty"] = "rp://api.minecraftservices.com/";
    body["TokenType"] = "JWT";

    QByteArray out;
    const auto res = http.postJson(QUrl("https://xsts.auth.xboxlive.com/xsts/authorize"), QJsonDocument(body).toJson(QJsonDocument::Compact), out,
                                  {
                                      {"Accept", "application/json"},
                                  });
    if (!res.ok)
    {
        error = res.error;
        return {};
    }
    const auto doc = QJsonDocument::fromJson(out);
    const auto obj = doc.object();
    XstsResult r;
    r.token = obj.value("Token").toString();
    const auto xui = obj.value("DisplayClaims").toObject().value("xui").toArray();
    if (!xui.isEmpty())
    {
        const auto o = xui.first().toObject();
        r.uhs = o.value("uhs").toString();
        r.xuid = o.value("xid").toString();
    }
    if (r.token.isEmpty() || r.uhs.isEmpty())
    {
        error = "Invalid XSTS response";
    }
    return r;
}

static MsTokens minecraftLogin(HttpClient& http, const XstsResult& xsts, QString& error)
{
    QJsonObject body;
    body["identityToken"] = QString("XBL3.0 x=%1;%2").arg(xsts.uhs, xsts.token);

    QByteArray out;
    const auto res =
        http.postJson(QUrl("https://api.minecraftservices.com/authentication/login_with_xbox"), QJsonDocument(body).toJson(QJsonDocument::Compact), out,
                      {
                          {"Accept", "application/json"},
                      });
    if (!res.ok)
    {
        error = res.error;
        return {};
    }
    const auto doc = QJsonDocument::fromJson(out);
    const auto obj = doc.object();
    MsTokens t;
    t.accessToken = obj.value("access_token").toString();
    t.expiresInSec = obj.value("expires_in").toInt(0);
    if (t.accessToken.isEmpty())
    {
        error = "Invalid minecraft token response";
    }
    return t;
}

static Account minecraftProfile(HttpClient& http, const MsTokens& mc, const QString& msRefresh, const XstsResult& xsts, QString& error)
{
    QByteArray out;
    const auto res = http.getWithHeaders(QUrl("https://api.minecraftservices.com/minecraft/profile"),
                                         out,
                                         {
                                             {"Authorization", QByteArray("Bearer ") + mc.accessToken.toUtf8()},
                                             {"Accept", "application/json"},
                                         });
    if (!res.ok)
    {
        error = res.error;
        return {};
    }
    const auto doc = QJsonDocument::fromJson(out);
    const auto obj = doc.object();
    Account a;
    a.accountId = QString("msa:%1").arg(obj.value("id").toString());
    a.displayName = obj.value("name").toString();
    a.uuid = dashUuid(obj.value("id").toString());
    a.msRefreshToken = msRefresh;
    a.mcAccessToken = mc.accessToken;
    a.mcExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(mc.expiresInSec) * 1000;
    a.xuid = xsts.xuid;
    a.userType = "msa";
    if (a.displayName.isEmpty() || a.uuid.isEmpty())
    {
        error = "Invalid minecraft profile response";
    }
    return a;
}

Account MicrosoftAuth::exchangeForMinecraftAccount(const MsTokens& ms, QString& error)
{
    const auto userToken = xboxUserToken(m_http, ms.accessToken, error);
    if (!error.isEmpty()) return {};
    const auto xsts = xboxXsts(m_http, userToken, error);
    if (!error.isEmpty()) return {};
    const auto mc = minecraftLogin(m_http, xsts, error);
    if (!error.isEmpty()) return {};
    return minecraftProfile(m_http, mc, ms.refreshToken, xsts, error);
}
}
