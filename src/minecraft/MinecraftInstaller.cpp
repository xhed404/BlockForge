#include "minecraft/MinecraftInstaller.h"

#include "minecraft/MinecraftManifest.h"
#include "minecraft/Os.h"
#include "minecraft/Zip.h"

#include "core/Settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QSet>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrlQuery>
#include <QtCore/QProcess>
#include <QtCore/QCoreApplication>

namespace blockforge
{
static bool rulesAllow(const QJsonArray& rules, const QJsonObject& features)
{
    if (rules.isEmpty()) return true;

    bool allow = false;
    const auto osName = QString::fromStdString(toString(currentOs()));
    for (const auto& r : rules)
    {
        const auto obj = r.toObject();
        const auto action = obj.value("action").toString("allow");
        const auto os = obj.value("os").toObject();
        if (!os.isEmpty())
        {
            const auto name = os.value("name").toString();
            if (name != osName)
            {
                continue;
            }
        }
        const auto feats = obj.value("features").toObject();
        if (!feats.isEmpty())
        {
            bool ok = true;
            for (auto it = feats.begin(); it != feats.end(); ++it)
            {
                const auto want = it.value().toBool(false);
                const auto have = features.value(it.key()).toBool(false);
                if (have != want)
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
            {
                continue;
            }
        }
        allow = (action == "allow");
    }
    return allow;
}

static QString classpathSeparator()
{
#if defined(_WIN32)
    return ";";
#else
    return ":";
#endif
}

static int majorFromMcVersion(const QString& mc)
{
    const auto parts = mc.split('.');
    if (parts.size() < 2) return 17;
    bool ok = false;
    const int minor = parts[1].toInt(&ok);
    if (!ok) return 17;
    int patch = 0;
    if (parts.size() >= 3) patch = parts[2].toInt(&ok);
    if (!ok) patch = 0;

    if (minor <= 16) return 8;
    if (minor == 17) return 16;
    if (minor <= 20)
    {
        if (minor == 20 && patch >= 5) return 21;
        return 17;
    }
    return 21;
}

static QString bundledJavaPath(int major)
{
    if (!QCoreApplication::instance()) return {};
    const auto appDir = QCoreApplication::applicationDirPath();
#if defined(_WIN32)
    const auto binDir = QDir(appDir).filePath(QString("jre/%1/bin").arg(major));
    const auto java = QDir(binDir).filePath("java.exe");
    if (QFileInfo::exists(java)) return java;
    return {};
#else
    const auto binDir = QDir(appDir).filePath(QString("jre/%1/bin").arg(major));
    const auto java = QDir(binDir).filePath("java");
    if (QFileInfo::exists(java)) return java;
    return {};
#endif
}

static QString resolveJavaForMc(const QString& mcVersion)
{
    const auto configured = appSettings().get("java.path").value_or("");
    if (!configured.empty())
    {
        const auto q = QString::fromStdString(configured).trimmed();
        if (!q.isEmpty()) return q;
    }
    const auto bundled = bundledJavaPath(majorFromMcVersion(mcVersion));
    if (!bundled.isEmpty()) return bundled;
    return "java";
}

struct MavenCoord final
{
    QString group;
    QString artifact;
    QString version;
    QString classifier;
    QString ext;
};

static bool parseMavenCoord(const QString& s, MavenCoord& out)
{
    auto raw = s.trimmed();
    auto ext = QString("jar");
    const auto at = raw.indexOf('@');
    if (at >= 0)
    {
        ext = raw.mid(at + 1).trimmed();
        raw = raw.left(at);
    }

    const auto parts = raw.split(':', Qt::KeepEmptyParts);
    if (parts.size() < 3 || parts.size() > 4)
    {
        return false;
    }

    out.group = parts[0];
    out.artifact = parts[1];
    out.version = parts[2];
    out.classifier = (parts.size() == 4) ? parts[3] : QString();
    out.ext = ext;
    return !out.group.isEmpty() && !out.artifact.isEmpty() && !out.version.isEmpty() && !out.ext.isEmpty();
}

static QString mavenRelPath(const MavenCoord& c)
{
    const auto groupPath = QString(c.group).replace('.', '/');
    const auto base = QString("%1/%2/%3").arg(groupPath, c.artifact, c.version);
    const auto file = c.classifier.isEmpty() ? QString("%1-%2.%3").arg(c.artifact, c.version, c.ext)
                                             : QString("%1-%2-%3.%4").arg(c.artifact, c.version, c.classifier, c.ext);
    return base + "/" + file;
}

static QString chooseRepoUrl(const QString& explicitUrl, const MavenCoord& c)
{
    if (!explicitUrl.trimmed().isEmpty())
    {
        auto u = explicitUrl.trimmed();
        if (!u.endsWith('/')) u += "/";
        return u;
    }

    if (c.group.startsWith("net.minecraftforge") || c.group.startsWith("de.oceanlabs") || c.group.startsWith("net.minecraft"))
    {
        return "https://maven.minecraftforge.net/";
    }

    return "https://libraries.minecraft.net/";
}

static VersionInstallResult ensureMavenDownloaded(HttpClient& http, const QString& libsDir, const MavenCoord& c, const QString& repoUrl)
{
    const auto rel = mavenRelPath(c);
    const auto outPath = QDir(libsDir).filePath(rel);
    if (QFileInfo::exists(outPath))
    {
        return {true, {}};
    }

    const auto url = QUrl(repoUrl + rel);
    const auto dl = http.downloadToFile(url, outPath);
    if (!dl.ok)
    {
        if (dl.status == 404)
        {
            static const QSet<QString> generatedClassifiers = QSet<QString>{
                "client",
                "server",
                "slim",
                "extra",
                "srg",
                "unpacked",
                "mappings",
                "mappings-merged",
            };
            if (c.ext != "jar" || (!c.classifier.isEmpty() && generatedClassifiers.contains(c.classifier)))
            {
                return {true, {}};
            }
        }
        return {false, QString("Library download failed: %1 (%2)").arg(dl.error, url.toString())};
    }
    return {true, {}};
}

MinecraftInstaller::MinecraftInstaller(QString dataDir)
    : m_dataDir(std::move(dataDir))
{
}

static QJsonObject mergeJsonObjects(const QJsonObject& base, const QJsonObject& overlay)
{
    QJsonObject out = base;
    for (auto it = overlay.begin(); it != overlay.end(); ++it)
    {
        const auto k = it.key();
        if (k == "libraries")
        {
            const auto a = base.value("libraries").toArray();
            const auto b = overlay.value("libraries").toArray();
            QJsonArray merged = a;
            for (const auto& v : b) merged.push_back(v);
            out["libraries"] = merged;
            continue;
        }
        if (k == "arguments")
        {
            const auto a = base.value("arguments").toObject();
            const auto b = overlay.value("arguments").toObject();
            if (a.isEmpty())
            {
                out["arguments"] = b;
                continue;
            }
            if (b.isEmpty())
            {
                out["arguments"] = a;
                continue;
            }

            QJsonObject merged = a;
            const auto aj = a.value("jvm").toArray();
            const auto bj = b.value("jvm").toArray();
            QJsonArray mj = aj;
            for (const auto& v : bj) mj.push_back(v);
            merged["jvm"] = mj;

            const auto ag = a.value("game").toArray();
            const auto bg = b.value("game").toArray();
            QJsonArray mg = ag;
            for (const auto& v : bg) mg.push_back(v);
            merged["game"] = mg;

            out["arguments"] = merged;
            continue;
        }
        out[k] = it.value();
    }
    return out;
}

static QJsonObject resolveVersionObjectRec(const QString& versionsDir, const QString& versionId, QString& error, QSet<QString>& visiting)
{
    if (visiting.contains(versionId))
    {
        error = "Version inheritance cycle";
        return {};
    }
    visiting.insert(versionId);

    const auto versionDir = QDir(versionsDir).filePath(versionId);
    const auto jsonPath = QDir(versionDir).filePath(versionId + ".json");
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
    {
        error = QString("Cannot read %1").arg(jsonPath);
        return {};
    }
    const auto bytes = f.readAll();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (doc.isNull() || !doc.isObject())
    {
        error = QString("Invalid JSON %1").arg(jsonPath);
        return {};
    }

    const auto root = doc.object();
    const auto parentId = root.value("inheritsFrom").toString();
    if (parentId.isEmpty())
    {
        QJsonObject out = root;
        out["id"] = versionId;
        visiting.remove(versionId);
        return out;
    }

    const auto parent = resolveVersionObjectRec(versionsDir, parentId, error, visiting);
    if (!error.isEmpty())
    {
        return {};
    }

    auto merged = mergeJsonObjects(parent, root);
    merged["id"] = versionId;
    merged["inheritsFrom"] = parentId;
    if (!merged.contains("jar"))
    {
        merged["jar"] = parentId;
    }
    visiting.remove(versionId);
    return merged;
}

QJsonObject MinecraftInstaller::resolveVersionObject(const QString& versionId, QString& error) const
{
    QSet<QString> visiting;
    return resolveVersionObjectRec(versionsDir(), versionId, error, visiting);
}

QString MinecraftInstaller::versionsDir() const
{
    return QDir(m_dataDir).filePath("versions");
}

QString MinecraftInstaller::librariesDir() const
{
    return QDir(m_dataDir).filePath("libraries");
}

QString MinecraftInstaller::assetsDir() const
{
    return QDir(m_dataDir).filePath("assets");
}

QString MinecraftInstaller::sha1OfFile(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

QJsonDocument MinecraftInstaller::loadJsonFile(const QString& path, QString& error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        error = QString("Cannot read %1").arg(path);
        return {};
    }
    const auto bytes = f.readAll();
    auto doc = QJsonDocument::fromJson(bytes);
    if (doc.isNull())
    {
        error = QString("Invalid JSON %1").arg(path);
    }
    return doc;
}

VersionInstallResult MinecraftInstaller::saveJsonFile(const QString& path, const QByteArray& bytes)
{
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return {false, QString("Cannot write %1").arg(path)};
    }
    f.write(bytes);
    f.flush();
    f.close();
    return {true, {}};
}

VersionInstallResult MinecraftInstaller::ensureManifest()
{
    const auto manifestPath = QDir(versionsDir()).filePath("manifest.json");
    if (QFileInfo::exists(manifestPath))
    {
        return {true, {}};
    }
    const auto url = MinecraftManifest::defaultManifestUrl();
    const auto res = m_http.downloadToFile(url, manifestPath);
    if (!res.ok)
    {
        return {false, QString("Manifest download failed: %1").arg(res.error)};
    }
    return {true, {}};
}

VersionMeta MinecraftInstaller::readVersionMeta(const QString& versionId, QString& error)
{
    const auto root = resolveVersionObject(versionId, error);
    if (!error.isEmpty()) return {};

    VersionMeta meta;
    meta.id = versionId;
    meta.jarId = root.value("jar").toString(versionId);
    meta.mainClass = root.value("mainClass").toString();
    meta.raw = QJsonDocument(root);

    const auto javaVersion = root.value("javaVersion").toObject();
    if (!javaVersion.isEmpty())
    {
        meta.javaMajor = javaVersion.value("majorVersion").toInt(8);
    }
    else
    {
        const auto base = root.value("inheritsFrom").toString(versionId);
        meta.javaMajor = majorFromMcVersion(base);
    }

    const auto assetIndex = root.value("assetIndex").toObject();
    meta.assetsId = assetIndex.value("id").toString();
    meta.assetIndexUrl = QUrl(assetIndex.value("url").toString());

    const auto downloads = root.value("downloads").toObject();
    const auto client = downloads.value("client").toObject();
    meta.clientUrl = QUrl(client.value("url").toString());
    meta.clientSha1 = client.value("sha1").toString();

    if (meta.mainClass.isEmpty() || !meta.clientUrl.isValid() || !meta.assetIndexUrl.isValid())
    {
        error = "Incomplete version metadata";
        return {};
    }

    return meta;
}

VersionInstallResult MinecraftInstaller::installVersion(const QString& versionId)
{
    const auto versionDir = QDir(versionsDir()).filePath(versionId);
    const auto jsonPath = QDir(versionDir).filePath(versionId + ".json");
    if (!QFileInfo::exists(jsonPath))
    {
        auto res = ensureManifest();
        if (!res.ok) return res;

        QString error;
        const auto manifestDoc = loadJsonFile(QDir(versionsDir()).filePath("manifest.json"), error);
        if (!error.isEmpty())
        {
            return {false, error};
        }

        const MinecraftManifest manifest(manifestDoc);
        const auto versionUrl = manifest.versionUrl(versionId);
        if (!versionUrl.isValid())
        {
            return {false, QString("Version not found in manifest: %1").arg(versionId)};
        }
        const auto dl = m_http.downloadToFile(versionUrl, jsonPath);
        if (!dl.ok)
        {
            return {false, QString("Version JSON download failed: %1").arg(dl.error)};
        }
    }

    QString error;
    const auto meta = readVersionMeta(versionId, error);
    if (!error.isEmpty())
    {
        return {false, error};
    }

    const auto jarDir = QDir(versionsDir()).filePath(meta.jarId);
    const auto jarPath = QDir(jarDir).filePath(meta.jarId + ".jar");
    if (!QFileInfo::exists(jarPath) || (!meta.clientSha1.isEmpty() && sha1OfFile(jarPath) != meta.clientSha1))
    {
        const auto dl = m_http.downloadToFile(meta.clientUrl, jarPath);
        if (!dl.ok)
        {
            return {false, QString("Client download failed: %1").arg(dl.error)};
        }
        if (!meta.clientSha1.isEmpty() && sha1OfFile(jarPath) != meta.clientSha1)
        {
            QFile::remove(jarPath);
            return {false, "Client SHA1 mismatch"};
        }
    }

    const auto assetIndexPath = QDir(assetsDir()).filePath(QString("indexes/%1.json").arg(meta.assetsId));
    if (!QFileInfo::exists(assetIndexPath))
    {
        const auto dl = m_http.downloadToFile(meta.assetIndexUrl, assetIndexPath);
        if (!dl.ok)
        {
            return {false, QString("Asset index download failed: %1").arg(dl.error)};
        }
    }

    const auto libsDir = librariesDir();
    const auto libs = meta.raw.object().value("libraries").toArray();
    const QJsonObject features;
    for (const auto& lib : libs)
    {
        const auto libObj = lib.toObject();
        if (!rulesAllow(libObj.value("rules").toArray(), features))
        {
            continue;
        }

        const auto downloads = libObj.value("downloads").toObject();
        const auto artifact = downloads.value("artifact").toObject();
        if (!artifact.isEmpty())
        {
            const auto path = artifact.value("path").toString();
            const auto urlStr = artifact.value("url").toString();
            const auto sha1 = artifact.value("sha1").toString();
            if (path.isEmpty() || urlStr.isEmpty())
            {
                continue;
            }

            const auto outPath = QDir(libsDir).filePath(path);
            if (QFileInfo::exists(outPath) && (sha1.isEmpty() || sha1OfFile(outPath) == sha1))
            {
                continue;
            }

            const auto dl = m_http.downloadToFile(QUrl(urlStr), outPath);
            if (!dl.ok)
            {
                return {false, QString("Library download failed: %1").arg(dl.error)};
            }
            if (!sha1.isEmpty() && sha1OfFile(outPath) != sha1)
            {
                QFile::remove(outPath);
                return {false, QString("Library SHA1 mismatch: %1").arg(path)};
            }
            continue;
        }

        const auto name = libObj.value("name").toString();
        if (name.isEmpty())
        {
            continue;
        }
        MavenCoord c;
        if (!parseMavenCoord(name, c))
        {
            continue;
        }
        const auto repo = chooseRepoUrl(libObj.value("url").toString(), c);
        const auto r = ensureMavenDownloaded(m_http, libsDir, c, repo);
        if (!r.ok)
        {
            return r;
        }
    }

    return {true, {}};
}

VersionInstallResult MinecraftInstaller::installFabric(const QString& minecraftVersion, const QString& loaderVersion, QString& outVersionId)
{
    QString loader = loaderVersion;
    if (loader.trimmed().isEmpty())
    {
        QByteArray out;
        const auto res = m_http.get(QUrl(QString("https://meta.fabricmc.net/v2/versions/loader/%1").arg(minecraftVersion)), out);
        if (!res.ok)
        {
            return {false, QString("Fabric loader list failed: %1").arg(res.error)};
        }
        const auto doc = QJsonDocument::fromJson(out);
        const auto arr = doc.array();
        if (arr.isEmpty())
        {
            return {false, "No Fabric loaders for this Minecraft version"};
        }
        const auto obj = arr.first().toObject();
        loader = obj.value("loader").toObject().value("version").toString();
        if (loader.isEmpty())
        {
            return {false, "Invalid Fabric loader response"};
        }
    }

    outVersionId = QString("fabric-loader-%1-%2").arg(loader, minecraftVersion);
    const auto versionDir = QDir(versionsDir()).filePath(outVersionId);
    const auto jsonPath = QDir(versionDir).filePath(outVersionId + ".json");

    if (!QFileInfo::exists(jsonPath))
    {
        QByteArray out;
        const auto url = QUrl(QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json").arg(minecraftVersion, loader));
        const auto res = m_http.get(url, out);
        if (!res.ok)
        {
            return {false, QString("Fabric profile download failed: %1").arg(res.error)};
        }
        const auto save = saveJsonFile(jsonPath, out);
        if (!save.ok)
        {
            return save;
        }
    }
    return {true, {}};
}

static QString normalizeForgeFullVersion(const QString& mc, const QString& forgeVersion)
{
    const auto v = forgeVersion.trimmed();
    if (v.isEmpty()) return {};
    if (v.startsWith(mc + "-")) return v;
    if (v.contains('-')) return v;
    return mc + "-" + v;
}

static QString pickForgeFromPromotions(const QString& mc, const QByteArray& json, QString& error)
{
    const auto doc = QJsonDocument::fromJson(json);
    const auto root = doc.object();
    const auto promos = root.value("promos").toObject();
    const auto recommendedKey = mc + "-recommended";
    const auto latestKey = mc + "-latest";
    QString v = promos.value(recommendedKey).toString();
    if (v.isEmpty()) v = promos.value(latestKey).toString();
    if (v.isEmpty())
    {
        error = "No Forge promotion found for this Minecraft version";
        return {};
    }
    return v;
}

static QString findForgeInstalledVersionId(const QString& versionsDir, const QString& mc, const QString& forgeShort)
{
    QDir dir(versionsDir);
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& e : entries)
    {
        const auto lc = e.toLower();
        if (!lc.contains("forge")) continue;
        if (!e.contains(mc)) continue;
        if (!forgeShort.isEmpty() && !e.contains(forgeShort)) continue;
        const auto jsonPath = QDir(dir.filePath(e)).filePath(e + ".json");
        if (!QFileInfo::exists(jsonPath)) continue;
        return e;
    }
    return {};
}

static QString readMainClassFromJar(const QString& jarPath, QString& error)
{
    QByteArray bytes;
    const auto r = readZipEntry(jarPath, "META-INF/MANIFEST.MF", bytes);
    if (!r.ok)
    {
        error = r.error;
        return {};
    }

    const auto text = QString::fromUtf8(bytes);
    const auto lines = text.split('\n');
    QString currentKey;
    QString currentVal;
    QMap<QString, QString> kv;
    for (auto line : lines)
    {
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith(' ') && !currentKey.isEmpty())
        {
            currentVal += line.mid(1);
            continue;
        }
        if (line.trimmed().isEmpty())
        {
            if (!currentKey.isEmpty())
            {
                kv[currentKey] = currentVal.trimmed();
                currentKey.clear();
                currentVal.clear();
            }
            continue;
        }
        const auto pos = line.indexOf(':');
        if (pos <= 0)
        {
            continue;
        }
        if (!currentKey.isEmpty())
        {
            kv[currentKey] = currentVal.trimmed();
        }
        currentKey = line.left(pos).trimmed();
        currentVal = line.mid(pos + 1).trimmed();
    }
    if (!currentKey.isEmpty()) kv[currentKey] = currentVal.trimmed();

