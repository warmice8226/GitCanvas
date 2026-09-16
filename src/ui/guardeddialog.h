#pragma once
#include <QDialog>
#include <QCloseEvent>
#include <functional>

class GuardedDialog final : public QDialog {
public:
    GuardedDialog(QWidget *parent, std::function<bool()> canClose)
        : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint), canClose_(std::move(canClose)) {}
    void reject() override { if (canClose_()) QDialog::reject(); }
protected:
    void closeEvent(QCloseEvent *event) override {
        if (canClose_()) { event->accept(); QDialog::done(QDialog::Rejected); }
        else event->ignore();
    }
private:
    std::function<bool()> canClose_;
};
