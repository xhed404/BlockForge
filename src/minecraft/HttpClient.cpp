#include "minecraft/HttpClient.h"

#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QObject>
#include <QtCore/QSaveFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QHttpMultiPart>
#include <QtCore/QUrlQuery>

namespace blockforge
{
static HttpResult toResult(QNetworkReply* reply)
{
    HttpResult r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r.status >= 400)
    {
        r.ok = false;
        r.error = QString("HTTP %1").arg(r.status);
        return r;
    }
    if (reply->error() != QNetworkReply::NoError)
    {
        r.ok = false;
        r.error = reply->errorString();
        return r;
    }
    r.ok = true;
    return r;
}

HttpResult HttpClient::get(const QUrl& url, QByteArray& out)
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setTransferTimeout(30000);

    QEventLoop loop;
    QNetworkReply* reply = mgr.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto res = toResult(reply);
    if (res.ok)
    {
        out = reply->readAll();
    }
    reply->deleteLater();
    return res;
}

HttpResult HttpClient::getWithHeaders(const QUrl& url, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setTransferTimeout(30000);
    for (const auto& h : headers)
    {
        req.setRawHeader(h.first, h.second);
    }

    QEventLoop loop;
    QNetworkReply* reply = mgr.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto res = toResult(reply);
    if (res.ok)
    {
        out = reply->readAll();
    }
    reply->deleteLater();
    return res;
}

HttpResult HttpClient::downloadToFile(const QUrl& url, const QString& filePath)
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setTransferTimeout(60000);

    QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
    {
        return HttpResult{false, 0, QString("Cannot write %1").arg(filePath)};
    }

    QEventLoop loop;
    QNetworkReply* reply = mgr.get(req);
    QObject::connect(reply, &QNetworkReply::readyRead, [&]() { f.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto res = toResult(reply);
    reply->deleteLater();
    if (!res.ok)
    {
        f.cancelWriting();
        return res;
    }
    if (!f.commit())
    {
        return HttpResult{false, res.status, QString("Cannot commit %1").arg(filePath)};
    }
    return res;
}

HttpResult HttpClient::postJson(const QUrl& url, const QByteArray& json, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(30000);
    for (const auto& h : headers)
    {
        req.setRawHeader(h.first, h.second);
    }

    QEventLoop loop;
    QNetworkReply* reply = mgr.post(req, json);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto res = toResult(reply);
    if (res.ok)
    {
        out = reply->readAll();
    }
    reply->deleteLater();
    return res;
}

HttpResult HttpClient::postForm(const QUrl& url, const QList<QPair<QByteArray, QByteArray>>& form, QByteArray& out,
                               const QList<QPair<QByteArray, QByteArray>>& headers)
{
    QUrlQuery q;
    for (const auto& kv : form)
    {
        q.addQueryItem(QString::fromUtf8(kv.first), QString::fromUtf8(kv.second));
    }
    const auto body = q.query(QUrl::FullyEncoded).toUtf8();

    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setTransferTimeout(30000);
    for (const auto& h : headers)
    {
        req.setRawHeader(h.first, h.second);
    }

    QEventLoop loop;
    QNetworkReply* reply = mgr.post(req, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto res = toResult(reply);
    if (res.ok)
    {
        out = reply->readAll();
    }
    reply->deleteLater();
    return res;
}
}