    const auto mc = kv.value("Main-Class");
    if (mc.isEmpty())
    {
        error = "Main-Class not found";
        return {};
    }
    return mc;
}

static QString stripSingleQuotes(const QString& s)
{
    auto t = s.trimmed();
    if (t.size() >= 2 && t.startsWith('\'') && t.endsWith('\''))
    {
        t = t.mid(1, t.size() - 2);
    }
    return t;
}

static bool isBracketValue(const QString& s)
{
    const auto t = s.trimmed();
    return t.startsWith('[') && t.endsWith(']');
}

static QString stripBrackets(const QString& s)
{
    const auto t = s.trimmed();
    return t.mid(1, t.size() - 2);
}

VersionInstallResult MinecraftInstaller::installForge(const QString& minecraftVersion, const QString& forgeVersion, QString& outVersionId)
{
    QString forgeShort = forgeVersion.trimmed();
    QString full = normalizeForgeFullVersion(minecraftVersion, forgeShort);
    if (full.isEmpty())
    {
        QByteArray out;
        const auto res = m_http.get(QUrl("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"), out);
        if (!res.ok)
        {
            return {false, QString("Forge promotions failed: %1").arg(res.error)};
        }
        QString e;
        forgeShort = pickForgeFromPromotions(minecraftVersion, out, e);
        if (!e.isEmpty())
        {
            return {false, e};
        }
        full = minecraftVersion + "-" + forgeShort;
    }
    else
    {
        if (full.startsWith(minecraftVersion + "-"))
        {
            forgeShort = full.mid(minecraftVersion.size() + 1);
        }
    }

    const auto installerUrl =
        QUrl(QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(full));

    const auto installersDir = QDir(m_dataDir).filePath("installers");
    QDir().mkpath(installersDir);
    const auto jarPath = QDir(installersDir).filePath(QString("forge-%1-installer.jar").arg(full));
    if (!QFileInfo::exists(jarPath))
    {
        const auto dl = m_http.downloadToFile(installerUrl, jarPath);
        if (!dl.ok)
        {
            return {false, QString("Forge installer download failed: %1").arg(dl.error)};
        }
    }

    QByteArray versionBytes;
    auto zr = readZipEntry(jarPath, "version.json", versionBytes);
    if (!zr.ok)
    {
        return {false, QString("Forge installer missing version.json: %1").arg(zr.error)};
    }

    const auto doc = QJsonDocument::fromJson(versionBytes);
    if (doc.isNull() || !doc.isObject())
    {
        return {false, "Forge version.json is invalid"};
    }

    const auto id = doc.object().value("id").toString();
    if (id.isEmpty())
    {
        return {false, "Forge version id missing"};
    }

    outVersionId = id;
    const auto versionDir = QDir(versionsDir()).filePath(outVersionId);
    const auto jsonOut = QDir(versionDir).filePath(outVersionId + ".json");
    const auto save = saveJsonFile(jsonOut, versionBytes);
    if (!save.ok)
    {
        return save;
    }

    QByteArray profileBytes;
    zr = readZipEntry(jarPath, "install_profile.json", profileBytes);
    if (zr.ok)
    {
        const auto profileDoc = QJsonDocument::fromJson(profileBytes);
        const auto profile = profileDoc.object();

        QString err;
        const auto base = installVersion(minecraftVersion);
        if (!base.ok)
        {
            return base;
        }

        const auto libsDir = librariesDir();
        const auto rootDir = m_dataDir;
        const auto mcJar = QDir(QDir(versionsDir()).filePath(minecraftVersion)).filePath(minecraftVersion + ".jar");

        QString matError;
        auto materialize = [&](const QString& raw, const QString& side, const QJsonObject& dataObj) -> QString {
            auto v = raw;
            if (v.startsWith('{') && v.endsWith('}'))
            {
                const auto key = v.mid(1, v.size() - 2);
                if (dataObj.contains(key))
                {
                    const auto d = dataObj.value(key).toObject();
                    if (!d.isEmpty())
                    {
                        v = d.value(side).toString();
                    }
                    else
                    {
                        v = dataObj.value(key).toString();
                    }
                }
            }

            v = stripSingleQuotes(v);

            if (isBracketValue(v))
            {
                MavenCoord c;
                if (parseMavenCoord(stripBrackets(v), c))
                {
                    const auto repo = chooseRepoUrl("https://maven.minecraftforge.net/", c);
                    const auto r = ensureMavenDownloaded(m_http, libsDir, c, repo);
                    if (!r.ok)
                    {
                        matError = r.error;
                        return {};
                    }
                    return QDir(libsDir).filePath(mavenRelPath(c));
                }
            }

            if (v.startsWith("/data/"))
            {
                const auto entry = v.mid(1);
                QByteArray data;
                const auto rr = readZipEntry(jarPath, entry, data);
                if (rr.ok)
                {
                    const auto outDir = QDir(m_dataDir).filePath(QString("forge/%1").arg(outVersionId));
                    const auto outPath = QDir(outDir).filePath(entry);
                    QFileInfo fi(outPath);
                    QDir().mkpath(fi.absolutePath());
                    QFile f(outPath);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    {
                        f.write(data);
                        f.flush();
                        f.close();
                    }
                    return outPath;
                }
                matError = rr.error;
                return {};
            }

            v.replace("{ROOT}", rootDir);
            v.replace("{LIBRARY_DIR}", libsDir);
            v.replace("{MINECRAFT_VERSION}", minecraftVersion);
            v.replace("{SIDE}", side);
            v.replace("{INSTALLER}", jarPath);
            v.replace("{MINECRAFT_JAR}", mcJar);
            return v;
        };

        const auto dataObj = profile.value("data").toObject();
        const auto processors = profile.value("processors").toArray();
        const auto side = QString("client");

        const auto patched = materialize("{PATCHED}", side, dataObj);
        if (!matError.isEmpty())
        {
            return {false, matError};
        }
        if (!patched.isEmpty() && QFileInfo::exists(patched))
        {
            return {true, {}};
        }

        for (const auto& p : processors)
        {
            const auto obj = p.toObject();
            const auto sidesArr = obj.value("sides").toArray();
            if (!sidesArr.isEmpty())
            {
                bool okSide = false;
                for (const auto& s : sidesArr)
                {
                    if (s.toString() == side)
                    {
                        okSide = true;
                        break;
                    }
                }
                if (!okSide)
                {
                    continue;
                }
            }

            MavenCoord jarCoord;
            if (!parseMavenCoord(obj.value("jar").toString(), jarCoord))
            {
                continue;
            }
            const auto repo = chooseRepoUrl("https://maven.minecraftforge.net/", jarCoord);
            auto r = ensureMavenDownloaded(m_http, libsDir, jarCoord, repo);
            if (!r.ok) return r;
            const auto toolJarPath = QDir(libsDir).filePath(mavenRelPath(jarCoord));

            QStringList cp;
            cp.push_back(toolJarPath);
            const auto cpArr = obj.value("classpath").toArray();
            for (const auto& v : cpArr)
            {
                MavenCoord c;
                if (!parseMavenCoord(v.toString(), c))
                {
                    continue;
                }
                const auto rr = ensureMavenDownloaded(m_http, libsDir, c, chooseRepoUrl("https://maven.minecraftforge.net/", c));
                if (!rr.ok) return rr;
                cp.push_back(QDir(libsDir).filePath(mavenRelPath(c)));
            }

            QString mainErr;
            const auto mainClass = readMainClassFromJar(toolJarPath, mainErr);
            if (mainClass.isEmpty())
            {
                return {false, QString("Forge processor main class failed: %1").arg(mainErr)};
            }

            QStringList args;
            args << "-Djava.net.preferIPv4Stack=true"
                 << "-Djava.net.preferIPv4Addresses=true"
                 << "-Dsun.net.client.defaultConnectTimeout=15000"
                 << "-Dsun.net.client.defaultReadTimeout=30000";
            args << "-cp" << cp.join(classpathSeparator()) << mainClass;

            const auto argsArr = obj.value("args").toArray();
            for (const auto& a : argsArr)
            {
                auto s = a.toString();
                for (auto it = dataObj.begin(); it != dataObj.end(); ++it)
                {
                    const auto key = it.key();
                    s.replace("{" + key + "}", materialize("{" + key + "}", side, dataObj));
                    if (!matError.isEmpty())
                    {
                        return {false, matError};
                    }
                }
                s = materialize(s, side, dataObj);
                if (!matError.isEmpty())
                {
                    return {false, matError};
                }
                if (!s.isEmpty()) args.push_back(s);
            }

            QProcess proc;
            proc.setWorkingDirectory(rootDir);
            proc.setProgram(resolveJavaForMc(minecraftVersion));
            proc.setArguments(args);
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start();
            if (!proc.waitForStarted(15000))
            {
                return {false, "Forge processor failed to start"};
            }
            proc.waitForFinished(-1);
            if (proc.exitCode() != 0)
            {
                const auto out = QString::fromUtf8(proc.readAll());
                return {false, QString("Forge processor failed: %1").arg(out.left(2000))};
            }
        }
    }

    return {true, {}};
}

