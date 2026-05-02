#include "ui/MainWindow.h"

#include "core/AppPaths.h"
#include "core/FsUtil.h"
#include "core/OfflineAuth.h"
#include "core/InstanceStore.h"
#include "core/Settings.h"
#include "auth/AccountStore.h"
#include "ui/AuthWorker.h"
#include "ui/LaunchWorker.h"

#if defined(BLOCKFORGE_BUILD_MC)
#include "minecraft/MinecraftInstaller.h"
#include "minecraft/MinecraftLauncher.h"
#endif

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QSplitter>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QUrl>

#include <filesystem>
#include <fstream>

#if defined(BLOCKFORGE_BUILD_MC)
#include <QProcess>
#endif

namespace blockforge
{
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("BlockForge Launcher");

    auto settings = appSettings();
    const auto initialName = settings.get("offline.name").value_or("BlockForgePlayer");

    auto* root = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* nav = new QListWidget(root);
    nav->addItem("Instances");
    nav->addItem("Accounts");
    nav->addItem("Java");
    nav->addItem("Mods");
    nav->addItem("Logs");
    nav->addItem("Settings");
    nav->setFixedWidth(180);

    auto* content = new QWidget(root);
    auto* contentLayout = new QVBoxLayout(content);

    auto* instRow = new QWidget(content);
    auto* instRowLayout = new QHBoxLayout(instRow);
    instRowLayout->setContentsMargins(12, 12, 12, 0);
    instRowLayout->addWidget(new QLabel("Instance", instRow));
    auto* instCombo = new QComboBox(instRow);
    instRowLayout->addWidget(instCombo, 1);
    auto* create = new QPushButton("Create", instRow);
    instRowLayout->addWidget(create);
    auto* exportBtn = new QPushButton("Export", instRow);
    instRowLayout->addWidget(exportBtn);
    auto* importBtn = new QPushButton("Import", instRow);
    instRowLayout->addWidget(importBtn);
    contentLayout->addWidget(instRow);

    auto* createRow = new QWidget(content);
    auto* createRowLayout = new QHBoxLayout(createRow);
    createRowLayout->setContentsMargins(12, 0, 12, 0);
    auto* nameInput = new QLineEdit(createRow);
    nameInput->setPlaceholderText("New instance name");
    auto* verInput = new QLineEdit(createRow);
    verInput->setPlaceholderText("Minecraft version (e.g. 1.20.1)");
    auto* loader = new QComboBox(createRow);
    loader->addItem("vanilla");
    loader->addItem("fabric");
    loader->addItem("forge");
    createRowLayout->addWidget(nameInput, 2);
    createRowLayout->addWidget(verInput, 1);
    createRowLayout->addWidget(loader, 1);
    contentLayout->addWidget(createRow);

    auto* nameRow = new QWidget(content);
    auto* nameRowLayout = new QHBoxLayout(nameRow);
    nameRowLayout->setContentsMargins(12, 12, 12, 0);
    nameRowLayout->addWidget(new QLabel("Nickname", nameRow));
    auto* nick = new QLineEdit(nameRow);
    nick->setText(QString::fromStdString(initialName));
    nameRowLayout->addWidget(nick, 1);
    contentLayout->addWidget(nameRow);

    auto* javaRow = new QWidget(content);
    auto* javaRowLayout = new QHBoxLayout(javaRow);
    javaRowLayout->setContentsMargins(12, 8, 12, 0);
    javaRowLayout->addWidget(new QLabel("Java path", javaRow));
    auto* javaPath = new QLineEdit(javaRow);
    javaPath->setPlaceholderText("java or C:\\\\Path\\\\to\\\\javaw.exe");
    if (const auto p = settings.get("java.path")) javaPath->setText(QString::fromStdString(*p));
    javaRowLayout->addWidget(javaPath, 1);
    contentLayout->addWidget(javaRow);

    auto* ramRow = new QWidget(content);
    auto* ramRowLayout = new QHBoxLayout(ramRow);
    ramRowLayout->setContentsMargins(12, 8, 12, 0);
    ramRowLayout->addWidget(new QLabel("Max RAM (MB)", ramRow));
    auto* ram = new QLineEdit(ramRow);
    ram->setPlaceholderText("4096");
    if (const auto r = settings.get("java.maxRamMb")) ram->setText(QString::fromStdString(*r));
    ramRowLayout->addWidget(ram, 1);
    contentLayout->addWidget(ramRow);

