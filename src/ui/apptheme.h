#pragma once
#include <QColor>
namespace AppTheme {
void initialize();
void setLight(bool light);
bool isLight();
QColor color(const char *darkColor);
}