VersionInstallResult MinecraftInstaller::downloadAssets(const QString& versionId, int limit)
{
    QString error;
    const auto meta = readVersionMeta(versionId, error);
    if (!error.isEmpty())
    {
        return {false, error};
    }

    const auto assetIndexPath = QDir(assetsDir()).filePath(QString("indexes/%1.json").arg(meta.assetsId));
    auto doc = loadJsonFile(assetIndexPath, error);
    if (!error.isEmpty())
    {
        return {false, error};
    }

    const auto objects = doc.object().value("objects").toObject();
    int done = 0;
    for (auto it = objects.begin(); it != objects.end(); ++it)
    {
        if (limit > 0 && done >= limit)
        {
            break;
        }
        const auto obj = it.value().toObject();
        const auto hash = obj.value("hash").toString();
        if (hash.size() < 2)
        {
            continue;
        }
        const auto prefix = hash.left(2);
        const auto outPath = QDir(assetsDir()).filePath(QString("objects/%1/%2").arg(prefix, hash));
        if (QFileInfo::exists(outPath) && sha1OfFile(outPath) == hash)
        {
            ++done;
            continue;
        }
        const auto url = QUrl(QString("https://resources.download.minecraft.net/%1/%2").arg(prefix, hash));
        const auto dl = m_http.downloadToFile(url, outPath);
        if (!dl.ok)
        {
            return {false, QString("Asset download failed: %1").arg(dl.error)};
        }
        if (sha1OfFile(outPath) != hash)
        {
            QFile::remove(outPath);
            return {false, QString("Asset SHA1 mismatch: %1").arg(hash)};
        }
        ++done;
    }

    return {true, {}};
}

