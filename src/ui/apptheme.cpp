#include "apptheme.h"
#include "windowchrome.h"
#include <QApplication>
#include <QEvent>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>
#include <QSyntaxHighlighter>
#include <QListWidget>
#include <QMap>

namespace {
bool lightMode = false;
bool applying = false;
QColor converted(QColor c) {
    if (!lightMode) return c;
    const QMap<QString, QString> fixed{{"#11151d", "#f5f7fa"}, {"#0c1017", "#eef1f5"},
        {"#151c27", "#ffffff"}, {"#dce3ef", "#202938"}, {"#202938", "#e4e9f0"},
        {"#5c687b", "#7b8796"}, {"#77869b", "#64748b"}, {"#7b968d", "#64748b"}};
    if (fixed.contains(c.name())) return QColor(fixed.value(c.name()));
    c.setHslF(c.hslHueF(), c.hslSaturationF(), c.lightnessF() < .45 ? .94 - c.lightnessF() * .25 : .28);
    return c;
}
void applyPalette() {
    QPalette palette;
    const auto set=[&palette](QPalette::ColorRole role,const char *value){palette.setColor(role,converted(QColor(value)));};
    set(QPalette::Window,"#11151d");set(QPalette::WindowText,"#dce3ef");set(QPalette::Base,"#151c27");
    set(QPalette::AlternateBase,"#18202c");set(QPalette::Text,"#dce3ef");set(QPalette::Button,"#202938");
    set(QPalette::ButtonText,"#dce3ef");set(QPalette::Highlight,"#27463f");set(QPalette::HighlightedText,"#a2f5db");
    set(QPalette::ToolTipBase,"#243044");set(QPalette::ToolTipText,"#dce3ef");
    palette.setColor(QPalette::Disabled,QPalette::Text,converted(QColor("#5c687b")));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,converted(QColor("#5c687b")));
    qApp->setPalette(palette);
}
QString themed(QString sheet) {
    if (!lightMode) return sheet;
    const auto matches = QRegularExpression("#[0-9a-fA-F]{6}").globalMatch(sheet);
    QList<QRegularExpressionMatch> found;
    auto scan = matches; while (scan.hasNext()) found.prepend(scan.next());
    for (const auto &m : found) sheet.replace(m.capturedStart(), m.capturedLength(), converted(QColor(m.captured())).name());
    return sheet;
}
void style(QWidget *widget) {
    if (widget->objectName() == "windowTitleBar") return;
    const auto current = widget->styleSheet();
    if (current != widget->property("themeAppliedSheet").toString()) widget->setProperty("themeDarkSheet", current);
    const auto result = themed(widget->property("themeDarkSheet").toString());
    widget->setProperty("themeAppliedSheet", result);
    if (current != result) widget->setStyleSheet(result);
}
class ThemeFilter final : public QObject {
public: using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (!applying && (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)) {
            if (auto *widget = qobject_cast<QWidget *>(object)) { applying = true; style(widget); applying = false; }
        }
        return false;
    }
};
}
bool AppTheme::isLight() { return lightMode; }
QColor AppTheme::color(const char *value) { return converted(QColor(QString::fromLatin1(value))); }
void AppTheme::initialize() {
    if (qApp->findChild<QObject *>("gitcanvasTheme")) return;
    lightMode = QSettings().value("ui/theme", "dark").toString() == "light";
    applyPalette();
    auto *filter = new ThemeFilter(qApp); filter->setObjectName("gitcanvasTheme"); qApp->installEventFilter(filter);
}
void AppTheme::setLight(bool light) {
    lightMode = light; QSettings().setValue("ui/theme", light ? "light" : "dark");
    applyPalette();
    applying = true;
    const auto widgets = QApplication::allWidgets();
    for (auto *widget : widgets) style(widget);
    applying = false;
    refreshWindowChrome();
    for (auto *widget : widgets) {
        for (auto *highlighter : widget->findChildren<QSyntaxHighlighter *>()) highlighter->rehighlight();
        if (auto *list = qobject_cast<QListWidget *>(widget)) {
            for (int i = 0; i < list->count(); ++i) {
                auto *item = list->item(i);
                if (item->data(Qt::UserRole + 100).isValid()) item->setForeground(color(item->data(Qt::UserRole + 100).toByteArray().constData()));
            }
        }
        widget->update();
    }
}
