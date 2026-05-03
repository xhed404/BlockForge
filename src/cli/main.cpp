#include "core/AppPaths.h"
#include "core/FsUtil.h"
#include "core/OfflineAuth.h"
#include "core/Settings.h"
#include "core/InstanceStore.h"

#if defined(BLOCKFORGE_BUILD_MC)
#include "minecraft/MinecraftInstaller.h"
#include "minecraft/MinecraftLauncher.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <iostream>

using namespace blockforge;

static void printUsage()
{
    std::cout << "blockforge-cli\n";
    std::cout << "  list\n";
    std::cout << "  create <name> <mcVersion> <vanilla|fabric|forge> [loaderVersion]\n";
    std::cout << "  export <instanceId> <outDir>\n";
    std::cout << "  import <srcDir>\n";
    std::cout << "  profile get\n";
    std::cout << "  profile set <name>\n";
#if defined(BLOCKFORGE_BUILD_MC)
    std::cout << "  mc install <mcVersion>\n";
    std::cout << "  mc fabric <mcVersion> [loaderVersion]\n";
    std::cout << "  mc forge <mcVersion> [forgeVersion]\n";
    std::cout << "  mc assets <mcVersion> [limit]\n";
    std::cout << "  mc natives <mcVersion> <instanceId>\n";
    std::cout << "  mc cmd <instanceId>\n";
    std::cout << "  mc run <instanceId>\n";
#endif
}

static LoaderType parseLoader(const std::string& s)
{
    if (auto t = loaderTypeFromString(s)) return *t;
    return LoaderType::Vanilla;
}

int main(int argc, char** argv)
{
#if defined(BLOCKFORGE_BUILD_MC)
    QCoreApplication qtApp(argc, argv);
#endif

    if (argc < 2)
    {
        printUsage();
        return 2;
    }

    const std::string cmd = argv[1];
    InstanceStore store(AppPaths::instancesDir());
    auto settings = appSettings();

    if (cmd == "list")
    {
        for (const auto& i : store.list())
        {
            std::cout << i.id << "\t" << i.name << "\t" << i.minecraftVersion << "\t" << toString(i.loaderType) << "\n";
        }
        return 0;
    }

    if (cmd == "profile")
    {
        if (argc < 3)
        {
            printUsage();
            return 2;
        }

        const std::string sub = argv[2];
        if (sub == "get")
        {
            const auto name = settings.get("offline.name").value_or("BlockForgePlayer");
            const auto profile = makeOfflineProfile(name);
            std::cout << profile.name << "\n" << profile.uuid << "\n";
            return 0;
        }

        if (sub == "set")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            settings.set("offline.name", argv[3]);
            const auto profile = makeOfflineProfile(argv[3]);
            std::cout << profile.uuid << "\n";
            return 0;
        }

        printUsage();
        return 2;
    }

    if (cmd == "create")
    {
        if (argc < 5)
        {
            printUsage();
            return 2;
        }
        std::optional<std::string> loaderVer;
        if (argc >= 6)
        {
            loaderVer = std::string(argv[5]);
        }
        const auto instance = store.create(argv[2], argv[3], parseLoader(argv[4]), loaderVer);
        std::cout << instance.id << "\n";
        return 0;
    }

    if (cmd == "export")
    {
        if (argc < 4)
        {
            printUsage();
            return 2;
        }
        const auto src = AppPaths::instancesDir() / argv[2];
        const auto dst = std::filesystem::path(argv[3]) / argv[2];
        if (!std::filesystem::exists(src))
        {
            std::cerr << "Instance dir not found\n";
            return 1;
        }
        if (!copyTree(src, dst))
        {
            std::cerr << "Export failed\n";
            return 1;
        }
        std::cout << dst.string() << "\n";
        return 0;
    }

    if (cmd == "import")
    {
        if (argc < 3)
        {
            printUsage();
            return 2;
        }
        const auto src = std::filesystem::path(argv[2]);
        if (!std::filesystem::exists(src))
        {
            std::cerr << "Source dir not found\n";
            return 1;
        }

        std::string name = src.filename().string();
        std::string mc = "1.20.1";
        LoaderType lt = LoaderType::Vanilla;
        std::optional<std::string> loaderVer;

        const auto meta = src / "instance.bf";
        if (std::filesystem::exists(meta))
        {
            std::ifstream in(meta);
            std::string line;
            while (std::getline(in, line))
            {
                const auto pos = line.find('=');
                if (pos == std::string::npos) continue;
                const auto k = line.substr(0, pos);
                const auto v = line.substr(pos + 1);
                if (k == "name") name = v;
                else if (k == "minecraftVersion") mc = v;
                else if (k == "loaderType")
                {
                    if (auto t = loaderTypeFromString(v)) lt = *t;
                }
                else if (k == "loaderVersion") loaderVer = v;
            }
        }

        const auto instance = store.create(name, mc, lt, loaderVer);
        const auto dst = AppPaths::instancesDir() / instance.id;
        if (!copyTree(src, dst))
        {
            std::cerr << "Import failed\n";
            return 1;
        }
        std::cout << instance.id << "\n";
        return 0;
    }

