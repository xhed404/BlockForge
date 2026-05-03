#include "minecraft/MinecraftLauncher.h"

#include "core/Settings.h"
#include "minecraft/Os.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QSet>

namespace blockforge
{
static QString classpathSeparator()
{
#if defined(_WIN32)
    return ";";
#else
    return ":";
#endif
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

static QString bundledJavaPath(int major, bool preferGui)
{
    if (!QCoreApplication::instance()) return {};
    const auto appDir = QCoreApplication::applicationDirPath();
#if defined(_WIN32)
    const auto binDir = QDir(appDir).filePath(QString("jre/%1/bin").arg(major));
    const auto javaw = QDir(binDir).filePath("javaw.exe");
    const auto java = QDir(binDir).filePath("java.exe");
    if (preferGui && QFileInfo::exists(javaw)) return javaw;
    if (QFileInfo::exists(java)) return java;
    if (QFileInfo::exists(javaw)) return javaw;
    return {};
#else
    const auto binDir = QDir(appDir).filePath(QString("jre/%1/bin").arg(major));
    const auto java = QDir(binDir).filePath("java");
    if (QFileInfo::exists(java)) return java;
    return {};
#endif
}

static QString resolveJavaExecutable(const QJsonObject& root, const LaunchOptions& options)
{
    if (!options.javaPath.trimmed().isEmpty())
    {
        return options.javaPath.trimmed();
    }

    const auto configured = appSettings().get("java.path").value_or("");
    if (!configured.empty())
    {
        const auto q = QString::fromStdString(configured).trimmed();
        if (!q.isEmpty()) return q;
    }

    int major = root.value("javaVersion").toObject().value("majorVersion").toInt(0);
    if (major <= 0)
    {
        const auto base = root.value("inheritsFrom").toString();
        const auto mc = base.isEmpty() ? root.value("id").toString() : base;
        major = majorFromMcVersion(mc);
    }

    const bool preferGui = QCoreApplication::instance() && QCoreApplication::instance()->inherits("QApplication");
    const auto bundled = bundledJavaPath(major, preferGui);
    if (!bundled.isEmpty()) return bundled;
    return "java";
}

static QString replaceVars(QString s, const QMap<QString, QString>& vars)
{
    for (auto it = vars.begin(); it != vars.end(); ++it)
    {
        s.replace("${" + it.key() + "}", it.value());
    }
    return s;
}

static void appendArgumentValue(QStringList& out, const QJsonValue& val)
{
    if (val.isString())
    {
        out.push_back(val.toString());
        return;
    }
    if (val.isArray())
    {
        const auto arr = val.toArray();
        for (const auto& item : arr)
        {
            if (item.isString()) out.push_back(item.toString());
        }
    }
}

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
        error = "Version is not installed";
        return {};
    }
    const auto bytes = f.readAll();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (doc.isNull() || !doc.isObject())
    {
        error = "Invalid version JSON";
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

static QJsonObject resolveVersionObject(const QString& versionsDir, const QString& versionId, QString& error)
{
    QSet<QString> visiting;
    return resolveVersionObjectRec(versionsDir, versionId, error, visiting);
}

MinecraftLauncher::MinecraftLauncher(QString dataDir)
    : m_dataDir(std::move(dataDir))
{
}

LaunchCommand MinecraftLauncher::buildLaunchCommand(const Instance& instance, const AuthSession& session, const LaunchOptions& options, QString& error) const
{
    const auto versionId = QString::fromStdString(instance.minecraftVersion);
    const auto versionsDir = QDir(m_dataDir).filePath("versions");
    const auto root = resolveVersionObject(versionsDir, versionId, error);
    if (!error.isEmpty()) return {};
    const auto jarId = root.value("jar").toString(versionId);
    const auto jarPath = QDir(QDir(versionsDir).filePath(jarId)).filePath(jarId + ".jar");
    if (!QFileInfo::exists(jarPath))
    {
        error = "Version is not installed";
        return {};
    }

    const auto mainClass = root.value("mainClass").toString();
    if (mainClass.isEmpty())
    {
        error = "Missing mainClass";
        return {};
    }

    QStringList cp;
    const auto libsDir = QDir(m_dataDir).filePath("libraries");
    const auto libs = root.value("libraries").toArray();
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
            if (path.isEmpty())
            {
                continue;
            }
            const auto fullPath = QDir(libsDir).filePath(path);
            cp.push_back(fullPath);
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
        const auto fullPath = QDir(libsDir).filePath(mavenRelPath(c));
        cp.push_back(fullPath);
    }
    cp.push_back(jarPath);

    const auto assetsRoot = QDir(m_dataDir).filePath("assets");
    const auto assetsIndex = root.value("assetIndex").toObject().value("id").toString();

    const auto gameDir = QDir(m_dataDir).filePath(QString("instances/%1/game").arg(QString::fromStdString(instance.id)));
    const auto nativesDir = QDir(m_dataDir).filePath(QString("instances/%1/natives").arg(QString::fromStdString(instance.id)));
    QDir().mkpath(gameDir);
    QDir().mkpath(nativesDir);

    QMap<QString, QString> vars;
    vars["auth_player_name"] = session.playerName;
    vars["version_name"] = versionId;
    vars["game_directory"] = gameDir;
    vars["assets_root"] = assetsRoot;
    vars["assets_index_name"] = assetsIndex;
    vars["auth_uuid"] = session.uuid;
    vars["auth_access_token"] = session.accessToken;
    vars["user_type"] = session.userType;
    vars["classpath"] = cp.join(classpathSeparator());
    vars["classpath_separator"] = classpathSeparator();
    vars["natives_directory"] = nativesDir;
    vars["launcher_name"] = "BlockForge";
    vars["launcher_version"] = QString::fromUtf8(BLOCKFORGE_VERSION);
    vars["library_directory"] = QDir(m_dataDir).filePath("libraries");
    vars["version_type"] = "release";
    vars["clientid"] = session.clientId.isEmpty() ? "0" : session.clientId;
    vars["auth_xuid"] = session.xuid.isEmpty() ? "0" : session.xuid;
    vars["resolution_width"] = options.resolutionWidth > 0 ? QString::number(options.resolutionWidth) : "";
    vars["resolution_height"] = options.resolutionHeight > 0 ? QString::number(options.resolutionHeight) : "";

    QStringList args;
    if (options.maxRamMb > 0)
    {
        args.push_back(QString("-Xmx%1M").arg(options.maxRamMb));
    }
    for (const auto& a : options.extraJvmArgs)
    {
        if (!a.isEmpty()) args.push_back(a);
    }
    const auto arguments = root.value("arguments").toObject();
    if (!arguments.isEmpty())
    {
        const auto jvm = arguments.value("jvm").toArray();
        for (const auto& a : jvm)
        {
            if (a.isString())
            {
                args.push_back(replaceVars(a.toString(), vars));
            }
            else if (a.isObject())
            {
                const auto obj = a.toObject();
                if (!rulesAllow(obj.value("rules").toArray(), features))
                {
                    continue;
                }
                QStringList tmp;
                appendArgumentValue(tmp, obj.value("value"));
                for (auto& t : tmp)
                {
                    args.push_back(replaceVars(t, vars));
                }
            }
        }

        args.push_back(mainClass);

        const auto game = arguments.value("game").toArray();
        for (const auto& a : game)
        {
            if (a.isString())
            {
                args.push_back(replaceVars(a.toString(), vars));
            }
            else if (a.isObject())
            {
                const auto obj = a.toObject();
                if (!rulesAllow(obj.value("rules").toArray(), features))
                {
                    continue;
                }
                QStringList tmp;
                appendArgumentValue(tmp, obj.value("value"));
                for (auto& t : tmp)
                {
                    args.push_back(replaceVars(t, vars));
                }
            }
        }
    }
    else
    {
        const auto legacyArgs = root.value("minecraftArguments").toString();
        args << "-cp" << vars["classpath"];
        args.push_back(mainClass);
        const auto parts = legacyArgs.split(' ', Qt::SkipEmptyParts);
        for (const auto& p : parts)
        {
            args.push_back(replaceVars(p, vars));
        }
    }

    LaunchCommand cmd;
    cmd.program = resolveJavaExecutable(root, options);
    cmd.args = args;
    return cmd;
}
}
