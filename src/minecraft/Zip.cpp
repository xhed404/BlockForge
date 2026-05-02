#include "minecraft/Zip.h"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <cstdint>
#include <vector>

#include <zlib.h>

namespace blockforge
{
static std::uint16_t rd16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
static std::uint32_t rd32(const unsigned char* p) { return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

struct Entry final
{
    QString name;
    std::uint16_t method = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
};

static bool hasExcludedPrefix(const QString& name, const QStringList& prefixes)
{
    for (const auto& p : prefixes)
    {
        if (!p.isEmpty() && name.startsWith(p)) return true;
    }
    return false;
}

static ZipExtractResult parseCentralDirectory(QFile& file, std::vector<Entry>& outEntries)
{
    const auto size = file.size();
    const qint64 readSize = std::min<qint64>(size, 66000);
    if (!file.seek(size - readSize))
    {
        return {false, "Cannot seek ZIP"};
    }
    const auto tail = file.read(readSize);
    const auto* data = reinterpret_cast<const unsigned char*>(tail.constData());
    const auto n = tail.size();

    int eocdPos = -1;
    for (int i = n - 22; i >= 0; --i)
    {
        if (rd32(data + i) == 0x06054b50u)
        {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos < 0)
    {
        return {false, "EOCD not found"};
    }

    const auto cdSize = rd32(data + eocdPos + 12);
    const auto cdOffset = rd32(data + eocdPos + 16);

    if (!file.seek(cdOffset))
    {
        return {false, "Cannot seek central directory"};
    }
    QByteArray cd = file.read(cdSize);
    if (cd.size() != static_cast<int>(cdSize))
    {
        return {false, "Cannot read central directory"};
    }

    const auto* p = reinterpret_cast<const unsigned char*>(cd.constData());
    std::size_t pos = 0;
    while (pos + 46 <= static_cast<std::size_t>(cd.size()))
    {
        if (rd32(p + pos) != 0x02014b50u)
        {
            break;
        }

        const auto method = rd16(p + pos + 10);
        const auto compSize = rd32(p + pos + 20);
        const auto uncompSize = rd32(p + pos + 24);
        const auto nameLen = rd16(p + pos + 28);
        const auto extraLen = rd16(p + pos + 30);
        const auto commentLen = rd16(p + pos + 32);
        const auto lhoff = rd32(p + pos + 42);

        const auto nameStart = pos + 46;
        const auto nameEnd = nameStart + nameLen;
        if (nameEnd > static_cast<std::size_t>(cd.size()))
        {
            return {false, "Invalid central directory"};
        }

        Entry e;
        e.method = method;
        e.compressedSize = compSize;
        e.uncompressedSize = uncompSize;
        e.localHeaderOffset = lhoff;
        e.name = QString::fromUtf8(cd.mid(static_cast<int>(nameStart), nameLen));
        outEntries.push_back(std::move(e));

        pos = nameEnd + extraLen + commentLen;
    }

    if (outEntries.empty())
    {
        return {false, "No ZIP entries"};
    }
    return {true, {}};
}

static ZipExtractResult extractEntry(QFile& file, const Entry& e, const QString& destDir)
{
    if (!file.seek(e.localHeaderOffset))
    {
        return {false, "Cannot seek local header"};
    }

    unsigned char hdr[30];
    if (file.read(reinterpret_cast<char*>(hdr), 30) != 30)
    {
        return {false, "Cannot read local header"};
    }
    if (rd32(hdr) != 0x04034b50u)
    {
        return {false, "Bad local header signature"};
    }
    const auto nameLen = rd16(hdr + 26);
    const auto extraLen = rd16(hdr + 28);
    if (!file.seek(file.pos() + nameLen + extraLen))
    {
        return {false, "Cannot seek entry data"};
    }

    QByteArray comp = file.read(static_cast<qint64>(e.compressedSize));
    if (comp.size() != static_cast<int>(e.compressedSize))
    {
        return {false, "Cannot read compressed data"};
    }

    const auto outPath = QDir(destDir).filePath(e.name);
    QFileInfo fi(outPath);
    QDir().mkpath(fi.absolutePath());

    if (e.name.endsWith('/'))
    {
        QDir().mkpath(outPath);
        return {true, {}};
    }

    if (e.method == 0)
    {
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return {false, "Cannot write extracted file"};
        }
        out.write(comp);
        out.flush();
        out.close();
        return {true, {}};
    }

    if (e.method != 8)
    {
        return {false, QString("Unsupported ZIP method %1").arg(e.method)};
    }

    std::vector<unsigned char> outBuf;
    outBuf.resize(e.uncompressedSize);

    z_stream strm{};
    strm.next_in = reinterpret_cast<Bytef*>(comp.data());
    strm.avail_in = static_cast<uInt>(comp.size());
    strm.next_out = reinterpret_cast<Bytef*>(outBuf.data());
    strm.avail_out = static_cast<uInt>(outBuf.size());

    const int rc = inflateInit2(&strm, -MAX_WBITS);
    if (rc != Z_OK)
    {
        return {false, "inflateInit failed"};
    }

    const int rc2 = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (rc2 != Z_STREAM_END)
    {
        return {false, "inflate failed"};
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return {false, "Cannot write extracted file"};
    }
    out.write(reinterpret_cast<const char*>(outBuf.data()), static_cast<qint64>(outBuf.size()));
    out.flush();
    out.close();
    return {true, {}};
}

static ZipExtractResult extractEntryToMemory(QFile& file, const Entry& e, QByteArray& outBytes)
{
    if (!file.seek(e.localHeaderOffset))
    {
        return {false, "Cannot seek local header"};
    }

    unsigned char hdr[30];
    if (file.read(reinterpret_cast<char*>(hdr), 30) != 30)
    {
        return {false, "Cannot read local header"};
    }
    if (rd32(hdr) != 0x04034b50u)
    {
        return {false, "Bad local header signature"};
    }
    const auto nameLen = rd16(hdr + 26);
    const auto extraLen = rd16(hdr + 28);
    if (!file.seek(file.pos() + nameLen + extraLen))
    {
        return {false, "Cannot seek entry data"};
    }

    QByteArray comp = file.read(static_cast<qint64>(e.compressedSize));
    if (comp.size() != static_cast<int>(e.compressedSize))
    {
        return {false, "Cannot read compressed data"};
    }

    if (e.name.endsWith('/'))
    {
        outBytes.clear();
        return {true, {}};
    }

    if (e.method == 0)
    {
        outBytes = comp;
        return {true, {}};
    }

    if (e.method != 8)
    {
        return {false, QString("Unsupported ZIP method %1").arg(e.method)};
    }

    std::vector<unsigned char> outBuf;
    outBuf.resize(e.uncompressedSize);

    z_stream strm{};
    strm.next_in = reinterpret_cast<Bytef*>(comp.data());
    strm.avail_in = static_cast<uInt>(comp.size());
    strm.next_out = reinterpret_cast<Bytef*>(outBuf.data());
    strm.avail_out = static_cast<uInt>(outBuf.size());

    const int rc = inflateInit2(&strm, -MAX_WBITS);
    if (rc != Z_OK)
    {
        return {false, "inflateInit failed"};
    }

    const int rc2 = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (rc2 != Z_STREAM_END)
    {
        return {false, "inflate failed"};
    }

    outBytes = QByteArray(reinterpret_cast<const char*>(outBuf.data()), static_cast<int>(outBuf.size()));
    return {true, {}};
}

ZipExtractResult extractZip(const QString& zipPath, const QString& destDir, const QStringList& excludePrefixes)
{
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {false, QString("Cannot open %1").arg(zipPath)};
    }

    std::vector<Entry> entries;
    auto res = parseCentralDirectory(file, entries);
    if (!res.ok) return res;

    for (const auto& e : entries)
    {
        if (hasExcludedPrefix(e.name, excludePrefixes))
        {
            continue;
        }
        res = extractEntry(file, e, destDir);
        if (!res.ok)
        {
            return res;
        }
    }

    return {true, {}};
}

ZipExtractResult readZipEntry(const QString& zipPath, const QString& entryName, QByteArray& outBytes)
{
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {false, QString("Cannot open %1").arg(zipPath)};
    }

    std::vector<Entry> entries;
    auto res = parseCentralDirectory(file, entries);
    if (!res.ok) return res;

    for (const auto& e : entries)
    {
        if (e.name == entryName)
        {
            return extractEntryToMemory(file, e, outBytes);
        }
    }
    return {false, QString("ZIP entry not found: %1").arg(entryName)};
}
}