    auto* msRow = new QWidget(content);
    auto* msRowLayout = new QHBoxLayout(msRow);
    msRowLayout->setContentsMargins(12, 8, 12, 0);
    msRowLayout->addWidget(new QLabel("Microsoft client id", msRow));
    auto* msClientId = new QLineEdit(msRow);
    msClientId->setPlaceholderText("Azure App client id");
    if (const auto v = settings.get("ms.clientId")) msClientId->setText(QString::fromStdString(*v));
    auto* addAccount = new QPushButton("Add account", msRow);
    msRowLayout->addWidget(msClientId, 1);
    msRowLayout->addWidget(addAccount);
    contentLayout->addWidget(msRow);

    auto* accountRow = new QWidget(content);
    auto* accountRowLayout = new QHBoxLayout(accountRow);
    accountRowLayout->setContentsMargins(12, 8, 12, 0);
    accountRowLayout->addWidget(new QLabel("Account", accountRow));
    auto* accountCombo = new QComboBox(accountRow);
    accountRowLayout->addWidget(accountCombo, 1);
    auto* logout = new QPushButton("Logout", accountRow);
    accountRowLayout->addWidget(logout);
    contentLayout->addWidget(accountRow);

    auto* info = new QLabel(content);
    const auto initialProfile = makeOfflineProfile(initialName);
    info->setText(QString("Offline UUID: %1").arg(QString::fromStdString(initialProfile.uuid)));
    info->setContentsMargins(12, 0, 12, 12);
    contentLayout->addWidget(info);

    auto* playRow = new QWidget(content);
    auto* playRowLayout = new QHBoxLayout(playRow);
    playRowLayout->setContentsMargins(12, 0, 12, 12);
    auto* play = new QPushButton("Play (offline)", playRow);
    playRowLayout->addWidget(play);
    playRowLayout->addStretch(1);
    contentLayout->addWidget(playRow);

    auto* logView = new QPlainTextEdit(content);
    logView->setReadOnly(true);
    logView->setMinimumHeight(240);
    contentLayout->addWidget(logView);

    auto* modsRow = new QWidget(content);
    auto* modsRowLayout = new QHBoxLayout(modsRow);
    modsRowLayout->setContentsMargins(12, 0, 12, 0);
    modsRowLayout->addWidget(new QLabel("Mods", modsRow));
    auto* modsRefresh = new QPushButton("Refresh", modsRow);
    modsRowLayout->addWidget(modsRefresh);
    auto* modsOpen = new QPushButton("Open folder", modsRow);
    modsRowLayout->addWidget(modsOpen);
    modsRowLayout->addStretch(1);
    contentLayout->addWidget(modsRow);

    auto* modsList = new QListWidget(content);
    modsList->setMinimumHeight(180);
    contentLayout->addWidget(modsList);

    connect(nick, &QLineEdit::editingFinished, this, [nick, info]() {
        auto settings = appSettings();
        const auto name = nick->text().toStdString();
        settings.set("offline.name", name);
        const auto profile = makeOfflineProfile(name);
        info->setText(QString("Offline UUID: %1").arg(QString::fromStdString(profile.uuid)));
    });

    connect(javaPath, &QLineEdit::editingFinished, this, [javaPath]() {
        auto settings = appSettings();
        const auto p = javaPath->text().trimmed().toStdString();
        if (p.empty()) settings.set("java.path", ""); else settings.set("java.path", p);
    });

    connect(ram, &QLineEdit::editingFinished, this, [ram]() {
        auto settings = appSettings();
        const auto v = ram->text().trimmed().toStdString();
        if (v.empty()) return;
        settings.set("java.maxRamMb", v);
    });

    connect(msClientId, &QLineEdit::editingFinished, this, [msClientId]() {
        auto settings = appSettings();
        settings.set("ms.clientId", msClientId->text().trimmed().toStdString());
    });

#if defined(BLOCKFORGE_BUILD_MC)
    auto refreshInstances = [instCombo]() {
        auto settings = appSettings();
        const auto last = settings.get("ui.lastInstanceId").value_or("");
        instCombo->blockSignals(true);
        instCombo->clear();
        InstanceStore store(AppPaths::instancesDir());
        const auto instances = store.list();
        int setIndex = -1;
        int idx = 0;
        for (const auto& i : instances)
        {
            const auto label = QString("%1 (%2, %3)")
                                   .arg(QString::fromStdString(i.name))
                                   .arg(QString::fromStdString(i.minecraftVersion))
                                   .arg(QString::fromStdString(toString(i.loaderType)));
            instCombo->addItem(label, QString::fromStdString(i.id));
            if (!last.empty() && i.id == last)
            {
                setIndex = idx;
            }
            ++idx;
        }
        if (setIndex >= 0)
        {
            instCombo->setCurrentIndex(setIndex);
        }
        instCombo->blockSignals(false);
    };

