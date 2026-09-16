#include "windowchrome.h"
#include <QApplication>
#include <QEvent>
#include <QWidget>
#include <QPalette>
#include <QMainWindow>
#include <QLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMouseEvent>
#include <QWindow>
#include <QSysInfo>
#include <QVersionNumber>
#include <QSizeGrip>
#include <QTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace {
class TitleBar final : public QWidget {
public:
    explicit TitleBar(QWidget *window) : QWidget(window), owner_(window) {
        setObjectName("windowTitleBar");
        setFixedHeight(34);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 0, 0, 0);
        auto *title = new QLabel(window->windowTitle().isEmpty() ? qApp->applicationName() : window->windowTitle(), this);
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(title, 1);
        connect(window, &QWidget::windowTitleChanged, title, &QLabel::setText);
        auto button = [this, row](const QString &text, auto action) {
            auto *control = new QToolButton(this);
            control->setText(text);
            control->setFixedSize(40, 32);
            row->addWidget(control);
            connect(control, &QToolButton::clicked, this, action);
        };
        if (qobject_cast<QMainWindow *>(window)) {
            button(QStringLiteral("−"), [window] { window->showMinimized(); });
            button(QStringLiteral("□"), [window] { window->isMaximized() ? window->showNormal() : window->showMaximized(); });
        }
        button(QStringLiteral("×"), [window] { window->close(); });
    }
    void syncColors() {
        const auto bg = owner_->palette().color(QPalette::Window).name();
        const auto fg = owner_->palette().color(QPalette::WindowText).name();
        setStyleSheet(QStringLiteral("QWidget#windowTitleBar, QWidget#windowTitleBar QLabel { background: %1; color: %2; } QToolButton { background: %1; color: %2; border: none; } QToolButton:hover { background: #354257; }").arg(bg, fg));
    }
protected:
    void showEvent(QShowEvent *event) override { QWidget::showEvent(event); syncColors(); }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && owner_->windowHandle()) {
            owner_->windowHandle()->startSystemMove();
            event->accept();
        } else QWidget::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && qobject_cast<QMainWindow *>(owner_))
            owner_->isMaximized() ? owner_->showNormal() : owner_->showMaximized();
    }
private:
    QWidget *owner_;
};

class WindowChrome final : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
#ifdef Q_OS_WIN
        auto *target = qobject_cast<QWidget *>(object);
        if (event->type() == QEvent::Polish && target && target->isWindow() &&
            (target->windowType() == Qt::Window || target->windowType() == Qt::Dialog) &&
            qApp->platformName() == QStringLiteral("windows") &&
            QVersionNumber::fromString(QSysInfo::kernelVersion()) < QVersionNumber(10, 0, 22000) &&
            !target->property("customWindowTitleBar").toBool()) {
            target->setProperty("customWindowTitleBar", true);
            target->setWindowFlag(Qt::FramelessWindowHint);
            auto *bar = new TitleBar(target);
            if (auto *main = qobject_cast<QMainWindow *>(target)) main->setMenuWidget(bar);
            else if (target->layout()) target->layout()->setMenuBar(bar);
            target->setContentsMargins(5, 5, 5, 5);
            target->setMouseTracking(true);
            if (qobject_cast<QMainWindow *>(target)) {
                auto *grip = new QSizeGrip(target); grip->setObjectName("mainWindowSizeGrip");
                grip->setFixedSize(18, 18); grip->show();
            }
        }
        if (target && target->isWindow() && event->type() == QEvent::Resize) {
            if (auto *grip = target->findChild<QSizeGrip *>("mainWindowSizeGrip")) {
                grip->move(target->width() - grip->width(), target->height() - grip->height()); grip->raise();
            }
        }
        if (target && target->isWindow() && event->type() == QEvent::PaletteChange) {
            if (auto *bar = dynamic_cast<TitleBar *>(target->findChild<QWidget *>("windowTitleBar", Qt::FindDirectChildrenOnly)))
                QTimer::singleShot(0, bar, [bar] { bar->syncColors(); });
        }
        if (target && event->type() == QEvent::MouseButtonPress) {
            auto *window = target->window();
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (window->property("customWindowTitleBar").toBool() && !window->isMaximized() &&
                mouse->button() == Qt::LeftButton && window->windowHandle()) {
                const auto point = window->mapFromGlobal(mouse->globalPosition().toPoint());
                Qt::Edges edges;
                if (window->minimumWidth() != window->maximumWidth()) {
                    if (point.x() < 5) edges |= Qt::LeftEdge;
                    if (point.x() >= window->width() - 5) edges |= Qt::RightEdge;
                }
                if (window->minimumHeight() != window->maximumHeight()) {
                    if (point.y() < 5) edges |= Qt::TopEdge;
                    if (point.y() >= window->height() - 5) edges |= Qt::BottomEdge;
                }
                if (edges && window->windowHandle()->startSystemResize(edges)) return true;
            }
        }
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange ||
            event->type() == QEvent::PaletteChange || event->type() == QEvent::WindowActivate) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (widget && widget->isWindow() && widget->internalWinId() &&
                (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog)) {
                const auto background = widget->palette().color(QPalette::Window);
                const auto foreground = widget->palette().color(QPalette::WindowText);
                const HWND handle = reinterpret_cast<HWND>(widget->internalWinId());
                const BOOL dark = background.lightness() < 128;
                const COLORREF caption = RGB(background.red(), background.green(), background.blue());
                const COLORREF text = RGB(foreground.red(), foreground.green(), foreground.blue());
                // Numeric attributes also compile against older Windows SDKs.
                // Unsupported attributes leave the OS's native title bar intact.
                DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
                DwmSetWindowAttribute(handle, 35, &caption, sizeof(caption));
                DwmSetWindowAttribute(handle, 36, &text, sizeof(text));
            }
        }
#endif
        return QObject::eventFilter(object, event);
    }
};
}

void installWindowChrome() {
    if (qApp->findChild<QObject *>(QStringLiteral("gitcanvasWindowChrome"))) return;
    auto *chrome = new WindowChrome(qApp);
    chrome->setObjectName(QStringLiteral("gitcanvasWindowChrome"));
    qApp->installEventFilter(chrome);
}
void refreshWindowChrome() {
    for(auto *window:QApplication::topLevelWidgets()){
        if(auto *bar=dynamic_cast<TitleBar*>(window->findChild<QWidget*>("windowTitleBar",Qt::FindDirectChildrenOnly)))bar->syncColors();
#ifdef Q_OS_WIN
        if(!window->internalWinId()||(window->windowType()!=Qt::Window&&window->windowType()!=Qt::Dialog))continue;
        const auto bg=window->palette().color(QPalette::Window),fg=window->palette().color(QPalette::WindowText);
        const auto handle=reinterpret_cast<HWND>(window->internalWinId());
        const BOOL dark=bg.lightness()<128;const COLORREF background=RGB(bg.red(),bg.green(),bg.blue()),text=RGB(fg.red(),fg.green(),fg.blue());
        DwmSetWindowAttribute(handle,20,&dark,sizeof(dark));DwmSetWindowAttribute(handle,35,&background,sizeof(background));DwmSetWindowAttribute(handle,36,&text,sizeof(text));
#endif
    }
}
