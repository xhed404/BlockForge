#include "ui/MainWindow.h"

#include "auth/AccountStore.h"
#include "core/AppPaths.h"
#include "core/FsUtil.h"
#include "core/InstanceStore.h"
#include "core/OfflineAuth.h"
#include "core/Settings.h"
#include "ui/AuthWorker.h"
#include "ui/LaunchWorker.h"
#include "ui/ModrinthDialog.h"
#include "ui/TokenRefreshWorker.h"
#include "ui/Theme.h"

#include <QCloseEvent>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCheckBox>
#include <QColorDialog>
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
#include <QTimer>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QSpinBox>
#include <QSplitter>
#include <QStyleOptionViewItem>
#include <QStyleOptionViewItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#if defined(BLOCKFORGE_BUILD_MC)
#include "minecraft/Zip.h"
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
enum class UiInstanceStatus
{
    Ready = 1,
    Downloading = 2,
    Broken = 3,
    NotInstalled = 4,
};

static QString bundledJavaPath(int major, bool preferGui)
{
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
    Q_UNUSED(preferGui);
    const auto binDir = QDir(appDir).filePath(QString("jre/%1/bin").arg(major));
    const auto java = QDir(binDir).filePath("java");
    if (QFileInfo::exists(java)) return java;
    return {};
#endif
}

static QStringList splitJvmArgsLines(const QString& s)
{
    QStringList out;
    const auto lines = s.split('\n');
    for (const auto& ln : lines)
    {
        const auto t = ln.trimmed();
        if (!t.isEmpty()) out.push_back(t);
    }
    return out;
}

class StatusDelegate final : public QStyledItemDelegate
{
public:
    explicit StatusDelegate(const Theme& theme, QObject* parent = nullptr)
        : QStyledItemDelegate(parent),
          m_theme(theme)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const auto txt = opt.text;
        opt.text.clear();

        QStyledItemDelegate::paint(painter, opt, index);

        if (txt.isEmpty()) return;

        QColor fg(m_theme.mutedText);
        QColor bg("#1a232e");

        const auto st = static_cast<UiInstanceStatus>(index.data(Qt::UserRole + 1).toInt(0));
        if (st == UiInstanceStatus::Ready)
        {
            fg = QColor("#c9f7c5");
            bg = QColor("#164a23");
        }
        else if (st == UiInstanceStatus::Downloading)
        {
            fg = QColor("#d9ecff");
            bg = QColor("#153a57");
        }
        else if (st == UiInstanceStatus::Broken)
        {
            fg = QColor("#ffd0d0");
            bg = QColor("#4a2020");
        }
        else if (st == UiInstanceStatus::NotInstalled)
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

private:
    Theme m_theme;
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("BlockForge Launcher");
    setStatusBar(new QStatusBar(this));

    const auto settings0 = appSettings();
    const auto theme = Theme::fromSettings(settings0);
    const auto lang = uiLangFromSettings(settings0);
    auto T = [lang](const QString& ru, const QString& en) { return lang == UiLang::Ru ? ru : en; };

    auto* statusNetwork = new QLabel(T("Сеть: <span style=\"color:#44d07a;\">●</span> Онлайн",
                                       "Network: <span style=\"color:#44d07a;\">●</span> Online"),
                                     this);
    statusNetwork->setStyleSheet(QString("color:%1;").arg(theme.mutedText));
    auto* statusAuth = new QLabel(T("Аккаунт: <span style=\"color:#737b86;\">●</span> Оффлайн",
                                    "Auth: <span style=\"color:#737b86;\">●</span> Offline"),
                                  this);
    statusAuth->setStyleSheet(QString("color:%1;").arg(theme.mutedText));
    statusBar()->addPermanentWidget(statusNetwork);
    statusBar()->addPermanentWidget(statusAuth);
    statusBar()->showMessage(T("Готово", "Ready"));
    setStyleSheet(theme.toStyleSheet());

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
    navInstances->setText(T("Инстансы", "Instances"));
    navInstances->setIcon(QIcon(":/assets/icons/nav_instances_on.svg"));
    navInstances->setCheckable(true);
    navInstances->setChecked(true);
    navInstances->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navAccounts = new QToolButton(sidebar);
    navAccounts->setText(T("Аккаунты", "Accounts"));
    navAccounts->setIcon(QIcon(":/assets/icons/nav_accounts.svg"));
    navAccounts->setCheckable(true);
    navAccounts->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navJava = new QToolButton(sidebar);
    navJava->setText("Java");
    navJava->setIcon(QIcon(":/assets/icons/nav_java.svg"));
    navJava->setCheckable(true);
    navJava->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navMods = new QToolButton(sidebar);
    navMods->setText(T("Моды", "Mods"));
    navMods->setIcon(QIcon(":/assets/icons/nav_mods.svg"));
    navMods->setCheckable(true);
    navMods->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navLogs = new QToolButton(sidebar);
    navLogs->setText(T("Логи", "Logs"));
    navLogs->setIcon(QIcon(":/assets/icons/nav_logs.svg"));
    navLogs->setCheckable(true);
    navLogs->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* navSettings = new QToolButton(sidebar);
    navSettings->setText(T("Настройки", "Settings"));
    navSettings->setIcon(QIcon(":/assets/icons/nav_settings.svg"));
    navSettings->setCheckable(true);
    navSettings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const auto navIconSize = QSize(22, 22);
    navInstances->setIconSize(navIconSize);
    navAccounts->setIconSize(navIconSize);
    navJava->setIconSize(navIconSize);
    navMods->setIconSize(navIconSize);
    navLogs->setIconSize(navIconSize);
    navSettings->setIconSize(navIconSize);

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