#if defined(BLOCKFORGE_BUILD_MC)
    if (cmd == "mc")
    {
        if (argc < 3)
        {
            printUsage();
            return 2;
        }
        const std::string sub = argv[2];
        if (sub == "install")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            const auto r = installer.installVersion(argv[3]);
            if (!r.ok)
            {
                std::cerr << r.error.toStdString() << "\n";
                return 1;
            }
            std::cout << "ok\n";
            return 0;
        }

        if (sub == "fabric")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            const auto mc = QString::fromUtf8(argv[3]);
            const auto loader = (argc >= 5) ? QString::fromUtf8(argv[4]) : QString();
            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            QString id;
            const auto r = installer.installFabric(mc, loader, id);
            if (!r.ok)
            {
                std::cerr << r.error.toStdString() << "\n";
                return 1;
            }
            const auto r2 = installer.installVersion(id);
            if (!r2.ok)
            {
                std::cerr << r2.error.toStdString() << "\n";
                return 1;
            }
            std::cout << id.toStdString() << "\n";
            return 0;
        }

        if (sub == "forge")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            const auto mc = QString::fromUtf8(argv[3]);
            const auto ver = (argc >= 5) ? QString::fromUtf8(argv[4]) : QString();
            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            QString id;
            const auto r = installer.installForge(mc, ver, id);
            if (!r.ok)
            {
                std::cerr << r.error.toStdString() << "\n";
                return 1;
            }
            const auto r2 = installer.installVersion(id);
            if (!r2.ok)
            {
                std::cerr << r2.error.toStdString() << "\n";
                return 1;
            }
            std::cout << id.toStdString() << "\n";
            return 0;
        }

        if (sub == "assets")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            const int limit = (argc >= 5) ? std::atoi(argv[4]) : 0;
            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            const auto r = installer.downloadAssets(argv[3], limit);
            if (!r.ok)
            {
                std::cerr << r.error.toStdString() << "\n";
                return 1;
            }
            std::cout << "ok\n";
            return 0;
        }

        if (sub == "natives")
        {
            if (argc < 5)
            {
                printUsage();
                return 2;
            }
            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            const auto r = installer.prepareNatives(argv[3], argv[4]);
            if (!r.ok)
            {
                std::cerr << r.error.toStdString() << "\n";
                return 1;
            }
            std::cout << "ok\n";
            return 0;
        }

        if (sub == "cmd")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            const auto instances = store.list();
            const auto id = std::string(argv[3]);
            auto it = std::find_if(instances.begin(), instances.end(), [&](const Instance& i) { return i.id == id; });
            if (it == instances.end())
            {
                std::cerr << "Instance not found\n";
                return 1;
            }

            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            QString version = QString::fromStdString(it->minecraftVersion);
            if (it->loaderType == LoaderType::Fabric)
            {
                QString fabricId;
                const auto r = installer.installFabric(QString::fromStdString(it->minecraftVersion),
                                                       it->loaderVersion.has_value() ? QString::fromStdString(*it->loaderVersion) : QString(),
                                                       fabricId);
                if (!r.ok)
                {
                    std::cerr << r.error.toStdString() << "\n";
                    return 1;
                }
                version = fabricId;
            }
            else if (it->loaderType == LoaderType::Forge)
            {
                QString forgeId;
                const auto r = installer.installForge(QString::fromStdString(it->minecraftVersion),
                                                      it->loaderVersion.has_value() ? QString::fromStdString(*it->loaderVersion) : QString(),
                                                      forgeId);
                if (!r.ok)
                {
                    std::cerr << r.error.toStdString() << "\n";
                    return 1;
                }
                version = forgeId;
            }

            const auto r2 = installer.installVersion(version);
            if (!r2.ok)
            {
                std::cerr << r2.error.toStdString() << "\n";
                return 1;
            }

            const auto name = settings.get("offline.name").value_or("BlockForgePlayer");
            const auto offline = makeOfflineProfile(name);

            AuthSession session;
            session.playerName = QString::fromStdString(offline.name);
            session.uuid = QString::fromStdString(offline.uuid);
            session.accessToken = "0";
            session.userType = "legacy";
            session.xuid = "0";
            session.clientId = "0";

            MinecraftLauncher launcher(QString::fromStdString(AppPaths::dataDir().string()));
            LaunchOptions opts;
            if (const auto p = settings.get("java.path")) opts.javaPath = QString::fromStdString(*p);
            if (const auto r = settings.get("java.maxRamMb"))
            {
                try { opts.maxRamMb = std::stoi(*r); } catch (...) { opts.maxRamMb = 4096; }
            }
            QString error;
            auto inst = *it;
            inst.minecraftVersion = version.toStdString();
            const auto cmd = launcher.buildLaunchCommand(inst, session, opts, error);
            if (!error.isEmpty())
            {
                std::cerr << error.toStdString() << "\n";
                return 1;
            }

            std::cout << cmd.program.toStdString() << "\n";
            for (const auto& a : cmd.args)
            {
                std::cout << a.toStdString() << "\n";
            }
            return 0;
        }

        if (sub == "run")
        {
            if (argc < 4)
            {
                printUsage();
                return 2;
            }
            const auto instances = store.list();
            const auto id = std::string(argv[3]);
            auto it = std::find_if(instances.begin(), instances.end(), [&](const Instance& i) { return i.id == id; });
            if (it == instances.end())
            {
                std::cerr << "Instance not found\n";
                return 1;
            }

            MinecraftInstaller installer(QString::fromStdString(AppPaths::dataDir().string()));
            QString version = QString::fromStdString(it->minecraftVersion);
            if (it->loaderType == LoaderType::Fabric)
            {
                QString fabricId;
                const auto r = installer.installFabric(QString::fromStdString(it->minecraftVersion),
                                                       it->loaderVersion.has_value() ? QString::fromStdString(*it->loaderVersion) : QString(),
                                                       fabricId);
                if (!r.ok)
                {
                    std::cerr << r.error.toStdString() << "\n";
                    return 1;
                }
                version = fabricId;
            }
            else if (it->loaderType == LoaderType::Forge)
            {
                QString forgeId;
                const auto r = installer.installForge(QString::fromStdString(it->minecraftVersion),
                                                      it->loaderVersion.has_value() ? QString::fromStdString(*it->loaderVersion) : QString(),
                                                      forgeId);
                if (!r.ok)
                {
                    std::cerr << r.error.toStdString() << "\n";
                    return 1;
                }
                version = forgeId;
            }

            const auto r2 = installer.installVersion(version);
            if (!r2.ok)
            {
                std::cerr << r2.error.toStdString() << "\n";
                return 1;
            }
            installer.prepareNatives(version, QString::fromStdString(it->id));

            const auto name = settings.get("offline.name").value_or("BlockForgePlayer");
            const auto offline = makeOfflineProfile(name);

            AuthSession session;
            session.playerName = QString::fromStdString(offline.name);
            session.uuid = QString::fromStdString(offline.uuid);
            session.accessToken = "0";
            session.userType = "legacy";
            session.xuid = "0";
            session.clientId = "0";

            MinecraftLauncher launcher(QString::fromStdString(AppPaths::dataDir().string()));
            LaunchOptions opts;
            if (const auto p = settings.get("java.path")) opts.javaPath = QString::fromStdString(*p);
            if (const auto r = settings.get("java.maxRamMb"))
            {
                try { opts.maxRamMb = std::stoi(*r); } catch (...) { opts.maxRamMb = 4096; }
            }
            QString error;
            auto inst = *it;
            inst.minecraftVersion = version.toStdString();
            const auto cmd = launcher.buildLaunchCommand(inst, session, opts, error);
            if (!error.isEmpty())
            {
                std::cerr << error.toStdString() << "\n";
                return 1;
            }

            QProcess proc;
            proc.setProgram(cmd.program);
            proc.setArguments(cmd.args);
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start();
            if (!proc.waitForStarted(10000))
            {
                std::cerr << "Failed to start process\n";
                return 1;
            }

            while (proc.state() != QProcess::NotRunning)
            {
                proc.waitForReadyRead(200);
                const auto out = proc.readAll();
                if (!out.isEmpty())
                {
                    std::cout << out.toStdString() << std::flush;
                }
            }
            const auto out = proc.readAll();
            if (!out.isEmpty())
            {
                std::cout << out.toStdString() << std::flush;
            }

            return proc.exitCode();
        }

        printUsage();
        return 2;
    }
#endif

    printUsage();
    return 2;
}
