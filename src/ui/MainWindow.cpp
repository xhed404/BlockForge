#include "ui/MainWindow.h"

#include "auth/AccountStore.h"
#include "core/AppPaths.h"
#include "core/FsUtil.h"
#include "core/InstanceStore.h"
#include "core/OfflineAuth.h"
#include "core/Settings.h"
#include "ui/AuthWorker.h"
#include "ui/LaunchWorker.h"

#include <QCloseEvent>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QSplitter>
#include <QStyleOptionViewItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#if defined(BLOCKFORGE_BUILD_MC)
#include <QProcess>
#endif

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace blockforge
{
namespace
{
class StatusDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const auto txt = opt.text;
        opt.text.clear();

        QStyledItemDelegate::paint(painter, opt, index);

        if (txt.isEmpty()) return;

        QColor fg("#aab2bd");
        QColor bg("#1a232e");

        if (txt.startsWith("Ready", Qt::CaseInsensitive))
        {
            fg = QColor("#c9f7c5");
            bg = QColor("#164a23");
        }
        else if (txt.startsWith("Downloading", Qt::CaseInsensitive))
        {
            fg = QColor("#d9ecff");
            bg = QColor("#153a57");
        }
        else if (txt.startsWith("Broken", Qt::CaseInsensitive))
        {
            fg = QColor("#ffd0d0");
            bg = QColor("#4a2020");
        }
        else if (txt.startsWith("Not", Qt::CaseInsensitive))
        {
            fg = QColor("#c9ced6");
            bg = QColor("#2a3440");
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto r = option.rect.adjusted(6, 10, -6, -10);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(r, 8, 8);

        painter->setPen(fg);
        painter->drawText(r, Qt::AlignCenter, txt);
        painter->restore();
    }
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("BlockForge Launcher");
    setStatusBar(new QStatusBar(this));
    auto* statusNetwork = new QLabel("Network: Online", this);
    statusNetwork->setStyleSheet("color:#aab2bd;");
    auto* statusAuth = new QLabel("Auth: Offline", this);
    statusAuth->setStyleSheet("color:#aab2bd;");
    statusBar()->addPermanentWidget(statusNetwork);
    statusBar()->addPermanentWidget(statusAuth);
    statusBar()->showMessage("Ready");

    setStyleSheet(
        "QMainWindow{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0f141b,stop:1 #070a0f);color:#e7e7e7;}"
        "QWidget{color:#e7e7e7;font-family:Segoe UI;}"
        "QLineEdit,QPlainTextEdit,QComboBox{background:#121820;border:1px solid #2a3440;border-radius:6px;padding:6px;}"
        "QComboBox::drop-down{border:0px;}"
        "QPushButton{background:#1a232e;border:1px solid #2a3440;border-radius:8px;padding:8px 12px;}"
        "QPushButton:hover{background:#222e3b;}"
        "QPushButton:disabled{color:#777;background:#141a22;border-color:#202833;}"
        "QPushButton#primary{background:#ff7a18;border:1px solid #ff7a18;color:#111; font-weight:600;}"
        "QPushButton#primary:hover{background:#ff8a33;}"
        "QSlider::groove:horizontal{height:6px;background:#121820;border:1px solid #2a3440;border-radius:4px;}"
        "QSlider::sub-page:horizontal{background:#ff7a18;border-radius:4px;}"
        "QSlider::handle:horizontal{width:16px;margin:-6px 0px -6px 0px;background:#e7e7e7;border:2px solid #ff7a18;border-radius:8px;}"
        "QToolButton{background:transparent;border:0px;padding:10px;text-align:left;}"
        "QToolButton:checked{background:#141a22;border-left:3px solid #ff7a18;}"
        "QSplitter::handle{background:#0b0f14;}"
        "#sidebar{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0d1219,stop:1 #070a0f);border-right:1px solid #151c25;}"
        "#topBar{background:transparent;}"
        "#detailPanel{background:transparent;}"
        "QTreeWidget{background:#0f151d;border:1px solid #202833;border-radius:10px;alternate-background-color:#0d1219;}"
        "QHeaderView::section{background:#0f151d;border:0px;padding:8px;color:#aab2bd;}"
        "QTreeWidget::item{height:44px;}"
        "QTreeWidget::item:selected{background:#141a22;}"
        "QProgressBar{background:#121820;border:1px solid #2a3440;border-radius:6px;text-align:center;}"
        "QProgressBar::chunk{background:#ff7a18;border-radius:6px;}");

    auto* root = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* sidebar = new QWidget(root);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(190);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 12, 12, 12);
    sidebarLayout->setSpacing(6);

    auto* brandRow = new QWidget(sidebar);
    auto* brandRowLayout = new QHBoxLayout(brandRow);
    brandRowLayout->setContentsMargins(0, 0, 0, 8);
    brandRowLayout->setSpacing(10);
    auto* brandIcon = new QLabel(brandRow);
    brandIcon->setPixmap(QIcon(":/assets/icons/logo.svg").pixmap(28, 28));
    auto* brand = new QLabel("BlockForge", brandRow);
    QFont bf = brand->font();
    bf.setPointSize(14);
    bf.setBold(true);
    brand->setFont(bf);
    brandRowLayout->addWidget(brandIcon);
    brandRowLayout->addWidget(brand, 1);
    sidebarLayout->addWidget(brandRow);

    auto* navInstances = new QToolButton(sidebar);
    navInstances->setText("Instances");
    navInstances->setIcon(QIcon(":/assets/icons/nav_instances.svg"));
    navInstances->setCheckable(true);
    navInstances->setChecked(true);
    navInstances->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navAccounts = new QToolButton(sidebar);
    navAccounts->setText("Accounts");
    navAccounts->setIcon(QIcon(":/assets/icons/nav_accounts.svg"));
    navAccounts->setCheckable(true);
    navAccounts->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navJava = new QToolButton(sidebar);
    navJava->setText("Java");
    navJava->setIcon(QIcon(":/assets/icons/nav_java.svg"));
    navJava->setCheckable(true);
    navJava->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navMods = new QToolButton(sidebar);
    navMods->setText("Mods");
    navMods->setIcon(QIcon(":/assets/icons/nav_mods.svg"));
    navMods->setCheckable(true);
    navMods->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navLogs = new QToolButton(sidebar);
    navLogs->setText("Logs");
    navLogs->setIcon(QIcon(":/assets/icons/nav_logs.svg"));
    navLogs->setCheckable(true);
    navLogs->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navSettings = new QToolButton(sidebar);
    navSettings->setText("Settings");
    navSettings->setIcon(QIcon(":/assets/icons/nav_settings.svg"));
    navSettings->setCheckable(true);
    navSettings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    sidebarLayout->addWidget(navInstances);
    sidebarLayout->addWidget(navAccounts);
    sidebarLayout->addWidget(navJava);
    sidebarLayout->addWidget(navMods);
    sidebarLayout->addWidget(navLogs);
    sidebarLayout->addWidget(navSettings);
    sidebarLayout->addStretch(1);

    auto* pages = new QStackedWidget(root);
    auto* pageInstances = new QWidget(pages);
    auto* pageAccounts = new QWidget(pages);
    auto* pageJava = new QWidget(pages);
    auto* pageMods = new QWidget(pages);
    auto* pageLogs = new QWidget(pages);
    auto* pageSettings = new QWidget(pages);

    pages->addWidget(pageInstances);
    pages->addWidget(pageAccounts);
    pages->addWidget(pageJava);
    pages->addWidget(pageMods);
    pages->addWidget(pageLogs);
    pages->addWidget(pageSettings);

    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(pages, 1);
    setCentralWidget(root);

    auto setNav = [&](QToolButton* active, int index) {
        const auto buttons = QList<QToolButton*>{navInstances, navAccounts, navJava, navMods, navLogs, navSettings};
        for (auto* b : buttons) b->setChecked(b == active);
        pages->setCurrentIndex(index);
    };

    connect(navInstances, &QToolButton::clicked, this, [=]() { setNav(navInstances, 0); });
    connect(navAccounts, &QToolButton::clicked, this, [=]() { setNav(navAccounts, 1); });
    connect(navJava, &QToolButton::clicked, this, [=]() { setNav(navJava, 2); });
    connect(navMods, &QToolButton::clicked, this, [=]() { setNav(navMods, 3); });
    connect(navLogs, &QToolButton::clicked, this, [=]() { setNav(navLogs, 4); });
    connect(navSettings, &QToolButton::clicked, this, [=]() { setNav(navSettings, 5); });

    auto* logsView = new QPlainTextEdit(pageLogs);
    logsView->setReadOnly(true);
    auto* logsLayout = new QVBoxLayout(pageLogs);
    logsLayout->setContentsMargins(16, 16, 16, 16);
    logsLayout->addWidget(logsView, 1);

    auto appendLog = [logsView](const QString& s) {
        if (!s.trimmed().isEmpty()) logsView->appendPlainText(s.trimmed());
    };

    auto* instancesRoot = new QWidget(pageInstances);
    auto* instancesRootLayout = new QHBoxLayout(instancesRoot);
    instancesRootLayout->setContentsMargins(16, 16, 16, 16);
    instancesRootLayout->setSpacing(16);

    auto* centerCol = new QWidget(instancesRoot);
    auto* centerColLayout = new QVBoxLayout(centerCol);
    centerColLayout->setContentsMargins(0, 0, 0, 0);
    centerColLayout->setSpacing(12);

    auto* topBar = new QWidget(centerCol);
    topBar->setObjectName("topBar");
    auto* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(0, 0, 0, 0);
    auto* search = new QLineEdit(topBar);
    search->setPlaceholderText("Search instances...");
    search->addAction(QIcon(":/assets/icons/action_search.svg"), QLineEdit::LeadingPosition);
    topBarLayout->addWidget(search, 1);
    auto* btnCreate = new QPushButton("Create", topBar);
    btnCreate->setObjectName("primary");
    auto* btnImport = new QPushButton("Import", topBar);
    auto* btnRefresh = new QPushButton("Refresh", topBar);
    btnCreate->setIcon(QIcon(":/assets/icons/action_create.svg"));
    btnImport->setIcon(QIcon(":/assets/icons/action_import.svg"));
    btnRefresh->setIcon(QIcon(":/assets/icons/action_refresh.svg"));
    topBarLayout->addWidget(btnCreate);
    topBarLayout->addWidget(btnImport);
    topBarLayout->addWidget(btnRefresh);

    auto* list = new QTreeWidget(centerCol);
    list->setColumnCount(7);
    list->setHeaderLabels(QStringList() << "Icon"
                                        << "Name"
                                        << "Version"
                                        << "Loader"
                                        << "Last played"
                                        << "Status"
                                        << "");
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    list->setRootIsDecorated(false);
    list->setAlternatingRowColors(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setItemDelegateForColumn(5, new StatusDelegate(list));
    list->setIconSize(QSize(28, 28));
    list->setUniformRowHeights(true);

    centerColLayout->addWidget(topBar);
    centerColLayout->addWidget(list, 1);

    auto* detail = new QWidget(instancesRoot);
    detail->setObjectName("detailPanel");
    detail->setFixedWidth(420);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(18, 14, 18, 14);
    detailLayout->setSpacing(10);

    auto* heroRow = new QWidget(detail);
    auto* heroRowLayout = new QHBoxLayout(heroRow);
    heroRowLayout->setContentsMargins(0, 0, 0, 0);
    heroRowLayout->setSpacing(14);

    auto* heroIcon = new QLabel(heroRow);
    heroIcon->setFixedSize(96, 96);
    heroIcon->setAlignment(Qt::AlignCenter);

    auto* heroText = new QWidget(heroRow);
    auto* heroTextLayout = new QVBoxLayout(heroText);
    heroTextLayout->setContentsMargins(0, 0, 0, 0);
    heroTextLayout->setSpacing(6);

    auto* title = new QLabel("Select an instance", heroText);
    QFont tf = title->font();
    tf.setPointSize(16);
    tf.setBold(true);
    title->setFont(tf);
    heroTextLayout->addWidget(title);

    auto* subtitle = new QLabel("", heroText);
    subtitle->setStyleSheet("color:#aab2bd;");
    heroTextLayout->addWidget(subtitle);
    heroTextLayout->addStretch(1);

    heroRowLayout->addWidget(heroIcon);
    heroRowLayout->addWidget(heroText, 1);
    detailLayout->addWidget(heroRow);

    auto* btnPlay = new QPushButton("Play", detail);
    btnPlay->setObjectName("primary");
    btnPlay->setIcon(QIcon(":/assets/icons/action_play.svg"));
    btnPlay->setMinimumHeight(44);
    detailLayout->addWidget(btnPlay);

    auto* actionRow = new QWidget(detail);
    auto* actionRowLayout = new QHBoxLayout(actionRow);
    actionRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* btnEdit = new QPushButton("Edit", actionRow);
    auto* btnFolder = new QPushButton("Folder", actionRow);
    auto* btnDelete = new QPushButton("Delete", actionRow);
    btnEdit->setIcon(QIcon(":/assets/icons/action_edit.svg"));
    btnFolder->setIcon(QIcon(":/assets/icons/action_folder.svg"));
    btnDelete->setIcon(QIcon(":/assets/icons/action_delete.svg"));
    btnDelete->setStyleSheet("QPushButton{background:#2a1616;border:1px solid #4a2020;}QPushButton:hover{background:#3a1a1a;}");
    actionRowLayout->addWidget(btnEdit);
    actionRowLayout->addWidget(btnFolder);
    actionRowLayout->addWidget(btnDelete);
    detailLayout->addWidget(actionRow);

    auto* form = new QWidget(detail);
    auto* formLayout = new QFormLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignTop);

    auto* labelMc = new QLabel("-", form);
    auto* labelLoader = new QLabel("-", form);
    auto* javaPath = new QLineEdit(form);
    auto* javaBrowse = new QPushButton("...", form);
    javaBrowse->setFixedWidth(36);
    auto* javaRow = new QWidget(form);
    auto* javaRowLayout = new QHBoxLayout(javaRow);
    javaRowLayout->setContentsMargins(0, 0, 0, 0);
    javaRowLayout->addWidget(javaPath, 1);
    javaRowLayout->addWidget(javaBrowse);

    auto* ramRow = new QWidget(form);
    auto* ramLayout = new QHBoxLayout(ramRow);
    ramLayout->setContentsMargins(0, 0, 0, 0);
    auto* ramSlider = new QSlider(Qt::Horizontal, ramRow);
    ramSlider->setRange(1024, 16384);
    ramSlider->setSingleStep(256);
    ramSlider->setPageStep(1024);
    auto* ramLabel = new QLabel("4096 MB", ramRow);
    ramLabel->setMinimumWidth(90);
    ramLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ramLayout->addWidget(ramSlider, 1);
    ramLayout->addWidget(ramLabel);

    auto* notes = new QPlainTextEdit(form);
    notes->setMaximumBlockCount(1000);
    notes->setFixedHeight(120);
    auto* notesMeta = new QLabel("0 / 500", form);
    notesMeta->setStyleSheet("color:#aab2bd;");
    notesMeta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    formLayout->addRow("Minecraft version", labelMc);
    formLayout->addRow("Loader", labelLoader);
    formLayout->addRow("Java path", javaRow);
    formLayout->addRow("RAM allocation", ramRow);
    formLayout->addRow("Notes", notes);
    formLayout->addRow("", notesMeta);

    detailLayout->addWidget(form, 1);

    auto* progress = new QProgressBar(detail);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setVisible(false);
    detailLayout->addWidget(progress);

    auto* instancesLayout = new QVBoxLayout(pageInstances);
    instancesLayout->setContentsMargins(0, 0, 0, 0);
    instancesRootLayout->addWidget(centerCol, 1);
    instancesRootLayout->addWidget(detail);
    instancesLayout->addWidget(instancesRoot, 1);

    auto* modsLayout = new QVBoxLayout(pageMods);
    modsLayout->setContentsMargins(16, 16, 16, 16);
    auto* modsTop = new QWidget(pageMods);
    auto* modsTopLayout = new QHBoxLayout(modsTop);
    modsTopLayout->setContentsMargins(0, 0, 0, 0);
    auto* modsRefresh = new QPushButton("Refresh", modsTop);
    auto* modsOpen = new QPushButton("Open folder", modsTop);
    modsTopLayout->addWidget(new QLabel("Mods", modsTop));
    modsTopLayout->addStretch(1);
    modsTopLayout->addWidget(modsRefresh);
    modsTopLayout->addWidget(modsOpen);
    auto* modsList = new QListWidget(pageMods);
    modsLayout->addWidget(modsTop);
    modsLayout->addWidget(modsList, 1);

    auto* accountsLayout = new QVBoxLayout(pageAccounts);
    accountsLayout->setContentsMargins(16, 16, 16, 16);
    accountsLayout->setSpacing(10);
    auto* msClientId = new QLineEdit(pageAccounts);
    msClientId->setPlaceholderText("Microsoft client id");
    auto* addAccount = new QPushButton("Add account", pageAccounts);
    auto* accountCombo = new QComboBox(pageAccounts);
    auto* logout = new QPushButton("Logout", pageAccounts);
    accountsLayout->addWidget(new QLabel("Microsoft client id", pageAccounts));
    auto* msRow = new QWidget(pageAccounts);
    auto* msRowLayout = new QHBoxLayout(msRow);
    msRowLayout->setContentsMargins(0, 0, 0, 0);
    msRowLayout->addWidget(msClientId, 1);
    msRowLayout->addWidget(addAccount);
    accountsLayout->addWidget(msRow);
    auto* accRow = new QWidget(pageAccounts);
    auto* accRowLayout = new QHBoxLayout(accRow);
    accRowLayout->setContentsMargins(0, 0, 0, 0);
    accRowLayout->addWidget(accountCombo, 1);
    accRowLayout->addWidget(logout);
    accountsLayout->addWidget(new QLabel("Account", pageAccounts));
    accountsLayout->addWidget(accRow);
    accountsLayout->addStretch(1);

    auto* javaLayout = new QVBoxLayout(pageJava);
    javaLayout->setContentsMargins(16, 16, 16, 16);
    javaLayout->addWidget(new QLabel("Java is selected automatically (bundled JRE 8/17/21). You can override Java path per instance.", pageJava));
    javaLayout->addStretch(1);

    auto* settingsLayout = new QVBoxLayout(pageSettings);
    settingsLayout->setContentsMargins(16, 16, 16, 16);
    settingsLayout->setSpacing(10);
    auto* nick = new QLineEdit(pageSettings);
    auto* globalJavaPath = new QLineEdit(pageSettings);
    globalJavaPath->setPlaceholderText("javaw.exe or java.exe path (optional)");
    auto* globalRam = new QLineEdit(pageSettings);
    globalRam->setPlaceholderText("4096");
    settingsLayout->addWidget(new QLabel("Nickname (offline)", pageSettings));
    settingsLayout->addWidget(nick);
    settingsLayout->addWidget(new QLabel("Java path (global override)", pageSettings));
    settingsLayout->addWidget(globalJavaPath);
    settingsLayout->addWidget(new QLabel("Default RAM (MB)", pageSettings));
    settingsLayout->addWidget(globalRam);
    settingsLayout->addWidget(new QLabel(QString("Data dir: %1").arg(QString::fromStdString(AppPaths::dataDir().string())), pageSettings));
    settingsLayout->addStretch(1);

    auto settings = appSettings();
    nick->setText(QString::fromStdString(settings.get("offline.name").value_or("BlockForgePlayer")));
    globalJavaPath->setText(QString::fromStdString(settings.get("java.path").value_or("")));
    globalRam->setText(QString::fromStdString(settings.get("java.maxRamMb").value_or("4096")));
    msClientId->setText(QString::fromStdString(settings.get("ms.clientId").value_or("")));

    auto instancesById = std::make_shared<std::unordered_map<std::string, Instance>>();
    auto selectedInstanceId = std::make_shared<QString>();

    auto instanceKey = [](const QString& id, const QString& key) {
        return QString("instance.%1.%2").arg(id, key).toStdString();
    };

    auto updateDetails = [=]() {
        if (selectedInstanceId->isEmpty())
        {
            title->setText("Select an instance");
            subtitle->setText("");
            labelMc->setText("-");
            labelLoader->setText("-");
            btnPlay->setEnabled(false);
            btnEdit->setEnabled(false);
            btnFolder->setEnabled(false);
            btnDelete->setEnabled(false);
            javaPath->setEnabled(false);
            javaBrowse->setEnabled(false);
            ramSlider->setEnabled(false);
            notes->setEnabled(false);
            heroIcon->setPixmap(QPixmap());
            notesMeta->setText("0 / 500");
            return;
        }

        const auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end())
        {
            selectedInstanceId->clear();
            title->setText("Select an instance");
            subtitle->setText("");
            labelMc->setText("-");
            labelLoader->setText("-");
            btnPlay->setEnabled(false);
            btnEdit->setEnabled(false);
            btnFolder->setEnabled(false);
            btnDelete->setEnabled(false);
            javaPath->setEnabled(false);
            javaBrowse->setEnabled(false);
            ramSlider->setEnabled(false);
            notes->setEnabled(false);
            heroIcon->setPixmap(QPixmap());
            notesMeta->setText("0 / 500");
            return;
        }
        const auto& inst = it->second;
        title->setText(QString::fromStdString(inst.name));
        subtitle->setText(QString("%1 / %2").arg(QString::fromStdString(inst.minecraftVersion), QString::fromStdString(toString(inst.loaderType))));
        labelMc->setText(QString::fromStdString(inst.minecraftVersion));
        labelLoader->setText(QString::fromStdString(toString(inst.loaderType)));

        QIcon ico(":/assets/icons/inst_grass.svg");
        if (inst.loaderType == LoaderType::Forge) ico = QIcon(":/assets/icons/inst_obsidian.svg");
        else if (inst.loaderType == LoaderType::Fabric) ico = QIcon(":/assets/icons/inst_stone.svg");
        heroIcon->setPixmap(ico.pixmap(96, 96));

        const QSignalBlocker b1(javaPath);
        const QSignalBlocker b2(ramSlider);
        const QSignalBlocker b3(notes);

        auto settings = appSettings();
        javaPath->setText(QString::fromStdString(settings.get(instanceKey(*selectedInstanceId, "javaPath")).value_or("")));

        const auto rm = settings.get(instanceKey(*selectedInstanceId, "ramMb")).value_or(settings.get("java.maxRamMb").value_or("4096"));
        bool ok = false;
        const int rmi = QString::fromStdString(rm).toInt(&ok);
        const int clamped = ok ? std::max(1024, std::min(16384, rmi)) : 4096;
        ramSlider->setValue(clamped);
        ramLabel->setText(QString("%1 MB").arg(clamped));

        const auto nt = QString::fromStdString(settings.get(instanceKey(*selectedInstanceId, "notes")).value_or(""));
        notes->setPlainText(nt.left(500));
        notesMeta->setText(QString("%1 / 500").arg(std::min(500, static_cast<int>(nt.size()))));

        btnPlay->setEnabled(true);
        btnEdit->setEnabled(true);
        btnFolder->setEnabled(true);
        btnDelete->setEnabled(true);
        javaPath->setEnabled(true);
        javaBrowse->setEnabled(true);
        ramSlider->setEnabled(true);
        notes->setEnabled(true);
    };

    auto refreshInstances = [=]() {
        instancesById->clear();
        const QSignalBlocker bl(list);
        list->clear();

        InstanceStore store(AppPaths::instancesDir());
        const auto items = store.list();
        auto settings = appSettings();
        const auto last = QString::fromStdString(settings.get("ui.lastInstanceId").value_or(""));
        for (const auto& inst : items)
        {
            (*instancesById)[inst.id] = inst;

            auto* row = new QTreeWidgetItem();
            QIcon ico(":/assets/icons/inst_grass.svg");
            if (inst.loaderType == LoaderType::Forge) ico = QIcon(":/assets/icons/inst_obsidian.svg");
            else if (inst.loaderType == LoaderType::Fabric) ico = QIcon(":/assets/icons/inst_stone.svg");
            row->setIcon(0, ico);
            row->setText(1, QString::fromStdString(inst.name));
            row->setText(2, QString::fromStdString(inst.minecraftVersion));
            row->setText(3, QString::fromStdString(toString(inst.loaderType)));

            const auto lastKey = QString("instance.%1.lastPlayedMs").arg(QString::fromStdString(inst.id)).toStdString();
            auto settings = appSettings();
            const auto last = settings.get(lastKey).value_or("");
            if (!last.empty())
            {
                bool ok = false;
                const qint64 ms = QString::fromStdString(last).toLongLong(&ok);
                if (ok && ms > 0) row->setText(4, QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm"));
            }

            const auto jsonPath = QDir(QString::fromStdString(AppPaths::dataDir().string()))
                                      .filePath(QString("versions/%1/%1.json").arg(QString::fromStdString(inst.minecraftVersion)));
            row->setText(5, QFileInfo::exists(jsonPath) ? "Ready" : "Not installed");
            row->setText(6, "⋮");
            row->setTextAlignment(6, Qt::AlignCenter);
            row->setData(0, Qt::UserRole, QString::fromStdString(inst.id));
            list->addTopLevelItem(row);
        }

        if (!last.isEmpty())
        {
            for (int i = 0; i < list->topLevelItemCount(); ++i)
            {
                auto* it = list->topLevelItem(i);
                if (it->data(0, Qt::UserRole).toString() == last)
                {
                    *selectedInstanceId = last;
                    list->setCurrentItem(it);
                    break;
                }
            }
        }
        if (selectedInstanceId->isEmpty() && list->topLevelItemCount() > 0)
        {
            *selectedInstanceId = list->topLevelItem(0)->data(0, Qt::UserRole).toString();
            list->setCurrentItem(list->topLevelItem(0));
        }
        if (list->topLevelItemCount() == 0)
        {
            selectedInstanceId->clear();
        }
        updateDetails();
    };

    auto refreshAccounts = [=]() {
        accountCombo->blockSignals(true);
        accountCombo->clear();

        AccountStore store(QString::fromStdString(AppPaths::dataDir().string()));
        QString error;
        const auto accounts = store.listAccounts(error);
        if (!error.isEmpty())
        {
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
            if (!activeId.isEmpty() && a.accountId == activeId) activeIndex = idx;
            ++idx;
        }
        if (activeIndex >= 0) accountCombo->setCurrentIndex(activeIndex);

        const bool hasAny = !accounts.isEmpty();
        accountCombo->setEnabled(hasAny);
        logout->setEnabled(hasAny);
        accountCombo->blockSignals(false);

        statusAuth->setText(activeIndex >= 0 ? "Auth: Signed in" : "Auth: Offline");
    };

    auto refreshMods = [=]() {
        modsList->blockSignals(true);
        modsList->clear();
        modsList->blockSignals(false);
        if (selectedInstanceId->isEmpty()) return;

        const auto gameDir =
            QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(*selectedInstanceId));
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

    connect(search, &QLineEdit::textChanged, this, [=](const QString& t) {
        for (int i = 0; i < list->topLevelItemCount(); ++i)
        {
            auto* it = list->topLevelItem(i);
            const auto name = it->text(1);
            it->setHidden(!t.trimmed().isEmpty() && !name.contains(t, Qt::CaseInsensitive));
        }
    });

    connect(list, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur) return;
        *selectedInstanceId = cur->data(0, Qt::UserRole).toString();
        auto settings = appSettings();
        settings.set("ui.lastInstanceId", selectedInstanceId->toStdString());
        updateDetails();
        refreshMods();
    });

    connect(list, &QTreeWidget::itemClicked, this, [=](QTreeWidgetItem* item, int column) {
        if (!item) return;
        if (column != 6) return;
        const auto id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty()) return;

        QMenu menu(this);
        auto* actPlay = menu.addAction("Play");
        auto* actEdit = menu.addAction("Edit");
        auto* actFolder = menu.addAction("Folder");
        auto* actExport = menu.addAction("Export");
        menu.addSeparator();
        auto* actDelete = menu.addAction("Delete");

        const auto chosen = menu.exec(QCursor::pos());
        if (!chosen) return;

        *selectedInstanceId = id;
        updateDetails();
        refreshMods();

        if (chosen == actPlay)
        {
            btnPlay->click();
            return;
        }
        if (chosen == actEdit)
        {
            btnEdit->click();
            return;
        }
        if (chosen == actFolder)
        {
            btnFolder->click();
            return;
        }
        if (chosen == actDelete)
        {
            btnDelete->click();
            return;
        }
        if (chosen == actExport)
        {
            const auto outDir = QFileDialog::getExistingDirectory(nullptr, "Export to folder");
            if (outDir.isEmpty()) return;
            const auto src = AppPaths::instancesDir() / selectedInstanceId->toStdString();
            const auto dst = std::filesystem::path(outDir.toStdString()) / selectedInstanceId->toStdString();
            if (!copyTree(src, dst))
            {
                appendLog("Export failed.");
                return;
            }
            appendLog(QString("Exported to: %1").arg(QString::fromStdString(dst.string())));
        }
    });

    connect(btnRefresh, &QPushButton::clicked, this, [=]() {
        refreshInstances();
        refreshMods();
    });

    connect(btnImport, &QPushButton::clicked, this, [=]() {
        const auto dir = QFileDialog::getExistingDirectory(nullptr, "Import instance folder");
        if (dir.isEmpty()) return;

        const auto src = std::filesystem::path(dir.toStdString());
        if (!std::filesystem::exists(src))
        {
            appendLog("Source not found.");
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
            appendLog("Import failed.");
            return;
        }
        *selectedInstanceId = QString::fromStdString(inst.id);
        appendLog(QString("Imported as instance: %1").arg(*selectedInstanceId));
        refreshInstances();
        refreshMods();
    });

    connect(btnCreate, &QPushButton::clicked, this, [=]() {
        bool ok = false;
        const auto name = QInputDialog::getText(this, "Create instance", "Name:", QLineEdit::Normal, "New Instance", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        const auto mc = QInputDialog::getText(this, "Create instance", "Minecraft version:", QLineEdit::Normal, "1.20.1", &ok);
        if (!ok || mc.trimmed().isEmpty()) return;

        QStringList items;
        items << "vanilla"
              << "fabric"
              << "forge";
        const auto loader = QInputDialog::getItem(this, "Create instance", "Loader:", items, 0, false, &ok);
        if (!ok || loader.isEmpty()) return;

        InstanceStore store(AppPaths::instancesDir());
        const auto lt = loaderTypeFromString(loader.toStdString()).value_or(LoaderType::Vanilla);
        const auto inst = store.create(name.toStdString(), mc.toStdString(), lt);
        *selectedInstanceId = QString::fromStdString(inst.id);
        appendLog(QString("Created instance: %1").arg(*selectedInstanceId));
        refreshInstances();
        refreshMods();
    });

    connect(btnFolder, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto gameDir =
            QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(*selectedInstanceId));
        QDir().mkpath(gameDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(gameDir));
    });

    connect(btnDelete, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto r = QMessageBox::question(this, "Delete instance", "Delete selected instance?");
        if (r != QMessageBox::Yes) return;
        std::error_code ec;
        std::filesystem::remove_all(AppPaths::instancesDir() / selectedInstanceId->toStdString(), ec);
        selectedInstanceId->clear();
        refreshInstances();
        refreshMods();
    });

    connect(btnEdit, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;
        bool ok = false;
        const auto name = QInputDialog::getText(this, "Rename instance", "Name:", QLineEdit::Normal,
                                               QString::fromStdString(it->second.name), &ok);
        if (!ok || name.trimmed().isEmpty()) return;

        const auto path = AppPaths::instancesDir() / it->second.id / "instance.bf";
        std::ifstream in(path, std::ios::binary);
        if (!in.good()) return;
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        in.close();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (auto& l : lines)
        {
            if (l.rfind("name=", 0) == 0) out << "name=" << name.toStdString() << "\n";
            else out << l << "\n";
        }
        out.close();
        refreshInstances();
    });

    connect(javaBrowse, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto file = QFileDialog::getOpenFileName(this, "Select java", QString(), "Java (javaw.exe java.exe)");
        if (file.isEmpty()) return;
        javaPath->setText(file);
        auto settings = appSettings();
        settings.set(instanceKey(*selectedInstanceId, "javaPath"), file.trimmed().toStdString());
    });

    connect(javaPath, &QLineEdit::editingFinished, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        auto settings = appSettings();
        settings.set(instanceKey(*selectedInstanceId, "javaPath"), javaPath->text().trimmed().toStdString());
    });

    connect(ramSlider, &QSlider::valueChanged, this, [=](int v) { ramLabel->setText(QString("%1 MB").arg(v)); });

    connect(ramSlider, &QSlider::sliderReleased, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        auto settings = appSettings();
        settings.set(instanceKey(*selectedInstanceId, "ramMb"), QString::number(ramSlider->value()).toStdString());
    });

    connect(notes, &QPlainTextEdit::textChanged, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        auto t = notes->toPlainText();
        if (t.size() > 500)
        {
            t = t.left(500);
            const QSignalBlocker b(notes);
            notes->setPlainText(t);
        }
        notesMeta->setText(QString("%1 / 500").arg(t.size()));
        auto settings = appSettings();
        settings.set(instanceKey(*selectedInstanceId, "notes"), t.toStdString());
    });

    connect(modsRefresh, &QPushButton::clicked, this, [=]() { refreshMods(); });
    connect(modsOpen, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto modsDir = QDir(QString::fromStdString(AppPaths::dataDir().string()))
                                 .filePath(QString("instances/%1/game/mods").arg(*selectedInstanceId));
        QDir().mkpath(modsDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(modsDir));
    });

    connect(modsList, &QListWidget::itemChanged, this, [=](QListWidgetItem* item) {
        if (!item) return;
        if (selectedInstanceId->isEmpty()) return;
        const auto modsDir = QDir(QString::fromStdString(AppPaths::dataDir().string()))
                                 .filePath(QString("instances/%1/game/mods").arg(*selectedInstanceId));
        QDir().mkpath(modsDir);

        const auto src = item->data(Qt::UserRole).toString();
        const bool wantEnabled = item->checkState() == Qt::Checked;
        QString dst = src;
        if (wantEnabled)
        {
            if (dst.endsWith(".disabled")) dst = dst.left(dst.size() - QString(".disabled").size());
        }
        else
        {
            if (!dst.endsWith(".disabled")) dst = dst + ".disabled";
        }

        const auto srcPath = QDir(modsDir).filePath(src);
        const auto dstPath = QDir(modsDir).filePath(dst);
        if (QFileInfo::exists(srcPath) && srcPath != dstPath) QFile::rename(srcPath, dstPath);
        item->setData(Qt::UserRole, dst);
    });

    connect(msClientId, &QLineEdit::editingFinished, this, [=]() {
        auto settings = appSettings();
        settings.set("ms.clientId", msClientId->text().trimmed().toStdString());
    });

    connect(accountCombo, &QComboBox::currentIndexChanged, this, [=](int) {
        if (accountCombo->currentIndex() < 0) return;
        const auto accountId = accountCombo->currentData().toString();
        AccountStore store(QString::fromStdString(AppPaths::dataDir().string()));
        QString error;
        store.setActiveAccount(accountId, error);
    });

    connect(logout, &QPushButton::clicked, this, [=]() {
        if (accountCombo->currentIndex() < 0) return;
        const auto accountId = accountCombo->currentData().toString();
        AccountStore store(QString::fromStdString(AppPaths::dataDir().string()));
        QString error;
        if (!store.removeAccount(accountId, error))
        {
            appendLog(QString("Logout failed: %1").arg(error));
        }
        refreshAccounts();
    });

    connect(addAccount, &QPushButton::clicked, this, [=]() {
        const auto clientId = msClientId->text().trimmed();
        if (clientId.isEmpty())
        {
            appendLog("Microsoft client id is empty.");
            return;
        }

        addAccount->setEnabled(false);
        appendLog("Starting Microsoft device code flow...");

        auto* thread = new QThread(this);
        auto* worker = new AuthWorker();
        worker->configure(QString::fromStdString(AppPaths::dataDir().string()), clientId);
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::started, worker, &AuthWorker::run);
        QObject::connect(worker, &AuthWorker::logLine, logsView, [=](const QString& line) { appendLog(line); });
        QObject::connect(worker, &AuthWorker::deviceCodeReady, logsView, [=](const DeviceCode& code) {
            appendLog(QString("Open: %1").arg(code.verificationUri));
            appendLog(QString("Code: %1").arg(code.userCode));
        });
        QObject::connect(worker, &AuthWorker::accountReady, logsView, [=](const QString& name) {
            appendLog(QString("Account added: %1").arg(name));
            addAccount->setEnabled(true);
            refreshAccounts();
            thread->quit();
        });
        QObject::connect(worker, &AuthWorker::failed, logsView, [=](const QString& e) {
            appendLog(QString("Auth failed: %1").arg(e));
            addAccount->setEnabled(true);
            thread->quit();
        });
        QObject::connect(worker, &AuthWorker::finished, thread, [=]() {
            worker->deleteLater();
            thread->deleteLater();
        });

        thread->start();
    });

    connect(nick, &QLineEdit::editingFinished, this, [=]() {
        auto settings = appSettings();
        settings.set("offline.name", nick->text().trimmed().toStdString());
    });
    connect(globalJavaPath, &QLineEdit::editingFinished, this, [=]() {
        auto settings = appSettings();
        settings.set("java.path", globalJavaPath->text().trimmed().toStdString());
    });
    connect(globalRam, &QLineEdit::editingFinished, this, [=]() {
        auto settings = appSettings();
        settings.set("java.maxRamMb", globalRam->text().trimmed().toStdString());
    });