    auto setNav = [pages, navInstances, navAccounts, navJava, navMods, navLogs, navSettings](QToolButton* active, int index) {
        const auto buttons = QList<QToolButton*>{navInstances, navAccounts, navJava, navMods, navLogs, navSettings};
        for (auto* b : buttons) b->setChecked(b == active);
        navInstances->setIcon(QIcon(navInstances->isChecked() ? ":/assets/icons/nav_instances_on.svg" : ":/assets/icons/nav_instances.svg"));
        navAccounts->setIcon(QIcon(navAccounts->isChecked() ? ":/assets/icons/nav_accounts_on.svg" : ":/assets/icons/nav_accounts.svg"));
        navJava->setIcon(QIcon(navJava->isChecked() ? ":/assets/icons/nav_java_on.svg" : ":/assets/icons/nav_java.svg"));
        navMods->setIcon(QIcon(navMods->isChecked() ? ":/assets/icons/nav_mods_on.svg" : ":/assets/icons/nav_mods.svg"));
        navLogs->setIcon(QIcon(navLogs->isChecked() ? ":/assets/icons/nav_logs_on.svg" : ":/assets/icons/nav_logs.svg"));
        navSettings->setIcon(QIcon(navSettings->isChecked() ? ":/assets/icons/nav_settings_on.svg" : ":/assets/icons/nav_settings.svg"));
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
    search->setPlaceholderText(T("Поиск инстансов...", "Search instances..."));
    search->addAction(QIcon(":/assets/icons/action_search.svg"), QLineEdit::LeadingPosition);
    topBarLayout->addWidget(search, 1);
    auto* btnCreate = new QPushButton(T("Создать", "Create"), topBar);
    btnCreate->setObjectName("primary");
    auto* btnImport = new QPushButton(T("Импорт", "Import"), topBar);
    auto* btnRefresh = new QPushButton(T("Обновить", "Refresh"), topBar);
    btnCreate->setIcon(QIcon(":/assets/icons/action_create.svg"));
    btnImport->setIcon(QIcon(":/assets/icons/action_import.svg"));
    btnRefresh->setIcon(QIcon(":/assets/icons/action_refresh.svg"));
    topBarLayout->addWidget(btnCreate);
    topBarLayout->addWidget(btnImport);
    topBarLayout->addWidget(btnRefresh);

    auto* list = new QTreeWidget(centerCol);
    list->setColumnCount(7);
    list->setHeaderLabels(QStringList() << T("Иконка", "Icon")
                                        << T("Название", "Name")
                                        << T("Версия", "Version")
                                        << T("Загрузчик", "Loader")
                                        << T("Последний запуск", "Last played")
                                        << T("Статус", "Status")
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
    list->setItemDelegateForColumn(5, new StatusDelegate(theme, list));
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

    auto* title = new QLabel(T("Выберите инстанс", "Select an instance"), heroText);
    QFont tf = title->font();
    tf.setPointSize(16);
    tf.setBold(true);
    title->setFont(tf);
    heroTextLayout->addWidget(title);

    auto* subtitle = new QLabel("", heroText);
    subtitle->setStyleSheet(QString("color:%1;").arg(theme.mutedText));
    heroTextLayout->addWidget(subtitle);
    heroTextLayout->addStretch(1);

    heroRowLayout->addWidget(heroIcon);
    heroRowLayout->addWidget(heroText, 1);
    detailLayout->addWidget(heroRow);

    auto* btnPlay = new QPushButton(T("Играть", "Play"), detail);
    btnPlay->setObjectName("primary");
    btnPlay->setIcon(QIcon(":/assets/icons/action_play.svg"));
    btnPlay->setMinimumHeight(44);
    detailLayout->addWidget(btnPlay);

    auto* actionRow = new QWidget(detail);
    auto* actionRowLayout = new QHBoxLayout(actionRow);
    actionRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* btnEdit = new QPushButton(T("Правка", "Edit"), actionRow);
    auto* btnFolder = new QPushButton(T("Папка", "Folder"), actionRow);
    auto* btnDelete = new QPushButton(T("Удалить", "Delete"), actionRow);
    btnEdit->setIcon(QIcon(":/assets/icons/action_edit.svg"));
    btnFolder->setIcon(QIcon(":/assets/icons/action_folder.svg"));
    btnDelete->setIcon(QIcon(":/assets/icons/action_delete.svg"));
    btnDelete->setObjectName("danger");
    actionRowLayout->addWidget(btnEdit);
    actionRowLayout->addWidget(btnFolder);
    actionRowLayout->addWidget(btnDelete);
    detailLayout->addWidget(actionRow);

    auto* profileTitle = new QLabel(T("Профиль", "Profile"), detail);
    QFont pft = profileTitle->font();
    pft.setPointSize(12);
    pft.setBold(true);
    profileTitle->setFont(pft);
    detailLayout->addWidget(profileTitle);

    auto* form = new QWidget(detail);
    auto* formLayout = new QFormLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignTop);

    auto* mcVersion = new QComboBox(form);
    mcVersion->setEditable(true);
    mcVersion->addItems(QStringList() << "1.20.1"
                                      << "1.19.2"
                                      << "1.18.2"
                                      << "1.16.5"
                                      << "1.12.2");

    auto* loaderType = new QComboBox(form);
    loaderType->addItem("Vanilla", "vanilla");
    loaderType->addItem("Fabric", "fabric");
    loaderType->addItem("Forge", "forge");

    auto* loaderVersion = new QComboBox(form);
    loaderVersion->setEditable(true);

    auto* loaderRow = new QWidget(form);
    auto* loaderRowLayout = new QHBoxLayout(loaderRow);
    loaderRowLayout->setContentsMargins(0, 0, 0, 0);
    loaderRowLayout->setSpacing(10);
    loaderRowLayout->addWidget(loaderType, 1);
    loaderRowLayout->addWidget(loaderVersion, 1);

    auto* javaVersion = new QComboBox(form);
    javaVersion->addItem(T("Авто", "Auto"), "auto");
    javaVersion->addItem("Java 8 (Temurin)", "temurin8");
    javaVersion->addItem("Java 17 (Temurin)", "temurin17");
    javaVersion->addItem("Java 21 (Temurin)", "temurin21");
    auto* javaPath = new QLineEdit(form);
    auto* javaBrowse = new QPushButton(form);
    javaBrowse->setIcon(QIcon(":/assets/icons/action_folder.svg"));
    javaBrowse->setFixedWidth(36);
    auto* javaRow = new QWidget(form);
    auto* javaRowLayout = new QHBoxLayout(javaRow);
    javaRowLayout->setContentsMargins(0, 0, 0, 0);
    javaRowLayout->addWidget(javaPath, 1);
    javaRowLayout->addWidget(javaBrowse);

    auto* ramBox = new QWidget(form);
    auto* ramBoxLayout = new QVBoxLayout(ramBox);
    ramBoxLayout->setContentsMargins(0, 0, 0, 0);
    ramBoxLayout->setSpacing(6);

    auto* ramRow = new QWidget(form);
    auto* ramLayout = new QHBoxLayout(ramRow);
    ramLayout->setContentsMargins(0, 0, 0, 0);
    auto* ramSlider = new QSlider(Qt::Horizontal, ramRow);
    ramSlider->setRange(2048, 8192);
    ramSlider->setSingleStep(256);
    ramSlider->setPageStep(512);
    auto* ramLabel = new QLabel("4.0 GB", ramRow);
    ramLabel->setMinimumWidth(90);
    ramLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ramLayout->addWidget(ramSlider, 1);
    ramLayout->addWidget(ramLabel);

    auto* ramTicks = new QWidget(form);
    auto* ramTicksLayout = new QHBoxLayout(ramTicks);
    ramTicksLayout->setContentsMargins(0, 0, 0, 0);
    auto* ram2 = new QLabel("2 GB", ramTicks);
    auto* ram4 = new QLabel("4 GB", ramTicks);
    auto* ram6 = new QLabel("6 GB", ramTicks);
    auto* ram8 = new QLabel("8 GB", ramTicks);
    for (auto* l : QList<QLabel*>{ram2, ram4, ram6, ram8})
    {
        l->setStyleSheet(QString("color:%1;").arg(theme.mutedText));
    }
    ram2->setAlignment(Qt::AlignLeft);
    ram4->setAlignment(Qt::AlignLeft);
    ram6->setAlignment(Qt::AlignLeft);
    ram8->setAlignment(Qt::AlignRight);
    ramTicksLayout->addWidget(ram2);
    ramTicksLayout->addStretch(1);
    ramTicksLayout->addWidget(ram4);
    ramTicksLayout->addStretch(1);
    ramTicksLayout->addWidget(ram6);
    ramTicksLayout->addStretch(1);
    ramTicksLayout->addWidget(ram8);

    ramBoxLayout->addWidget(ramRow);
    ramBoxLayout->addWidget(ramTicks);

    auto* resolutionW = new QSpinBox(form);
    resolutionW->setRange(0, 16384);
    resolutionW->setSpecialValueText(T("Авто", "Auto"));
    auto* resolutionH = new QSpinBox(form);
    resolutionH->setRange(0, 16384);
    resolutionH->setSpecialValueText(T("Авто", "Auto"));
    auto* resolutionRow = new QWidget(form);
    auto* resolutionRowLayout = new QHBoxLayout(resolutionRow);
    resolutionRowLayout->setContentsMargins(0, 0, 0, 0);
    resolutionRowLayout->setSpacing(10);
    resolutionRowLayout->addWidget(resolutionW, 1);
    resolutionRowLayout->addWidget(resolutionH, 1);

    auto* jvmArgs = new QPlainTextEdit(form);
    jvmArgs->setPlaceholderText(T("По одному аргументу на строку, например:\n-XX:+UseG1GC\n-Dfile.encoding=UTF-8",
                                  "One JVM arg per line, e.g.:\n-XX:+UseG1GC\n-Dfile.encoding=UTF-8"));
    jvmArgs->setFixedHeight(90);

    auto* notes = new QPlainTextEdit(form);
    notes->setMaximumBlockCount(1000);
    notes->setFixedHeight(120);
    auto* notesMeta = new QLabel("0 / 500", form);
    notesMeta->setStyleSheet(QString("color:%1;").arg(theme.mutedText));
    notesMeta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto makeLabel = [&](const QString& t) {
        auto* l = new QLabel(t, form);
        l->setStyleSheet(QString("color:%1;").arg(theme.mutedText));
        return l;
    };

    auto* notesBox = new QWidget(form);
    auto* notesBoxLayout = new QVBoxLayout(notesBox);
    notesBoxLayout->setContentsMargins(0, 0, 0, 0);
    notesBoxLayout->setSpacing(6);
    auto* notesFooter = new QWidget(form);
    auto* notesFooterLayout = new QHBoxLayout(notesFooter);
    notesFooterLayout->setContentsMargins(0, 0, 0, 0);
    notesFooterLayout->addStretch(1);
    notesFooterLayout->addWidget(notesMeta);
    notesBoxLayout->addWidget(notes);
    notesBoxLayout->addWidget(notesFooter);

    formLayout->addRow(makeLabel(T("Версия Minecraft", "Minecraft version")), mcVersion);
    formLayout->addRow(makeLabel(T("Загрузчик", "Loader")), loaderRow);
    formLayout->addRow(makeLabel(T("Версия Java", "Java version")), javaVersion);
    formLayout->addRow(makeLabel(T("Путь к Java", "Java path")), javaRow);
    formLayout->addRow(makeLabel(T("Память (RAM)", "RAM Allocation")), ramBox);
    formLayout->addRow(makeLabel(T("Разрешение (W/H)", "Resolution (W/H)")), resolutionRow);
    formLayout->addRow(makeLabel(T("JVM аргументы", "JVM args")), jvmArgs);
    formLayout->addRow(makeLabel(T("Заметки", "Notes")), notesBox);

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
    auto* modsRefresh = new QPushButton(T("Обновить", "Refresh"), modsTop);
    auto* modsOpen = new QPushButton(T("Открыть папку", "Open folder"), modsTop);
    auto* modsModrinth = new QPushButton("Modrinth", modsTop);
    modsTopLayout->addWidget(new QLabel(T("Моды", "Mods"), modsTop));
    modsTopLayout->addStretch(1);
    modsTopLayout->addWidget(modsRefresh);
    modsTopLayout->addWidget(modsModrinth);
    modsTopLayout->addWidget(modsOpen);
    auto* modsList = new QListWidget(pageMods);
    modsLayout->addWidget(modsTop);
    modsLayout->addWidget(modsList, 1);

    auto* accountsLayout = new QVBoxLayout(pageAccounts);
    accountsLayout->setContentsMargins(16, 16, 16, 16);
    accountsLayout->setSpacing(10);
    auto* msClientId = new QLineEdit(pageAccounts);
    msClientId->setPlaceholderText(T("Client ID Microsoft", "Microsoft client id"));
    auto* addAccount = new QPushButton(T("Добавить аккаунт", "Add account"), pageAccounts);
    auto* accountCombo = new QComboBox(pageAccounts);
    auto* logout = new QPushButton(T("Выйти", "Logout"), pageAccounts);
    accountsLayout->addWidget(new QLabel(T("Client ID Microsoft", "Microsoft client id"), pageAccounts));
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
    accountsLayout->addWidget(new QLabel(T("Аккаунт", "Account"), pageAccounts));
    accountsLayout->addWidget(accRow);
    accountsLayout->addStretch(1);

    auto* javaLayout = new QVBoxLayout(pageJava);
    javaLayout->setContentsMargins(16, 16, 16, 16);
    javaLayout->addWidget(new QLabel(T("Java выбирается автоматически (при необходимости можно переопределить путь к Java для инстанса).",
                                       "Java is selected automatically (bundled JRE 8/17/21). You can override Java path per instance."),
                                     pageJava));
    javaLayout->addStretch(1);

    auto* settingsLayout = new QVBoxLayout(pageSettings);
    settingsLayout->setContentsMargins(16, 16, 16, 16);
    settingsLayout->setSpacing(10);

    auto* uiTitle = new QLabel(T("Интерфейс", "Interface"), pageSettings);
    QFont uiTitleFont = uiTitle->font();
    uiTitleFont.setPointSize(12);
    uiTitleFont.setBold(true);
    uiTitle->setFont(uiTitleFont);

    auto* uiLang = new QComboBox(pageSettings);
    uiLang->addItem("Русский", "ru");
    uiLang->addItem("English", "en");

    auto* uiGradients = new QCheckBox(T("Градиенты фона", "Background gradients"), pageSettings);

    auto* uiAccent = new QLineEdit(pageSettings);
    uiAccent->setPlaceholderText("#ff7a18");
    auto* uiAccentPick = new QPushButton(T("Выбрать", "Pick"), pageSettings);
    uiAccentPick->setFixedWidth(110);
    auto* uiAccentRow = new QWidget(pageSettings);
    auto* uiAccentRowLayout = new QHBoxLayout(uiAccentRow);
    uiAccentRowLayout->setContentsMargins(0, 0, 0, 0);
    uiAccentRowLayout->addWidget(uiAccent, 1);
    uiAccentRowLayout->addWidget(uiAccentPick);

    settingsLayout->addWidget(uiTitle);
    settingsLayout->addWidget(new QLabel(T("Язык", "Language"), pageSettings));
    settingsLayout->addWidget(uiLang);
    settingsLayout->addWidget(uiGradients);
    settingsLayout->addWidget(new QLabel(T("Акцент (цвет)", "Accent color"), pageSettings));
    settingsLayout->addWidget(uiAccentRow);

    auto* launchTitle = new QLabel(T("Запуск", "Launch"), pageSettings);
    QFont launchTitleFont = launchTitle->font();
    launchTitleFont.setPointSize(12);
    launchTitleFont.setBold(true);
    launchTitle->setFont(launchTitleFont);
    settingsLayout->addWidget(launchTitle);

    auto* nick = new QLineEdit(pageSettings);
    auto* globalJavaPath = new QLineEdit(pageSettings);
    globalJavaPath->setPlaceholderText(T("Путь к javaw.exe или java.exe (необязательно)", "javaw.exe or java.exe path (optional)"));
    auto* globalRam = new QLineEdit(pageSettings);
    globalRam->setPlaceholderText("4096");
    settingsLayout->addWidget(new QLabel(T("Ник (оффлайн)", "Nickname (offline)"), pageSettings));
    settingsLayout->addWidget(nick);
    settingsLayout->addWidget(new QLabel(T("Путь к Java (глобально)", "Java path (global override)"), pageSettings));
    settingsLayout->addWidget(globalJavaPath);
    settingsLayout->addWidget(new QLabel(T("RAM по умолчанию (MB)", "Default RAM (MB)"), pageSettings));
    settingsLayout->addWidget(globalRam);
    settingsLayout->addWidget(
        new QLabel(T(QString("Папка данных: %1").arg(QString::fromStdString(AppPaths::dataDir().string())),
                     QString("Data dir: %1").arg(QString::fromStdString(AppPaths::dataDir().string()))),
                   pageSettings));
    settingsLayout->addStretch(1);

    auto settings = appSettings();
    const auto uiLangKey = QString::fromStdString(settings.get("ui.lang").value_or("ru"));
    const int uiLangIndex = uiLang->findData(uiLangKey);
    if (uiLangIndex >= 0) uiLang->setCurrentIndex(uiLangIndex);
    else uiLang->setCurrentIndex(0);

    uiGradients->setChecked(theme.enableGradients);
    uiAccent->setText(theme.accent);

    nick->setText(QString::fromStdString(settings.get("offline.name").value_or("BlockForgePlayer")));
    globalJavaPath->setText(QString::fromStdString(settings.get("java.path").value_or("")));
    globalRam->setText(QString::fromStdString(settings.get("java.maxRamMb").value_or("4096")));
    msClientId->setText(QString::fromStdString(settings.get("ms.clientId").value_or("")));

    auto applyThemeNow = [=, this]() {
        const auto s = appSettings();
        const auto t = Theme::fromSettings(s);
        setStyleSheet(t.toStyleSheet());
        list->setItemDelegateForColumn(5, new StatusDelegate(t, list));
        statusNetwork->setStyleSheet(QString("color:%1;").arg(t.mutedText));
        statusAuth->setStyleSheet(QString("color:%1;").arg(t.mutedText));
    };

    connect(uiGradients, &QCheckBox::toggled, this, [=, this](bool on) {
        auto s = appSettings();
        s.set("ui.gradients", on ? "1" : "0");
        applyThemeNow();
    });
    connect(uiAccentPick, &QPushButton::clicked, this, [=, this]() {
        const QColor cur(uiAccent->text().trimmed());
        const QColor initial = cur.isValid() ? cur : QColor(theme.accent);
        const QColor chosen = QColorDialog::getColor(initial, this, T("Выбор акцента", "Choose accent"));
        if (!chosen.isValid()) return;
        uiAccent->setText(chosen.name(QColor::HexRgb));
        auto s = appSettings();
        s.set("ui.accent", uiAccent->text().trimmed().toStdString());
        applyThemeNow();
    });
    connect(uiAccent, &QLineEdit::editingFinished, this, [=, this]() {
        const QColor c(uiAccent->text().trimmed());
        if (!c.isValid())
        {
            uiAccent->setText(theme.accent);
            return;
        }
        uiAccent->setText(c.name(QColor::HexRgb));
        auto s = appSettings();
        s.set("ui.accent", uiAccent->text().trimmed().toStdString());
        applyThemeNow();
    });
    connect(uiLang, &QComboBox::currentIndexChanged, this, [=, this](int) {
        auto s = appSettings();
        s.set("ui.lang", uiLang->currentData().toString().toStdString());
        QMessageBox::information(this,
                                 T("Язык", "Language"),
                                 T("Перезапустите BlockForge, чтобы применить язык интерфейса.",
                                   "Restart BlockForge to apply UI language."));
    });

    auto instancesById = std::make_shared<std::unordered_map<std::string, Instance>>();
    auto selectedInstanceId = std::make_shared<QString>();

    auto instanceKey = [](const QString& id, const QString& key) {
        return QString("instance.%1.%2").arg(id, key).toStdString();
    };

    auto writeInstanceMeta = [&](const QString& id, const QString& key, const QString& value, bool removeIfEmpty) {
        if (id.isEmpty()) return;
        const auto path = AppPaths::instancesDir() / id.toStdString() / "instance.bf";
        std::ifstream in(path, std::ios::binary);
        if (!in.good()) return;

        std::vector<std::string> lines;
        std::string line;
        bool found = false;
        const auto prefix = (key + "=").toStdString();
        while (std::getline(in, line))
        {
            if (line.rfind(prefix, 0) == 0)
            {
                found = true;
                if (removeIfEmpty && value.trimmed().isEmpty())
                {
                    continue;
                }
                lines.push_back(prefix + value.trimmed().toStdString());
            }
            else
            {
                lines.push_back(line);
            }
        }
        in.close();

        if (!found && !(removeIfEmpty && value.trimmed().isEmpty()))
        {
            lines.push_back(prefix + value.trimmed().toStdString());
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (const auto& l : lines) out << l << "\n";
        out.close();
    };

    auto loaderDisplay = [=](const Instance& inst) {
        QString lt = "Vanilla";
        if (inst.loaderType == LoaderType::Fabric) lt = "Fabric";
        else if (inst.loaderType == LoaderType::Forge) lt = "Forge";
        if (inst.loaderVersion.has_value() && !inst.loaderVersion->empty())
        {
            return QString("%1 %2").arg(lt, QString::fromStdString(*inst.loaderVersion));
        }
        return lt;
    };

    auto statusText = [=](UiInstanceStatus st, int pct) {
        if (st == UiInstanceStatus::Ready) return T("Готово", "Ready");
        if (st == UiInstanceStatus::NotInstalled) return T("Не установлено", "Not installed");
        if (st == UiInstanceStatus::Broken) return T("Сломано", "Broken");
        if (st == UiInstanceStatus::Downloading) return T(QString("Загрузка %1%").arg(pct), QString("Downloading %1%").arg(pct));
        return QString();
    };

    auto setRowStatus = [=](QTreeWidgetItem* row, UiInstanceStatus st, int pct) {
        if (!row) return;
        row->setData(5, Qt::UserRole + 1, static_cast<int>(st));
        row->setText(5, statusText(st, pct));
    };

    auto updateDetails = [=]() {
        if (selectedInstanceId->isEmpty())
        {
            title->setText(T("Выберите инстанс", "Select an instance"));
            subtitle->setText("");
            btnPlay->setEnabled(false);
            btnEdit->setEnabled(false);
            btnFolder->setEnabled(false);
            btnDelete->setEnabled(false);
            mcVersion->setEnabled(false);
            loaderType->setEnabled(false);
            loaderVersion->setEnabled(false);
            javaVersion->setEnabled(false);
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
            title->setText(T("Выберите инстанс", "Select an instance"));
            subtitle->setText("");
            btnPlay->setEnabled(false);
            btnEdit->setEnabled(false);
            btnFolder->setEnabled(false);
            btnDelete->setEnabled(false);
            mcVersion->setEnabled(false);
            loaderType->setEnabled(false);
            loaderVersion->setEnabled(false);
            javaVersion->setEnabled(false);
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
        subtitle->setText(QString("%1 / %2").arg(QString::fromStdString(inst.minecraftVersion), loaderDisplay(inst)));

        QIcon ico(":/assets/icons/inst_grass.svg");
        if (inst.loaderType == LoaderType::Forge) ico = QIcon(":/assets/icons/inst_obsidian.svg");
        else if (inst.loaderType == LoaderType::Fabric) ico = QIcon(":/assets/icons/inst_stone.svg");
        heroIcon->setPixmap(ico.pixmap(96, 96));

        const QSignalBlocker b0(mcVersion);
        const QSignalBlocker b00(loaderType);
        const QSignalBlocker b000(loaderVersion);
        const QSignalBlocker b0000(javaVersion);
        const QSignalBlocker b1(javaPath);
        const QSignalBlocker b2(ramSlider);
        const QSignalBlocker b3(notes);

        auto settings = appSettings();
        mcVersion->setCurrentText(QString::fromStdString(inst.minecraftVersion));
        if (inst.loaderType == LoaderType::Vanilla) loaderType->setCurrentIndex(loaderType->findData("vanilla"));
        else if (inst.loaderType == LoaderType::Fabric) loaderType->setCurrentIndex(loaderType->findData("fabric"));
        else loaderType->setCurrentIndex(loaderType->findData("forge"));
        loaderVersion->setEnabled(inst.loaderType != LoaderType::Vanilla);
        loaderVersion->setCurrentText(inst.loaderVersion.has_value() ? QString::fromStdString(*inst.loaderVersion) : "");
        javaVersion->setEnabled(true);
        const auto javaSel = QString::fromStdString(settings.get(instanceKey(*selectedInstanceId, "javaVersion")).value_or("auto"));
        const auto javaIdx = javaVersion->findData(javaSel);
        if (javaIdx >= 0) javaVersion->setCurrentIndex(javaIdx);
        javaPath->setText(QString::fromStdString(settings.get(instanceKey(*selectedInstanceId, "javaPath")).value_or("")));

        const auto rm = settings.get(instanceKey(*selectedInstanceId, "ramMb")).value_or(settings.get("java.maxRamMb").value_or("4096"));
        bool ok = false;
        const int rmi = QString::fromStdString(rm).toInt(&ok);
        const int clamped = ok ? std::max(2048, std::min(8192, rmi)) : 4096;
        ramSlider->setValue(clamped);
        ramLabel->setText(QString("%1 GB").arg(QString::number(clamped / 1024.0, 'f', 1)));

        const auto nt = QString::fromStdString(settings.get(instanceKey(*selectedInstanceId, "notes")).value_or(""));
        notes->setPlainText(nt.left(500));
        notesMeta->setText(QString("%1 / 500").arg(std::min(500, static_cast<int>(nt.size()))));

        btnPlay->setEnabled(true);
        btnEdit->setEnabled(true);
        btnFolder->setEnabled(true);
        btnDelete->setEnabled(true);
        mcVersion->setEnabled(true);
        loaderType->setEnabled(true);
        javaVersion->setEnabled(true);
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
            row->setText(3, loaderDisplay(inst));

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
            setRowStatus(row, QFileInfo::exists(jsonPath) ? UiInstanceStatus::Ready : UiInstanceStatus::NotInstalled, 0);
            row->setIcon(6, QIcon(":/assets/icons/action_more.svg"));
            row->setText(6, "");
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

        statusAuth->setText(activeIndex >= 0 ? T("Аккаунт: <span style=\"color:#44d07a;\">●</span> Вход выполнен",
                                                 "Auth: <span style=\"color:#44d07a;\">●</span> Signed in")
                                             : T("Аккаунт: <span style=\"color:#737b86;\">●</span> Оффлайн",
                                                 "Auth: <span style=\"color:#737b86;\">●</span> Offline"));
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

    auto* searchDebounce = new QTimer(this);
    searchDebounce->setSingleShot(true);
    QObject::connect(searchDebounce, &QTimer::timeout, this, [=]() {
        const auto t = search->text();
        for (int i = 0; i < list->topLevelItemCount(); ++i)
        {
            auto* it = list->topLevelItem(i);
            const auto name = it->text(1);
            it->setHidden(!t.trimmed().isEmpty() && !name.contains(t, Qt::CaseInsensitive));
        }
    });

    connect(search, &QLineEdit::textChanged, this, [=](const QString&) {
        searchDebounce->start(140);
    });

    connect(list, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur) return;
        *selectedInstanceId = cur->data(0, Qt::UserRole).toString();
        auto settings = appSettings();
        settings.set("ui.lastInstanceId", selectedInstanceId->toStdString());
        updateDetails();
        if (pages->currentIndex() == 3) refreshMods();
    });

    connect(list, &QTreeWidget::itemClicked, this, [=, this](QTreeWidgetItem* item, int column) {
        if (!item) return;
        if (column != 6) return;
        const auto id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty()) return;

        QMenu menu(this);
        auto* actPlay = menu.addAction(T("Играть", "Play"));
        auto* actEdit = menu.addAction(T("Правка", "Edit"));
        auto* actFolder = menu.addAction(T("Папка", "Folder"));
        auto* actExport = menu.addAction(T("Экспорт", "Export"));
        menu.addSeparator();
        auto* actDelete = menu.addAction(T("Удалить", "Delete"));

        const auto chosen = menu.exec(QCursor::pos());
        if (!chosen) return;

        *selectedInstanceId = id;
        updateDetails();
        if (pages->currentIndex() == 3) refreshMods();

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
            const auto outDir = QFileDialog::getExistingDirectory(nullptr, T("Экспорт в папку", "Export to folder"));
            if (outDir.isEmpty()) return;
            const auto src = AppPaths::instancesDir() / selectedInstanceId->toStdString();
            const auto dst = std::filesystem::path(outDir.toStdString()) / selectedInstanceId->toStdString();
            if (!copyTree(src, dst))
            {
                appendLog(T("Экспорт не удался.", "Export failed."));
                return;
            }
            appendLog(T(QString("Экспортировано в: %1").arg(QString::fromStdString(dst.string())),
                        QString("Exported to: %1").arg(QString::fromStdString(dst.string()))));
        }
    });

