#include "ui/apptheme.h"
#include "historytable.h"
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QLocale>
QVariant HistoryModel::data(const QModelIndex &index,int role)const{
    if(!index.isValid()||index.row()<0||index.row()>=rows_.size())return {};
    const auto &c=rows_[index.row()];const int col=index.column();
    if(role==Qt::ForegroundRole&&col==2)return AppTheme::color("#7fe5c4");
    if(role==Qt::FontRole&&col==6)return QFont("Consolas",10);
    if(role!=Qt::DisplayRole&&role!=Qt::ToolTipRole)return {};
    if(role==Qt::ToolTipRole&&col==1)return c.subject+"\n\n"+c.body;
    switch(col){case 0:return role==Qt::ToolTipRole?tr("원은 커밋, 선은 부모 커밋과의 연결입니다."):QString();case 1:return c.subject;case 2:return c.refs;case 3:return c.author;
    case 4:return QLocale().toString(QDateTime::fromString(c.date,Qt::ISODate).toLocalTime(),QLocale::ShortFormat);case 5:return c.body.trimmed();case 6:return c.hash;case 7:return c.email;case 8:return c.parents.join(", ");case 9:return QLocale().toString(QDateTime::fromString(c.committedDate,Qt::ISODate).toLocalTime(),QLocale::ShortFormat);default:return {};}
}
QVariant HistoryModel::headerData(int section,Qt::Orientation orientation,int role)const{return orientation==Qt::Horizontal&&role==Qt::DisplayRole?headers.value(section):QVariant();}
