#pragma once
#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <functional>

class ActionLogger final : public QObject {
public:
    ActionLogger(QWidget *root, std::function<void(const QString &,const QString &)> write)
        : QObject(root), root_(root), write_(std::move(write)) {
        watch(root); qApp->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show) {
            if (auto *widget = qobject_cast<QWidget *>(object); widget &&
                (widget == root_ || root_->isAncestorOf(widget))) watch(widget);
        }
        return false;
    }
private:
    void watch(QWidget *widget) {
        auto buttons = widget->findChildren<QAbstractButton *>();
        if (auto *button = qobject_cast<QAbstractButton *>(widget)) buttons.append(button);
        for (auto *button : buttons) {
            if (button->property("actionLogConnected").toBool()) continue;
            button->setProperty("actionLogConnected", true);
            // pressed precedes clicked, so the user's action precedes its Git commands.
            connect(button, &QAbstractButton::pressed, this, [this, button] {
                const auto text = button->text().remove('&').trimmed();
                if (!text.isEmpty()) write_(text + tr(" 클릭"),button->objectName());
            });
        }
        if (auto *menu = qobject_cast<QMenu *>(widget); menu && !menu->property("actionLogConnected").toBool()) {
            menu->setProperty("actionLogConnected", true);
            connect(menu, &QMenu::triggered, this, [this](QAction *action) { write_(action->text().remove('&') + tr(" 선택"),action->objectName()); });
        }
    }
    QWidget *root_;
    std::function<void(const QString &,const QString &)> write_;
};