    auto refreshAccounts = [accountCombo, play, logout]() {
        accountCombo->blockSignals(true);
        accountCombo->clear();

        AccountStore store(QString::fromStdString(AppPaths::dataDir().string()));
        QString error;
        const auto accounts = store.listAccounts(error);
        if (!error.isEmpty())
        {
            play->setText("Play (offline)");
            accountCombo->setEnabled(false);
            logout->setEnabled(false);
            accountCombo->blockSignals(false);
            return;
        }

        QString activeId;
        if (store.hasActiveAccount())
        {
            QString e;
            const auto acc = store.activeAccount(e);
            if (e.isEmpty()) activeId = acc.accountId;
        }

        int activeIndex = -1;
        int idx = 0;
        for (const auto& a : accounts)
        {
            const auto label = a.displayName.isEmpty() ? a.accountId : a.displayName;
            accountCombo->addItem(label, a.accountId);
            if (!activeId.isEmpty() && a.accountId == activeId)
            {
                activeIndex = idx;
            }
            ++idx;
        }
        if (activeIndex >= 0)
        {
            accountCombo->setCurrentIndex(activeIndex);
            play->setText("Play (online)");
        }
        else
        {
            play->setText("Play (offline)");
        }

        const bool hasAny = !accounts.isEmpty();
        accountCombo->setEnabled(hasAny);
        logout->setEnabled(hasAny);
        accountCombo->blockSignals(false);
    };

    auto refreshMods = [instCombo, modsList, logView]() {
        modsList->blockSignals(true);
        modsList->clear();
        modsList->blockSignals(false);

        if (instCombo->currentIndex() < 0)
        {
            return;
        }
        const auto instanceId = instCombo->currentData().toString();
        const auto gameDir = QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(instanceId));
        const auto modsDir = QDir(gameDir).filePath("mods");
        QDir().mkpath(modsDir);

        QDir d(modsDir);
        const auto files = d.entryList(QStringList() << "*.jar" << "*.jar.disabled", QDir::Files, QDir::Name);
        modsList->blockSignals(true);
        for (const auto& f : files)
        {
            const bool disabled = f.endsWith(".disabled");
            const auto display = disabled ? f.left(f.size() - QString(".disabled").size()) : f;
            auto* item = new QListWidgetItem(display, modsList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(disabled ? Qt::Unchecked : Qt::Checked);
            item->setData(Qt::UserRole, f);
        }
        modsList->blockSignals(false);
    };

    refreshInstances();
    refreshAccounts();
    refreshMods();

