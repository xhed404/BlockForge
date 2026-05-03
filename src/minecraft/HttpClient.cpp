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
#include <QtCore/QThread>

namespace blockforge
{
static QNetworkAccessManager& sharedManager()
{
    static thread_local QNetworkAccessManager mgr;
    return mgr;
}

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

static bool isRetryable(QNetworkReply* reply, int status)
{
    if (status == 0)
    {
        const auto e = reply->error();
        return e != QNetworkReply::NoError;
    }
    if (status >= 500 && status <= 599)
    {
        return true;
    }
    if (status == 408 || status == 429)
    {
        return true;
    }
    const auto e = reply->error();
    switch (e)
    {
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::UnknownProxyError:
        case QNetworkReply::UnknownServerError:
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
            return true;
        default:
            break;
    }
    return false;
}

HttpResult HttpClient::get(const QUrl& url, QByteArray& out)
{
    auto& mgr = sharedManager();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setTransferTimeout(30000);

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        QEventLoop loop;
        QNetworkReply* reply = mgr.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const auto res = toResult(reply);
        const bool retry = !res.ok && attempt < 3 && isRetryable(reply, res.status);
        if (res.ok)
        {
            out = reply->readAll();
        }
        reply->deleteLater();
        if (!retry)
        {
            return res;
        }
        QThread::msleep(static_cast<unsigned long>(250 * attempt));
    }
    return {false, 0, "Retry exhausted"};
}

HttpResult HttpClient::getWithHeaders(const QUrl& url, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    auto& mgr = sharedManager();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setTransferTimeout(30000);
    for (const auto& h : headers)
    {
        req.setRawHeader(h.first, h.second);
    }

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        QEventLoop loop;
        QNetworkReply* reply = mgr.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const auto res = toResult(reply);
        const bool retry = !res.ok && attempt < 3 && isRetryable(reply, res.status);
        if (res.ok)
        {
            out = reply->readAll();
        }
        reply->deleteLater();
        if (!retry)
        {
            return res;
        }
        QThread::msleep(static_cast<unsigned long>(250 * attempt));
    }
    return {false, 0, "Retry exhausted"};
}

HttpResult HttpClient::downloadToFile(const QUrl& url, const QString& filePath)
{
    auto& mgr = sharedManager();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setTransferTimeout(60000);

    QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
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
        const bool retry = !res.ok && attempt < 3 && isRetryable(reply, res.status);
        reply->deleteLater();

        if (!res.ok)
        {
            f.cancelWriting();
            if (!retry)
            {
                return res;
            }
            QThread::msleep(static_cast<unsigned long>(400 * attempt));
            continue;
        }
        if (!f.commit())
        {
            return HttpResult{false, res.status, QString("Cannot commit %1").arg(filePath)};
        }
        return res;
    }
    return {false, 0, "Retry exhausted"};
}

HttpResult HttpClient::postJson(const QUrl& url, const QByteArray& json, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    auto& mgr = sharedManager();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(30000);
    for (const auto& h : headers)
    {
        req.setRawHeader(h.first, h.second);
    }

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        QEventLoop loop;
        QNetworkReply* reply = mgr.post(req, json);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const auto res = toResult(reply);
        const bool retry = !res.ok && attempt < 3 && isRetryable(reply, res.status);
        if (res.ok)
        {
            out = reply->readAll();
        }
        reply->deleteLater();
        if (!retry)
        {
            return res;
        }
        QThread::msleep(static_cast<unsigned long>(250 * attempt));
    }
    return {false, 0, "Retry exhausted"};
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

    auto& mgr = sharedManager();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockForge");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setTransferTimeout(30000);
    for (const auto& h : headers)
    {
        req.setRawHeader(h.first, h.second);
    }

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        QEventLoop loop;
        QNetworkReply* reply = mgr.post(req, body);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const auto res = toResult(reply);
        const bool retry = !res.ok && attempt < 3 && isRetryable(reply, res.status);
        if (res.ok)
        {
            out = reply->readAll();
        }
        reply->deleteLater();
        if (!retry)
        {
            return res;
        }
        QThread::msleep(static_cast<unsigned long>(250 * attempt));
    }
    return {false, 0, "Retry exhausted"};
}
}
