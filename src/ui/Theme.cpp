#include "ui/Theme.h"
 
namespace blockforge
{
static bool parseBool(const std::optional<std::string>& v, bool def)
{
if (!v.has_value()) return def;
const auto s = QString::fromStdString(*v).trimmed().toLower();
if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
if (s == "0" || s == "false" || s == "no" || s == "off") return false;
return def;
}
 
UiLang uiLangFromSettings(const Settings& settings)
{
const auto v = settings.get("ui.lang").value_or("ru");
const auto s = QString::fromStdString(v).trimmed().toLower();
return s == "en" ? UiLang::En : UiLang::Ru;
}
 
Theme Theme::fromSettings(const Settings& settings)
{
Theme t;
t.enableGradients = parseBool(settings.get("ui.gradients"), true);
 
t.accent = QString::fromStdString(settings.get("ui.accent").value_or("#ff7a18"));
t.accentHover = QString::fromStdString(settings.get("ui.accentHover").value_or("#ff8a33"));
 
t.windowBgTop = QString::fromStdString(settings.get("ui.windowBgTop").value_or("#0f141b"));
t.windowBgBottom = QString::fromStdString(settings.get("ui.windowBgBottom").value_or("#070a0f"));
 
t.sidebarBgTop = QString::fromStdString(settings.get("ui.sidebarBgTop").value_or("#0d1219"));
t.sidebarBgBottom = QString::fromStdString(settings.get("ui.sidebarBgBottom").value_or("#070a0f"));
t.sidebarBorder = QString::fromStdString(settings.get("ui.sidebarBorder").value_or("#151c25"));
 
t.text = QString::fromStdString(settings.get("ui.text").value_or("#e7e7e7"));
t.mutedText = QString::fromStdString(settings.get("ui.mutedText").value_or("#aab2bd"));
 
t.controlBg = QString::fromStdString(settings.get("ui.controlBg").value_or("#121820"));
t.controlBorder = QString::fromStdString(settings.get("ui.controlBorder").value_or("#2a3440"));
t.controlHoverBg = QString::fromStdString(settings.get("ui.controlHoverBg").value_or("#222e3b"));
 
t.dangerBg = QString::fromStdString(settings.get("ui.dangerBg").value_or("#2a1616"));
t.dangerBorder = QString::fromStdString(settings.get("ui.dangerBorder").value_or("#4a2020"));
t.dangerHoverBg = QString::fromStdString(settings.get("ui.dangerHoverBg").value_or("#3a1a1a"));
 
t.splitterHandle = QString::fromStdString(settings.get("ui.splitterHandle").value_or("#0b0f14"));
 
t.listBg = QString::fromStdString(settings.get("ui.listBg").value_or("#0f151d"));
t.listBorder = QString::fromStdString(settings.get("ui.listBorder").value_or("#202833"));
t.listAltBg = QString::fromStdString(settings.get("ui.listAltBg").value_or("#0d1219"));
t.listHeaderText = QString::fromStdString(settings.get("ui.listHeaderText").value_or("#aab2bd"));
t.listSelectedBg = QString::fromStdString(settings.get("ui.listSelectedBg").value_or("#141a22"));
 
t.progressBg = QString::fromStdString(settings.get("ui.progressBg").value_or("#121820"));
t.progressBorder = QString::fromStdString(settings.get("ui.progressBorder").value_or("#2a3440"));
t.progressChunk = QString::fromStdString(settings.get("ui.progressChunk").value_or("#ff7a18"));
 
return t;
}
 
QString Theme::toStyleSheet() const
{
const auto winBg = enableGradients
? QString("qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,stop:1 %2)").arg(windowBgTop, windowBgBottom)
: windowBgTop;
const auto sideBg = enableGradients
? QString("qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,stop:1 %2)").arg(sidebarBgTop, sidebarBgBottom)
: sidebarBgTop;
 
return QString()
.append(QString("QMainWindow{background:%1;color:%2;}").arg(winBg, text))
.append(QString("QWidget{color:%1;font-family:Segoe UI;}").arg(text))
.append(QString("QLineEdit,QPlainTextEdit,QComboBox{background:%1;border:1px solid %2;border-radius:6px;padding:6px;min-height:34px;}")
.arg(controlBg, controlBorder))
.append("QComboBox::drop-down{border:0px;}")
.append(QString("QPushButton{background:%1;border:1px solid %2;border-radius:8px;padding:8px 12px;min-height:34px;}")
.arg(QString("#1a232e"), controlBorder))
.append(QString("QPushButton:hover{background:%1;}").arg(controlHoverBg))
.append(QString("QPushButton:disabled{color:#777;background:#141a22;border-color:#202833;}"))
.append(QString("QPushButton#primary{background:%1;border:1px solid %1;color:#111;font-weight:600;}").arg(accent))
.append(QString("QPushButton#primary:hover{background:%1;}").arg(accentHover))
.append(QString("QPushButton#danger{background:%1;border:1px solid %2;}").arg(dangerBg, dangerBorder))
.append(QString("QPushButton#danger:hover{background:%1;}").arg(dangerHoverBg))
.append(QString("QSlider::groove:horizontal{height:6px;background:%1;border:1px solid %2;border-radius:4px;}")
.arg(controlBg, controlBorder))
.append(QString("QSlider::sub-page:horizontal{background:%1;border-radius:4px;}").arg(accent))
.append(QString("QSlider::handle:horizontal{width:16px;margin:-6px 0px -6px 0px;background:%1;border:2px solid %2;border-radius:8px;}")
.arg(text, accent))
.append(QString("QToolButton{background:transparent;border:0px;padding:12px 10px;text-align:left;color:%1;}").arg(QString("#c9ced6")))
.append(QString("QToolButton:hover{background:%1;}").arg(QString("#0f151d")))
.append(QString("QToolButton:checked{background:%1;border-left:3px solid %2;color:%2;}").arg(listSelectedBg, accent))
.append(QString("QSplitter::handle{background:%1;}").arg(splitterHandle))
.append(QString("#sidebar{background:%1;border-right:1px solid %2;}").arg(sideBg, sidebarBorder))
.append("#topBar{background:transparent;}")
.append("#detailPanel{background:transparent;}")
.append(QString("QTreeWidget{background:%1;border:1px solid %2;border-radius:10px;alternate-background-color:%3;}")
.arg(listBg, listBorder, listAltBg))
.append(QString("QHeaderView::section{background:%1;border:0px;padding:10px 8px;color:%2;}").arg(listBg, listHeaderText))
.append("QTreeWidget::item{height:44px;}")
.append(QString("QTreeWidget::item:selected{background:%1;}").arg(listSelectedBg))
.append(QString("QProgressBar{background:%1;border:1px solid %2;border-radius:6px;text-align:center;}")
.arg(progressBg, progressBorder))
.append(QString("QProgressBar::chunk{background:%1;border-radius:6px;}").arg(progressChunk));
}
}
