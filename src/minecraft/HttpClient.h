#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>

namespace blockforge
{
struct HttpResult final
{
    bool ok = false;
    int status = 0;
    QString error;
};

class HttpClient final
{
public:
    HttpResult downloadToFile(const QUrl& url, const QString& filePath);
    HttpResult get(const QUrl& url, QByteArray& out);
    HttpResult getWithHeaders(const QUrl& url, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers);
    HttpResult postJson(const QUrl& url, const QByteArray& json, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers = {});
    HttpResult postForm(const QUrl& url, const QList<QPair<QByteArray, QByteArray>>& form, QByteArray& out, const QList<QPair<QByteArray, QByteArray>>& headers = {});
};
}