    connect(instCombo, &QComboBox::currentIndexChanged, this, [refreshMods, instCombo](int) {
        auto settings = appSettings();
        settings.set("ui.lastInstanceId", instCombo->currentData().toString().toStdString());
        refreshMods();
    });
    connect(modsRefresh, &QPushButton::clicked, this, [refreshMods]() { refreshMods(); });
    connect(modsOpen, &QPushButton::clicked, this, [instCombo]() {
        if (instCombo->currentIndex() < 0) return;
        const auto instanceId = instCombo->currentData().toString();
        const auto gameDir = QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(instanceId));
        const auto modsDir = QDir(gameDir).filePath("mods");
        QDir().mkpath(modsDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(modsDir));
    });

    connect(accountCombo, &QComboBox::currentIndexChanged, this, [accountCombo, play](int) {
        if (accountCombo->currentIndex() < 0)
        {
            play->setText("Play (offline)");
            return;
        }
        const auto accountId = accountCombo->currentData().toString();
        AccountStore store(QString::fromStdString(AppPaths::dataDir().string()));
        QString error;
        store.setActiveAccount(accountId, error);
        play->setText(error.isEmpty() ? "Play (online)" : "Play (offline)");
    });

    connect(logout, &QPushButton::clicked, this, [accountCombo, play, logView, refreshAccounts]() {
        if (accountCombo->currentIndex() < 0)
        {
            return;
        }
        const auto accountId = accountCombo->currentData().toString();
        AccountStore store(QString::fromStdString(AppPaths::dataDir().string()));
        QString error;
        if (!store.removeAccount(accountId, error))
        {
            logView->appendPlainText(QString("Logout failed: %1").arg(error));
        }
        refreshAccounts();
        play->setText("Play (offline)");
    });

    connect(modsList, &QListWidget::itemChanged, this, [instCombo, modsList, logView](QListWidgetItem* item) {
        if (!item) return;
        if (instCombo->currentIndex() < 0) return;

        const auto instanceId = instCombo->currentData().toString();
        const auto gameDir = QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(instanceId));
        const auto modsDir = QDir(gameDir).filePath("mods");
        QDir().mkpath(modsDir);

        const auto src = item->data(Qt::UserRole).toString();
        const bool wantEnabled = item->checkState() == Qt::Checked;
        QString dst = src;
        if (wantEnabled)
        {
            if (dst.endsWith(".disabled"))
            {
                dst = dst.left(dst.size() - QString(".disabled").size());
            }
        }
        else
        {
            if (!dst.endsWith(".disabled"))
            {
                dst = dst + ".disabled";
            }
        }

        const auto srcPath = QDir(modsDir).filePath(src);
        const auto dstPath = QDir(modsDir).filePath(dst);
        if (QFileInfo::exists(srcPath) && srcPath != dstPath)
        {
            QFile::rename(srcPath, dstPath);
        }
        item->setData(Qt::UserRole, dst);
    });

    connect(create, &QPushButton::clicked, this, [nameInput, verInput, loader, logView, refreshInstances]() {
        const auto name = nameInput->text().trimmed();
        const auto ver = verInput->text().trimmed();
        if (name.isEmpty() || ver.isEmpty())
        {
            logView->appendPlainText("Enter instance name and Minecraft version.");
            return;
        }
        InstanceStore store(AppPaths::instancesDir());
        const auto loaderStr = loader->currentText().toStdString();
        const auto lt = loaderTypeFromString(loaderStr).value_or(LoaderType::Vanilla);
        const auto inst = store.create(name.toStdString(), ver.toStdString(), lt);
        auto settings = appSettings();
        settings.set("ui.lastInstanceId", inst.id);
        refreshInstances();
        logView->appendPlainText(QString("Created instance: %1").arg(QString::fromStdString(inst.id)));
    });

    connect(exportBtn, &QPushButton::clicked, this, [instCombo, logView]() {
        if (instCombo->currentIndex() < 0)
        {
            logView->appendPlainText("Select instance first.");
            return;
        }
        const auto instanceId = instCombo->currentData().toString().toStdString();
        const auto outDir = QFileDialog::getExistingDirectory(nullptr, "Export to folder");
        if (outDir.isEmpty())
        {
            return;
        }
        const auto src = AppPaths::instancesDir() / instanceId;
        const auto dst = std::filesystem::path(outDir.toStdString()) / instanceId;
        if (!copyTree(src, dst))
        {
            logView->appendPlainText("Export failed.");
            return;
        }
        logView->appendPlainText(QString("Exported to: %1").arg(QString::fromStdString(dst.string())));
    });

    connect(importBtn, &QPushButton::clicked, this, [logView, refreshInstances]() {
        const auto dir = QFileDialog::getExistingDirectory(nullptr, "Import instance folder");
        if (dir.isEmpty())
        {
            return;
        }
        const auto src = std::filesystem::path(dir.toStdString());
        if (!std::filesystem::exists(src))
        {
            logView->appendPlainText("Source not found.");
            return;
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

        InstanceStore store(AppPaths::instancesDir());
        const auto inst = store.create(name, mc, lt, loaderVer);
        const auto dst = AppPaths::instancesDir() / inst.id;
        if (!copyTree(src, dst))
        {
            logView->appendPlainText("Import failed.");
            return;
        }
        logView->appendPlainText(QString("Imported as instance: %1").arg(QString::fromStdString(inst.id)));
        auto settings = appSettings();
        settings.set("ui.lastInstanceId", inst.id);
        refreshInstances();
    });

    connect(play, &QPushButton::clicked, this, [nick, logView, play, instCombo, javaPath, ram]() {
        InstanceStore store(AppPaths::instancesDir());
        const auto instances = store.list();
        if (instances.empty() || instCombo->currentIndex() < 0)
        {
            logView->appendPlainText("No instances. Create one via CLI for now.");
            return;
        }

        const auto selectedId = instCombo->currentData().toString().toStdString();
        auto it = std::find_if(instances.begin(), instances.end(), [&](const Instance& i) { return i.id == selectedId; });
        if (it == instances.end())
        {
            logView->appendPlainText("Selected instance not found.");
            return;
        }
        const auto inst = *it;
        const auto profile = makeOfflineProfile(nick->text().toStdString());

        AuthSession session;
        session.playerName = QString::fromStdString(profile.name);
        session.uuid = QString::fromStdString(profile.uuid);
        session.accessToken = "0";
        session.userType = "legacy";
        session.xuid = "0";
        session.clientId = "0";

        play->setEnabled(false);
        logView->appendPlainText("Preparing launch...");

        LaunchOptions opts;
        opts.javaPath = javaPath->text().trimmed();
        bool ok = false;
        const int m = ram->text().trimmed().toInt(&ok);
        if (ok && m > 0) opts.maxRamMb = m;

        auto* thread = new QThread(logView);
        auto* worker = new LaunchWorker();
        worker->configure(QString::fromStdString(AppPaths::dataDir().string()), inst, session, opts);
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::started, worker, &LaunchWorker::run);
        QObject::connect(worker, &LaunchWorker::logLine, logView, [logView](const QString& line) { logView->appendPlainText(line); });
        QObject::connect(worker, &LaunchWorker::failed, logView, [logView, play, thread, worker](const QString& e) {
            logView->appendPlainText(e);
            play->setEnabled(true);
            thread->quit();
        });
        QObject::connect(worker, &LaunchWorker::readyToLaunch, logView, [logView, play, thread, worker](const LaunchCommand& cmd) {
            auto* proc = new QProcess(logView);
            proc->setProgram(cmd.program);
            proc->setArguments(cmd.args);
            proc->setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(proc, &QProcess::readyRead, logView, [proc, logView]() {
                const auto bytes = proc->readAll();
                if (!bytes.isEmpty())
                {
                    logView->appendPlainText(QString::fromUtf8(bytes));
                }
            });
            QObject::connect(proc, &QProcess::finished, logView, [proc, logView, play](int code, QProcess::ExitStatus status) {
                logView->appendPlainText(QString("Process finished: code=%1 status=%2").arg(code).arg(status));
                play->setEnabled(true);
                proc->deleteLater();
            });
            proc->start();
            logView->appendPlainText("Starting Minecraft...");
            thread->quit();
        });
        QObject::connect(worker, &LaunchWorker::finished, thread, [thread, worker]() {
            worker->deleteLater();
            thread->deleteLater();
        });

        thread->start();
    });

    connect(addAccount, &QPushButton::clicked, this, [logView, addAccount, msClientId, refreshAccounts]() {
        const auto clientId = msClientId->text().trimmed();
        if (clientId.isEmpty())
        {
            logView->appendPlainText("Set Microsoft client id first.");
            return;
        }

        addAccount->setEnabled(false);
        logView->appendPlainText("Starting Microsoft device code flow...");

        auto* thread = new QThread(logView);
        auto* worker = new AuthWorker();
        worker->configure(QString::fromStdString(AppPaths::dataDir().string()), clientId);
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::started, worker, &AuthWorker::run);
        QObject::connect(worker, &AuthWorker::logLine, logView, [logView](const QString& line) { logView->appendPlainText(line); });
        QObject::connect(worker, &AuthWorker::deviceCodeReady, logView, [logView](const DeviceCode& code) {
            logView->appendPlainText(QString("Open: %1").arg(code.verificationUri));
            logView->appendPlainText(QString("Code: %1").arg(code.userCode));
        });
        QObject::connect(worker, &AuthWorker::accountReady, logView, [logView, addAccount, thread, worker, refreshAccounts](const QString& name) {
            logView->appendPlainText(QString("Account added: %1").arg(name));
            addAccount->setEnabled(true);
            refreshAccounts();
            thread->quit();
        });
        QObject::connect(worker, &AuthWorker::failed, logView, [logView, addAccount, thread, worker](const QString& e) {
            logView->appendPlainText(QString("Auth failed: %1").arg(e));
            addAccount->setEnabled(true);
            thread->quit();
        });
        QObject::connect(worker, &AuthWorker::finished, thread, [thread, worker]() {
            worker->deleteLater();
            thread->deleteLater();
        });

        thread->start();
    });
#else
    play->setEnabled(false);
#endif

    contentLayout->addStretch(1);

    rootLayout->addWidget(nav);
    rootLayout->addWidget(content, 1);

    setCentralWidget(root);
}
}
