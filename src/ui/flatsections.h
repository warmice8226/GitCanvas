#pragma once
#include <QScrollArea>
#include <QGroupBox>
#include <QVBoxLayout>

// Sections remain visible together; navigation only scrolls to a section.
class FlatSections final : public QScrollArea {
public:
    explicit FlatSections(QWidget *parent = nullptr) : QScrollArea(parent) {
        setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
        auto *content = new QWidget; layout_ = new QVBoxLayout(content);
        layout_->setContentsMargins(0, 0, 0, 0); layout_->setSpacing(16);
        setWidget(content);
    }
    void addSection(QWidget *content, const QString &title) {
        auto *group = new QGroupBox(title); auto *layout = new QVBoxLayout(group);
        layout->addWidget(content); layout_->addWidget(group); sections_.append(group);
    }
    void scrollToSection(int index) {
        if (index >= 0 && index < sections_.size()) ensureWidgetVisible(sections_[index], 0, 12);
    }
private:
    QVBoxLayout *layout_;
    QList<QWidget *> sections_;
};