#if defined(BLOCKFORGE_BUILD_MC)
    connect(btnPlay, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;

        progress->setVisible(true);
        progress->setValue(0);
        btnPlay->setEnabled(false);
        statusBar()->showMessage("Launching...");

        const auto profile = makeOfflineProfile(nick->text().trimmed().toStdString());
        AuthSession session;
        session.playerName = QString::fromStdString(profile.name);
        session.uuid = QString::fromStdString(profile.uuid);
        session.accessToken = "0";
        session.userType = "legacy";
        session.xuid = "0";
        session.clientId = "0";

        LaunchOptions opts;
        opts.javaPath = javaPath->text().trimmed();
        opts.maxRamMb = ramSlider->value();

        auto* thread = new QThread(this);
        auto* worker = new LaunchWorker();
        worker->configure(QString::fromStdString(AppPaths::dataDir().string()), it->second, session, opts);
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::started, worker, &LaunchWorker::run);
        QObject::connect(worker, &LaunchWorker::logLine, logsView, [=](const QString& line) { appendLog(line); });
        QObject::connect(worker, &LaunchWorker::progress, logsView, [=](const QString& phase, int cur, int total) {
            if (phase != "assets") return;
            if (total <= 0) return;
            const int pct = std::max(0, std::min(100, (cur * 100) / total));
            static int lastPct = -1;
            if (pct == lastPct) return;
            lastPct = pct;
            progress->setValue(pct);
            statusBar()->showMessage(QString("Downloading assets: %1%").arg(pct));

            auto* curItem = list->currentItem();
            if (curItem)
            {
                curItem->setText(5, QString("Downloading %1%").arg(pct));
            }
        });
        QObject::connect(worker, &LaunchWorker::failed, logsView, [=](const QString& e) {
            appendLog(e);
            btnPlay->setEnabled(true);
            progress->setVisible(false);
            statusBar()->showMessage("Failed");
            thread->quit();
        });
        QObject::connect(worker, &LaunchWorker::readyToLaunch, logsView, [=](const LaunchCommand& cmd) {
            auto* proc = new QProcess(logsView);
            proc->setProgram(cmd.program);
            proc->setArguments(cmd.args);
            proc->setProcessChannelMode(QProcess::MergedChannels);

            QObject::connect(proc, &QProcess::readyRead, logsView, [=]() {
                const auto out = QString::fromUtf8(proc->readAll());
                if (!out.trimmed().isEmpty()) appendLog(out.trimmed());
            });
            QObject::connect(proc, &QProcess::finished, logsView, [=](int code) {
                appendLog(QString("Minecraft exited: %1").arg(code));
                btnPlay->setEnabled(true);
                progress->setVisible(false);
                statusBar()->showMessage("Ready");
                proc->deleteLater();
            });

            auto settings = appSettings();
            settings.set(QString("instance.%1.lastPlayedMs").arg(*selectedInstanceId).toStdString(),
                         QString::number(QDateTime::currentMSecsSinceEpoch()).toStdString());
            refreshInstances();

            appendLog(QString("Launching: %1").arg(cmd.program));
            statusBar()->showMessage("Running");
            auto* curItem = list->currentItem();
            if (curItem) curItem->setText(5, "Ready");
            proc->start();
            thread->quit();
        });
        QObject::connect(worker, &LaunchWorker::finished, thread, [=]() {
            worker->deleteLater();
            thread->deleteLater();
        });

        thread->start();
    });
#else
    btnPlay->setEnabled(false);
#endif
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    const auto threads = findChildren<QThread*>();
    for (auto* t : threads)
    {
        if (!t) continue;
        if (!t->isRunning()) continue;
        t->quit();
        if (!t->wait(1500))
        {
            t->terminate();
            t->wait(1500);
        }
    }
    QMainWindow::closeEvent(event);
}
}
