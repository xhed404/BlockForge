#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QByteArray>

namespace blockforge
{
struct ZipExtractResult final
{
    bool ok = false;
    QString error;
};

ZipExtractResult extractZip(const QString& zipPath, const QString& destDir, const QStringList& excludePrefixes);
ZipExtractResult readZipEntry(const QString& zipPath, const QString& entryName, QByteArray& outBytes);
}
