#include "ui/ModrinthDialog.h"
 
#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
 
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QSaveFile>
#include <QVBoxLayout>
 
namespace blockforge
{
static QString l10n(const UiLang lang, const QString& ru, const QString& en)
{
return lang == UiLang::Ru ? ru : en;
}
 
static QByteArray userAgent()
{
return QByteArray("BlockForge/1 (Modrinth)");
}
 
ModrinthDialog::ModrinthDialog(UiLang lang, QWidget* parent)
: QDialog(parent),
m_lang(lang)
{
setWindowTitle(l10n(m_lang, "Modrinth", "Modrinth"));
setModal(true);
resize(820, 520);
 
auto* root = new QWidget(this);
auto* rootLayout = new QVBoxLayout(root);
rootLayout->setContentsMargins(16, 16, 16, 16);
rootLayout->setSpacing(10);
 
auto* searchRow = new QWidget(root);
auto* searchRowLayout = new QHBoxLayout(searchRow);
searchRowLayout->setContentsMargins(0, 0, 0, 0);
searchRowLayout->setSpacing(10);
 
m_searchEdit = new QLineEdit(searchRow);
m_searchEdit->setPlaceholderText(l10n(m_lang, "Поиск модов (Modrinth)...", "Search mods (Modrinth)..."));
m_searchBtn = new QPushButton(l10n(m_lang, "Поиск", "Search"), searchRow);
m_searchBtn->setFixedWidth(140);
 
searchRowLayout->addWidget(m_searchEdit, 1);
searchRowLayout->addWidget(m_searchBtn);
 
m_results = new QListWidget(root);
m_results->setSelectionMode(QAbstractItemView::SingleSelection);
 
auto* bottom = new QWidget(root);
auto* bottomLayout = new QHBoxLayout(bottom);
bottomLayout->setContentsMargins(0, 0, 0, 0);
bottomLayout->setSpacing(10);
 
m_status = new QLabel("", bottom);
m_status->setText(l10n(m_lang, "Введите запрос и нажмите «Поиск».", "Enter a query and press Search."));
m_installBtn = new QPushButton(l10n(m_lang, "Установить", "Install"), bottom);
m_installBtn->setObjectName("primary");
m_installBtn->setEnabled(false);
m_installBtn->setFixedWidth(160);
 
bottomLayout->addWidget(m_status, 1);
bottomLayout->addWidget(m_installBtn);
 
rootLayout->addWidget(searchRow);
rootLayout->addWidget(m_results, 1);
rootLayout->addWidget(bottom);
 
auto* dlgLayout = new QVBoxLayout(this);
dlgLayout->setContentsMargins(0, 0, 0, 0);
dlgLayout->addWidget(root);
 
connect(m_searchBtn, &QPushButton::clicked, this, [this]() { startSearch(m_searchEdit->text().trimmed()); });
connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() { startSearch(m_searchEdit->text().trimmed()); });
connect(m_results, &QListWidget::currentRowChanged, this, [this](int row) { m_installBtn->setEnabled(row >= 0); });
connect(m_installBtn, &QPushButton::clicked, this, [this]() { startInstallSelected(); });
}
 
void ModrinthDialog::setInstanceContext(QString instanceId, QString mcVersion, QString loaderKey, QString modsDir)
{
m_instanceId = std::move(instanceId);
m_mcVersion = std::move(mcVersion);
m_loaderKey = std::move(loaderKey);
m_modsDir = std::move(modsDir);
}
 
void ModrinthDialog::startSearch(const QString& query)
{
if (query.isEmpty())
{
m_status->setText(l10n(m_lang, "Введите запрос.", "Enter a query."));
return;
}
 
if (m_activeReply)
{
m_activeReply->abort();
m_activeReply->deleteLater();
m_activeReply.clear();
}
 
m_results->clear();
m_installBtn->setEnabled(false);
m_status->setText(l10n(m_lang, "Поиск...", "Searching..."));
 
QUrl url("https://api.modrinth.com/v2/search");
QUrlQuery q;
q.addQueryItem("query", query);
q.addQueryItem("limit", "25");
q.addQueryItem("facets", "[[\"project_type:mod\"]]");
url.setQuery(q);
 
QNetworkRequest req(url);
req.setRawHeader("User-Agent", userAgent());
req.setRawHeader("Accept", "application/json");
 
m_activeReply = m_net.get(req);
connect(m_activeReply, &QNetworkReply::finished, this, [this]() {
const auto reply = m_activeReply;
if (!reply) return;
 
const QByteArray body = reply->readAll();
const auto err = reply->error();
reply->deleteLater();
m_activeReply.clear();
 
if (err != QNetworkReply::NoError)
{
m_status->setText(l10n(m_lang, "Ошибка сети.", "Network error."));
return;
}
 
const auto doc = QJsonDocument::fromJson(body);
const auto obj = doc.object();
const auto hits = obj.value("hits").toArray();
if (hits.isEmpty())
{
m_status->setText(l10n(m_lang, "Ничего не найдено.", "No results."));
return;
}
 
for (const auto& v : hits)
{
const auto hit = v.toObject();
const auto pid = hit.value("project_id").toString();
const auto title = hit.value("title").toString();
const auto author = hit.value("author").toString();
if (pid.isEmpty() || title.isEmpty()) continue;
 
const auto label = author.isEmpty() ? title : QString("%1 — %2").arg(title, author);
auto* item = new QListWidgetItem(label, m_results);
item->setData(Qt::UserRole, pid);
}
 
m_status->setText(l10n(m_lang, "Выберите мод и нажмите «Установить».", "Select a mod and press Install."));
if (m_results->count() > 0) m_results->setCurrentRow(0);
});
}
 