    connect(btnRefresh, &QPushButton::clicked, this, [=]() {
        refreshInstances();
        if (pages->currentIndex() == 3) refreshMods();
    });

    connect(pages, &QStackedWidget::currentChanged, this, [=](int index) {
        if (index == 3) refreshMods();
    });

    connect(btnImport, &QPushButton::clicked, this, [=]() {
        const auto dir = QFileDialog::getExistingDirectory(nullptr, T("Импорт папки инстанса", "Import instance folder"));
        if (dir.isEmpty()) return;

        const auto src = std::filesystem::path(dir.toStdString());
        if (!std::filesystem::exists(src))
        {
            appendLog(T("Источник не найден.", "Source not found."));
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
            appendLog(T("Импорт не удался.", "Import failed."));
            return;
        }
        *selectedInstanceId = QString::fromStdString(inst.id);
        appendLog(T(QString("Импортировано как инстанс: %1").arg(*selectedInstanceId),
                    QString("Imported as instance: %1").arg(*selectedInstanceId)));
        refreshInstances();
        if (pages->currentIndex() == 3) refreshMods();
    });

    connect(btnCreate, &QPushButton::clicked, this, [=, this]() {
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, T("Создать инстанс", "Create instance"), T("Название:", "Name:"), QLineEdit::Normal, T("Новый инстанс", "New Instance"), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        const auto mc = QInputDialog::getText(this,
                                              T("Создать инстанс", "Create instance"),
                                              T("Версия Minecraft:", "Minecraft version:"),
                                              QLineEdit::Normal,
                                              "1.20.1",
                                              &ok);
        if (!ok || mc.trimmed().isEmpty()) return;

        QStringList items;
        items << "vanilla"
              << "fabric"
              << "forge";
        const auto loader = QInputDialog::getItem(this, T("Создать инстанс", "Create instance"), T("Загрузчик:", "Loader:"), items, 0, false, &ok);
        if (!ok || loader.isEmpty()) return;

        InstanceStore store(AppPaths::instancesDir());
        const auto lt = loaderTypeFromString(loader.toStdString()).value_or(LoaderType::Vanilla);
        const auto inst = store.create(name.toStdString(), mc.toStdString(), lt);
        *selectedInstanceId = QString::fromStdString(inst.id);
        appendLog(T(QString("Создан инстанс: %1").arg(*selectedInstanceId),
                    QString("Created instance: %1").arg(*selectedInstanceId)));
        refreshInstances();
        if (pages->currentIndex() == 3) refreshMods();
    });