VersionInstallResult MinecraftInstaller::prepareNatives(const QString& versionId, const QString& instanceId)
{
    QString error;
    const auto meta = readVersionMeta(versionId, error);
    if (!error.isEmpty())
    {
        return {false, error};
    }

    const auto osName = QString::fromStdString(toString(currentOs()));
    const auto arch = QString::fromStdString(currentArch());

    const auto nativesDir = QDir(m_dataDir).filePath(QString("instances/%1/natives").arg(instanceId));
    QDir().mkpath(nativesDir);

    const auto libsDir = librariesDir();
    const auto libs = meta.raw.object().value("libraries").toArray();
    const QJsonObject features;
    for (const auto& lib : libs)
    {
        const auto libObj = lib.toObject();
        if (!rulesAllow(libObj.value("rules").toArray(), features))
        {
            continue;
        }

        QStringList excludes;
        const auto extract = libObj.value("extract").toObject();
        const auto excludeArr = extract.value("exclude").toArray();
        for (const auto& ex : excludeArr)
        {
            if (ex.isString()) excludes.push_back(ex.toString());
        }
        if (excludes.empty())
        {
            excludes.push_back("META-INF/");
        }

        const auto natives = libObj.value("natives").toObject();
        const auto downloads = libObj.value("downloads").toObject();
        if (!natives.isEmpty())
        {
            auto classifier = natives.value(osName).toString();
            if (classifier.isEmpty())
            {
                continue;
            }
            classifier.replace("${arch}", arch);

            const auto classifiers = downloads.value("classifiers").toObject();
            const auto nat = classifiers.value(classifier).toObject();
            if (nat.isEmpty())
            {
                continue;
            }
            const auto path = nat.value("path").toString();
            const auto urlStr = nat.value("url").toString();
            const auto sha1 = nat.value("sha1").toString();
            if (path.isEmpty() || urlStr.isEmpty())
            {
                continue;
            }

            const auto jarPath = QDir(libsDir).filePath(path);
            if (!QFileInfo::exists(jarPath) || (!sha1.isEmpty() && sha1OfFile(jarPath) != sha1))
            {
                const auto dl = m_http.downloadToFile(QUrl(urlStr), jarPath);
                if (!dl.ok)
                {
                    return {false, QString("Native download failed: %1").arg(dl.error)};
                }
                if (!sha1.isEmpty() && sha1OfFile(jarPath) != sha1)
                {
                    QFile::remove(jarPath);
                    return {false, QString("Native SHA1 mismatch: %1").arg(path)};
                }
            }

            const auto exRes = extractZip(jarPath, nativesDir, excludes);
            if (!exRes.ok)
            {
                return {false, QString("Native extract failed: %1").arg(exRes.error)};
            }
            continue;
        }

        const auto artifact = downloads.value("artifact").toObject();
        if (artifact.isEmpty())
        {
            continue;
        }
        const auto path = artifact.value("path").toString();
        const auto urlStr = artifact.value("url").toString();
        const auto sha1 = artifact.value("sha1").toString();
        if (path.isEmpty() || urlStr.isEmpty())
        {
            continue;
        }
        if (!path.contains(QString("natives-%1").arg(osName)))
        {
            continue;
        }

        const auto jarPath = QDir(libsDir).filePath(path);
        if (!QFileInfo::exists(jarPath) || (!sha1.isEmpty() && sha1OfFile(jarPath) != sha1))
        {
            const auto dl = m_http.downloadToFile(QUrl(urlStr), jarPath);
            if (!dl.ok)
            {
                return {false, QString("Native download failed: %1").arg(dl.error)};
            }
            if (!sha1.isEmpty() && sha1OfFile(jarPath) != sha1)
            {
                QFile::remove(jarPath);
                return {false, QString("Native SHA1 mismatch: %1").arg(path)};
            }
        }

        const auto exRes = extractZip(jarPath, nativesDir, excludes);
        if (!exRes.ok)
        {
            return {false, QString("Native extract failed: %1").arg(exRes.error)};
        }
    }

    return {true, {}};
}
}
