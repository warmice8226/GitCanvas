#pragma once
#include "historywidget.h"
#include <QAbstractTableModel>
#include <QTableView>
class HistoryModel final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent={}) const override { return parent.isValid()?0:rows_.size(); }
    int columnCount(const QModelIndex &parent={}) const override { return parent.isValid()?0:10; }
    QVariant data(const QModelIndex &index,int role=Qt::DisplayRole) const override;
    QVariant headerData(int section,Qt::Orientation orientation,int role) const override;
    void replace(const QVector<HistoryCommit> &rows){beginResetModel();rows_=rows;endResetModel();}
    QStringList headers;
private: QVector<HistoryCommit> rows_;
};
class HistoryTable final : public QTableView {
    Q_OBJECT
public:
    explicit HistoryTable(QWidget *parent=nullptr):QTableView(parent){setModel(new HistoryModel(this));}
    void replace(const QVector<HistoryCommit> &rows){static_cast<HistoryModel*>(model())->replace(rows);}
    void setHorizontalHeaderLabels(const QStringList &labels){static_cast<HistoryModel*>(model())->headers=labels;}
    int rowCount() const { return model()->rowCount(); }
    int columnCount() const { return model()->columnCount(); }
    int currentRow() const { return currentIndex().row(); }
    void setCurrentCell(int row,int col){setCurrentIndex(model()->index(row,col));}
    QString cellText(int row,int col) const { return model()->index(row,col).data().toString(); }
};
