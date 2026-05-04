#pragma once
 
#include "core/Settings.h"
 
#include <QtCore/QString>
 
namespace blockforge
{
enum class UiLang
{
Ru,
En,
};
 
UiLang uiLangFromSettings(const Settings& settings);
 
struct Theme final
{
QString windowBgTop;
QString windowBgBottom;
QString sidebarBgTop;
QString sidebarBgBottom;
QString sidebarBorder;
 
QString text;
QString mutedText;
 
QString controlBg;
QString controlBorder;
QString controlHoverBg;
 
QString accent;
QString accentHover;
 
QString dangerBg;
QString dangerBorder;
QString dangerHoverBg;
 
QString splitterHandle;
 
QString listBg;
QString listBorder;
QString listAltBg;
QString listHeaderText;
QString listSelectedBg;
 
QString progressBg;
QString progressBorder;
QString progressChunk;
 
bool enableGradients = true;
 
static Theme fromSettings(const Settings& settings);
QString toStyleSheet() const;
};
}
