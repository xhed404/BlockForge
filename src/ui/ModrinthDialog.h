#pragma once
 
#include "ui/Theme.h"
 
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
 
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
 
namespace blockforge
{
class ModrinthDialog final : public QDialog
{
public:
explicit ModrinthDialog(UiLang lang, QWidget* parent = nullptr);
 
void setInstanceContext(QString instanceId, QString mcVersion, QString loaderKey, QString modsDir);
 
private:
void startSearch(const QString& query);
void startInstallSelected();
 
UiLang m_lang = UiLang::Ru;
QString m_instanceId;
QString m_mcVersion;
QString m_loaderKey;
QString m_modsDir;
 
QNetworkAccessManager m_net;
QPointer<QNetworkReply> m_activeReply;
 
QLineEdit* m_searchEdit = nullptr;
QPushButton* m_searchBtn = nullptr;
QListWidget* m_results = nullptr;
QPushButton* m_installBtn = nullptr;
QLabel* m_status = nullptr;
};
}