static QString loaderForModrinth(const QString& loaderKey)
{
if (loaderKey == "fabric") return "fabric";
if (loaderKey == "forge") return "forge";
if (loaderKey == "quilt") return "quilt";
if (loaderKey == "neoforge") return "neoforge";
return {};
}
 
void ModrinthDialog::startInstallSelected()
{
const auto* cur = m_results->currentItem();
if (!cur) return;
 
const auto projectId = cur->data(Qt::UserRole).toString();
if (projectId.isEmpty()) return;
 
const auto loader = loaderForModrinth(m_loaderKey);
if (loader.isEmpty())
{
QMessageBox::warning(this,
l10n(m_lang, "Modrinth", "Modrinth"),
l10n(m_lang, "Для этого инстанса не определён загрузчик (Fabric/Forge).", "Loader is not set for this instance."));
return;
}
if (m_mcVersion.isEmpty())
{
QMessageBox::warning(this, l10n(m_lang, "Modrinth", "Modrinth"), l10n(m_lang, "Не задана версия Minecraft.", "Minecraft version is empty."));
return;
}
if (m_modsDir.isEmpty())
{
QMessageBox::warning(this, l10n(m_lang, "Modrinth", "Modrinth"), l10n(m_lang, "Папка mods не задана.", "Mods folder is empty."));
return;
}
 
if (m_activeReply)
{
m_activeReply->abort();
m_activeReply->deleteLater();
m_activeReply.clear();
}
 
m_installBtn->setEnabled(false);
m_searchBtn->setEnabled(false);
m_searchEdit->setEnabled(false);
m_status->setText(l10n(m_lang, "Подбор версии...", "Resolving version..."));
 
QUrl url(QString("https://api.modrinth.com/v2/project/%1/version").arg(projectId));
QUrlQuery q;
q.addQueryItem("loaders", QString("[\"%1\"]").arg(loader));
q.addQueryItem("game_versions", QString("[\"%1\"]").arg(m_mcVersion));
url.setQuery(q);
 
QNetworkRequest req(url);
req.setRawHeader("User-Agent", userAgent());
req.setRawHeader("Accept", "application/json");
 
m_activeReply = m_net.get(req);
connect(m_activeReply, &QNetworkReply::finished, this, [this]() {
const auto reply = m_activeReply;
if (!reply) return;
 
const QByteArray body = reply->readAll();
const auto err = reply->error();
reply->deleteLater();
m_activeReply.clear();
 
if (err != QNetworkReply::NoError)
{
m_status->setText(l10n(m_lang, "Ошибка сети.", "Network error."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
const auto doc = QJsonDocument::fromJson(body);
if (!doc.isArray())
{
m_status->setText(l10n(m_lang, "Некорректный ответ API.", "Invalid API response."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
const auto arr = doc.array();
if (arr.isEmpty())
{
m_status->setText(l10n(m_lang, "Нет подходящих версий под инстанс.", "No compatible versions found."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
const auto ver = arr.first().toObject();
const auto files = ver.value("files").toArray();
if (files.isEmpty())
{
m_status->setText(l10n(m_lang, "У версии нет файлов.", "No files for this version."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
const auto f0 = files.first().toObject();
const auto fileUrl = QUrl(f0.value("url").toString());
const auto fileName = f0.value("filename").toString();
if (!fileUrl.isValid() || fileName.isEmpty())
{
m_status->setText(l10n(m_lang, "У версии нет ссылки на файл.", "Missing file URL."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
QDir().mkpath(m_modsDir);
const auto outPath = QDir(m_modsDir).filePath(fileName);
m_status->setText(l10n(m_lang, "Скачивание...", "Downloading..."));
 
QNetworkRequest req(fileUrl);
req.setRawHeader("User-Agent", userAgent());
m_activeReply = m_net.get(req);
 
auto* out = new QSaveFile(outPath, this);
if (!out->open(QIODevice::WriteOnly))
{
out->deleteLater();
m_status->setText(l10n(m_lang, "Не удалось создать файл.", "Cannot create file."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
connect(m_activeReply, &QNetworkReply::readyRead, this, [this, out]() {
if (!m_activeReply) return;
out->write(m_activeReply->readAll());
});
connect(m_activeReply, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
if (total <= 0) return;
const int pct = static_cast<int>((rec * 100) / total);
m_status->setText(l10n(m_lang, QString("Скачивание... %1%").arg(pct), QString("Downloading... %1%").arg(pct)));
});
connect(m_activeReply, &QNetworkReply::finished, this, [this, out, fileName]() {
const auto reply = m_activeReply;
if (!reply) return;
 
out->write(reply->readAll());
const auto err = reply->error();
reply->deleteLater();
m_activeReply.clear();
 
if (err != QNetworkReply::NoError)
{
out->cancelWriting();
out->deleteLater();
m_status->setText(l10n(m_lang, "Ошибка скачивания.", "Download error."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
 
if (!out->commit())
{
out->deleteLater();
m_status->setText(l10n(m_lang, "Не удалось сохранить файл.", "Cannot save file."));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
return;
}
out->deleteLater();
 
m_status->setText(l10n(m_lang, QString("Установлено: %1").arg(fileName), QString("Installed: %1").arg(fileName)));
m_searchBtn->setEnabled(true);
m_searchEdit->setEnabled(true);
m_installBtn->setEnabled(true);
});
});
}
}