    connect(btnFolder, &QPushButton::clicked, this, [=]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto gameDir =
            QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(*selectedInstanceId));
        QDir().mkpath(gameDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(gameDir));
    });

    connect(btnDelete, &QPushButton::clicked, this, [=, this]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto r = QMessageBox::question(this,
                                             T("Удалить инстанс", "Delete instance"),
                                             T("Удалить выбранный инстанс?", "Delete selected instance?"));
        if (r != QMessageBox::Yes) return;
        std::error_code ec;
        std::filesystem::remove_all(AppPaths::instancesDir() / selectedInstanceId->toStdString(), ec);
        selectedInstanceId->clear();
        refreshInstances();
        if (pages->currentIndex() == 3) refreshMods();
    });

    connect(btnEdit, &QPushButton::clicked, this, [=, this]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;
        bool ok = false;
        const auto name = QInputDialog::getText(this, T("Переименовать инстанс", "Rename instance"), T("Название:", "Name:"), QLineEdit::Normal,
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

    connect(mcVersion, &QComboBox::currentTextChanged, this, [=](const QString& t) {
        if (selectedInstanceId->isEmpty()) return;
        auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;
        it->second.minecraftVersion = t.trimmed().toStdString();
        writeInstanceMeta(*selectedInstanceId, "minecraftVersion", t, false);
        updateDetails();
        auto* row = list->currentItem();
        if (row) row->setText(2, t.trimmed());
    });

    connect(loaderType, &QComboBox::currentIndexChanged, this, [=](int) {
        if (selectedInstanceId->isEmpty()) return;
        auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;

        const auto key = loaderType->currentData().toString();
        LoaderType lt = LoaderType::Vanilla;
        if (key == "fabric") lt = LoaderType::Fabric;
        else if (key == "forge") lt = LoaderType::Forge;
        it->second.loaderType = lt;

        writeInstanceMeta(*selectedInstanceId, "loaderType", key, false);
        if (lt == LoaderType::Vanilla)
        {
            it->second.loaderVersion.reset();
            writeInstanceMeta(*selectedInstanceId, "loaderVersion", "", true);
            loaderVersion->setCurrentText("");
        }
        loaderVersion->setEnabled(lt != LoaderType::Vanilla);

        updateDetails();
        auto* row = list->currentItem();
        if (row)
        {
            QIcon ico(":/assets/icons/inst_grass.svg");
            if (lt == LoaderType::Forge) ico = QIcon(":/assets/icons/inst_obsidian.svg");
            else if (lt == LoaderType::Fabric) ico = QIcon(":/assets/icons/inst_stone.svg");
            row->setIcon(0, ico);
            row->setText(3, loaderDisplay(it->second));
        }
    });

    connect(loaderVersion, &QComboBox::currentTextChanged, this, [=](const QString& t) {
        if (selectedInstanceId->isEmpty()) return;
        auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;
        if (it->second.loaderType == LoaderType::Vanilla) return;
        const auto v = t.trimmed();
        if (v.isEmpty()) it->second.loaderVersion.reset();
        else it->second.loaderVersion = v.toStdString();
        writeInstanceMeta(*selectedInstanceId, "loaderVersion", v, true);
        updateDetails();
        auto* row = list->currentItem();
        if (row) row->setText(3, loaderDisplay(it->second));
    });

    connect(javaVersion, &QComboBox::currentIndexChanged, this, [=](int) {
        if (selectedInstanceId->isEmpty()) return;
        auto settings = appSettings();
        settings.set(instanceKey(*selectedInstanceId, "javaVersion"), javaVersion->currentData().toString().toStdString());
    });

    connect(javaBrowse, &QPushButton::clicked, this, [=, this]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto file =
            QFileDialog::getOpenFileName(this, T("Выберите Java", "Select java"), QString(), T("Java (javaw.exe java.exe)", "Java (javaw.exe java.exe)"));
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

    connect(ramSlider, &QSlider::valueChanged, this, [=](int v) {
        ramLabel->setText(QString("%1 GB").arg(QString::number(v / 1024.0, 'f', 1)));
    });

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
    connect(modsModrinth, &QPushButton::clicked, this, [=, this]() {
        if (selectedInstanceId->isEmpty())
        {
            QMessageBox::information(this, "Modrinth", T("Сначала выберите инстанс.", "Select an instance first."));
            return;
        }
        const auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;

        const auto gameDir =
            QDir(QString::fromStdString(AppPaths::dataDir().string())).filePath(QString("instances/%1/game").arg(*selectedInstanceId));
        const auto modsDir = QDir(gameDir).filePath("mods");
        QDir().mkpath(modsDir);

        ModrinthDialog dlg(lang, this);
        dlg.setInstanceContext(*selectedInstanceId,
                               QString::fromStdString(it->second.minecraftVersion),
                               QString::fromStdString(toString(it->second.loaderType)),
                               modsDir);
        dlg.exec();
        refreshMods();
    });
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
            appendLog(T(QString("Выход не удался: %1").arg(error), QString("Logout failed: %1").arg(error)));
        }
        refreshAccounts();
    });

    connect(addAccount, &QPushButton::clicked, this, [=, this]() {
        const auto clientId = msClientId->text().trimmed();
        if (clientId.isEmpty())
        {
            appendLog(T("Client ID Microsoft пуст.", "Microsoft client id is empty."));
            return;
        }

        addAccount->setEnabled(false);
        appendLog(T("Запуск входа Microsoft (device code)...", "Starting Microsoft device code flow..."));

        auto* thread = new QThread(this);
        auto* worker = new AuthWorker();
        worker->configure(QString::fromStdString(AppPaths::dataDir().string()), clientId);
        worker->moveToThread(thread);

        QObject::connect(thread, &QThread::started, worker, &AuthWorker::run);
        QObject::connect(worker, &AuthWorker::logLine, logsView, [=](const QString& line) { appendLog(line); });
        QObject::connect(worker, &AuthWorker::deviceCodeReady, logsView, [=](const DeviceCode& code) {
            appendLog(T(QString("Откройте: %1").arg(code.verificationUri), QString("Open: %1").arg(code.verificationUri)));
            appendLog(T(QString("Код: %1").arg(code.userCode), QString("Code: %1").arg(code.userCode)));
        });
        QObject::connect(worker, &AuthWorker::accountReady, logsView, [=](const QString& name) {
            appendLog(T(QString("Аккаунт добавлен: %1").arg(name), QString("Account added: %1").arg(name)));
            addAccount->setEnabled(true);
            refreshAccounts();
            thread->quit();
        });
        QObject::connect(worker, &AuthWorker::failed, logsView, [=](const QString& e) {
            appendLog(T(QString("Ошибка входа: %1").arg(e), QString("Auth failed: %1").arg(e)));
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
    connect(btnPlay, &QPushButton::clicked, this, [=, this]() {
        if (selectedInstanceId->isEmpty()) return;
        const auto it = instancesById->find(selectedInstanceId->toStdString());
        if (it == instancesById->end()) return;

        progress->setVisible(true);
        progress->setValue(0);
        btnPlay->setEnabled(false);
        statusBar()->showMessage(T("Запуск...", "Launching..."));

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
        QObject::connect(worker, &LaunchWorker::progress, logsView, [=, this](const QString& phase, int cur, int total) {
            if (phase != "assets") return;
            if (total <= 0) return;
            const int pct = std::max(0, std::min(100, (cur * 100) / total));
            static int lastPct = -1;
            if (pct == lastPct) return;
            lastPct = pct;
            progress->setValue(pct);
            statusBar()->showMessage(T(QString("Загрузка ассетов: %1%").arg(pct), QString("Downloading assets: %1%").arg(pct)));

            auto* curItem = list->currentItem();
            if (curItem)
            {
                setRowStatus(curItem, UiInstanceStatus::Downloading, pct);
            }
        });
        QObject::connect(worker, &LaunchWorker::failed, logsView, [=, this](const QString& e) {
            appendLog(e);
            btnPlay->setEnabled(true);
            progress->setVisible(false);
            statusBar()->showMessage(T("Ошибка", "Failed"));
            thread->quit();
        });
        QObject::connect(worker, &LaunchWorker::readyToLaunch, logsView, [=, this](const LaunchCommand& cmd) {
            auto* proc = new QProcess(logsView);
            proc->setProgram(cmd.program);
            proc->setArguments(cmd.args);
            proc->setProcessChannelMode(QProcess::MergedChannels);

            QObject::connect(proc, &QProcess::readyRead, logsView, [=]() {
                const auto out = QString::fromUtf8(proc->readAll());
                if (!out.trimmed().isEmpty()) appendLog(out.trimmed());
            });
            QObject::connect(proc, &QProcess::finished, logsView, [=, this](int code) {
                appendLog(T(QString("Minecraft завершился: %1").arg(code), QString("Minecraft exited: %1").arg(code)));
                btnPlay->setEnabled(true);
                progress->setVisible(false);
                statusBar()->showMessage(T("Готово", "Ready"));
                proc->deleteLater();
            });

            auto settings = appSettings();
            settings.set(QString("instance.%1.lastPlayedMs").arg(*selectedInstanceId).toStdString(),
                         QString::number(QDateTime::currentMSecsSinceEpoch()).toStdString());
            refreshInstances();

            appendLog(T(QString("Запуск: %1").arg(cmd.program), QString("Launching: %1").arg(cmd.program)));
            statusBar()->showMessage(T("Работает", "Running"));
            auto* curItem = list->currentItem();
            if (curItem)
            {
                curItem->setData(5, Qt::UserRole + 1, static_cast<int>(UiInstanceStatus::Ready));
                curItem->setText(5, T("Готово", "Ready"));
            }
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
