#include "ui/apptheme.h"
#include "historywidget.h"
#include "historytable.h"
#include <QLocale>
#include <climits>
#include <QCryptographicHash>
#include "git/gitclient.h"
#include <QAbstractItemView>
#include "diff/diffwidget.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QUuid>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QCalendarWidget>
#include <QFormLayout>
#include <QTimer>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QColor laneColor(int index) {
    const QColor colors[] = {AppTheme::color("#6ae0bd"),AppTheme::color("#91b7ff"),AppTheme::color("#e9b76f"),AppTheme::color("#c5a0ef"),AppTheme::color("#ef8eaa"),AppTheme::color("#6bd3e3")};
    return colors[index % 6];
}
class GraphDelegate : public QStyledItemDelegate {
public:
    QVector<HistoryGraphRow> rows;
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter,option,index);
        if(index.row()>=rows.size()) return;
        const auto &row=rows[index.row()];
        painter->save(); painter->setClipRect(option.rect); painter->setRenderHint(QPainter::Antialiasing);
        const qreal top=option.rect.top(), bottom=option.rect.bottom()+1, middle=(top+bottom)/2;
        auto x=[&](int lane){ const qreal scale=option.rect.height()/36.0;return option.rect.left()+(18.0+lane*20.0)*scale; };
        auto line=[&](int from,int to,qreal y1,qreal y2,int color){
            painter->setPen(QPen(laneColor(color),2));
            QPainterPath path(QPointF(x(from),y1));
            path.cubicTo(x(from),(y1+y2)/2,x(to),(y1+y2)/2,x(to),y2); painter->drawPath(path);
        };
        for(const auto &edge:row.through) line(edge.from,edge.to,top,bottom,edge.color);
        if(row.incoming) line(row.lane,row.lane,top,middle,row.color);
        for(const auto &edge:row.parents) line(edge.from,edge.to,middle,bottom,edge.color);
        painter->setPen(QPen(laneColor(row.color),2)); painter->setBrush(AppTheme::color("#151c27"));
        painter->drawEllipse(QPointF(x(row.lane),middle),5,5);
        painter->setBrush(laneColor(row.color)); painter->setPen(Qt::NoPen); painter->drawEllipse(QPointF(x(row.lane),middle),2,2);
        painter->restore();
    }
};
}
QVector<HistoryCommit> HistoryWidget::parseLog(const QByteArray &output) {
    QVector<HistoryCommit> result;
    const auto fields=output.split('\0');
    for(qsizetype i=0;i+8<fields.size();i+=9) {
        HistoryCommit c;
        c.hash=QString::fromUtf8(fields[i]); c.parents=QString::fromUtf8(fields[i+1]).split(' ',Qt::SkipEmptyParts);
        c.author=QString::fromUtf8(fields[i+2]); c.email=QString::fromUtf8(fields[i+3]);
        c.date=QString::fromUtf8(fields[i+4]); c.refs=QString::fromUtf8(fields[i+5]);
        c.subject=QString::fromUtf8(fields[i+6]); c.body=QString::fromUtf8(fields[i+7]);c.committedDate=QString::fromUtf8(fields[i+8]);
        result.append(c);
    }
    return result;
}
QVector<HistoryGraphRow> HistoryWidget::buildGraph(const QVector<HistoryCommit> &commits) {
    QStringList lanes;
    QVector<int> colors;
    QVector<HistoryGraphRow> rows;
    int nextColor=0;
    for(const auto &commit:commits) {
        HistoryGraphRow row;
        row.lane=lanes.indexOf(commit.hash); row.incoming=row.lane>=0;
        if(row.lane<0) {row.lane=lanes.size();lanes.append(commit.hash);colors.append(nextColor++);}
        row.color=colors[row.lane];
        auto after=lanes; auto afterColors=colors;
        after.removeAt(row.lane); afterColors.removeAt(row.lane);
        for(qsizetype p=0;p<commit.parents.size();++p) {
            if(!after.contains(commit.parents[p])) {
                int at=p==0 ? std::min(row.lane,int(after.size())) : int(after.size());
                after.insert(at,commit.parents[p]); afterColors.insert(at,p==0 ? row.color : nextColor++);
            }
        }
        for(int lane=0;lane<lanes.size();++lane) {
            if(lane!=row.lane) row.through.append({lane,int(after.indexOf(lanes[lane])),colors[lane]});
        }
        for(const auto &parent:commit.parents) {
            int to=after.indexOf(parent); row.parents.append({row.lane,to,afterColors[to]});
        }
        rows.append(row); lanes=after; colors=afterColors;
    }
    return rows;
}
HistoryWidget::HistoryWidget(GitClient *git,QWidget *parent) : QWidget(parent),git_(git) {
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(10);
    summary_=new QLabel(tr("저장소를 열면 커밋 기록이 표시됩니다.")); summary_->setObjectName("historySummary"); summary_->setStyleSheet("color: #92a0b5;"); layout->addWidget(summary_);
    auto *filters=new QHBoxLayout;
    search_=new QLineEdit;search_->setObjectName("historySearch");search_->setPlaceholderText(tr("메시지 검색 (전체 이력)"));filters->addWidget(search_,2);
    author_=new QLineEdit;author_->setPlaceholderText(tr("작성자"));author_->setObjectName("historyAuthor");
    branchFilter_=new QLineEdit;branchFilter_->setObjectName("historyBranch");branchFilter_->setPlaceholderText(tr("브랜치 / 태그 / 커밋 ID (비우면 전체)"));
    searchButton_=new QPushButton(tr("검색 / 적용"));searchButton_->setObjectName("historySearchButton");filters->addWidget(searchButton_);layout->addLayout(filters);
    auto search=[this]{limit_=100;reload();};connect(searchButton_,&QPushButton::clicked,this,search);connect(search_,&QLineEdit::returnPressed,this,search);
    pathFilter_=new QLineEdit;pathFilter_->setObjectName("historyPath");pathFilter_->setPlaceholderText(tr("파일 경로 필터 (이름 변경 추적)"));
    since_=new QLineEdit;since_->setPlaceholderText(tr("시작 날짜 YYYY-MM-DD"));until_=new QLineEdit;until_->setPlaceholderText(tr("끝 날짜 YYYY-MM-DD"));
    auto *advanced=new QPushButton(tr("상세 검색"));advanced->setObjectName("historyAdvancedSearch");filters->addWidget(advanced);
    auto *filterDialog=new QDialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);filterDialog->setObjectName("historySearchDialog");filterDialog->resize(560,400);
    auto *filterLayout=new QVBoxLayout(filterDialog);filterLayout->addWidget(new QLabel(tr("상세 검색 · 비워 둔 조건은 제한하지 않습니다")));
    auto *form=new QFormLayout;filterLayout->addLayout(form);form->addRow(tr("작성자"),author_);form->addRow(tr("브랜치 / 태그 / 커밋"),branchFilter_);form->addRow(tr("파일 경로"),pathFilter_);
    since_->setObjectName("historySince");until_->setObjectName("historyUntil");
    for(auto *field:{since_,until_}){
        auto *dateRow=new QWidget;auto *dateLayout=new QHBoxLayout(dateRow);dateLayout->setContentsMargins(0,0,0,0);dateLayout->addWidget(field,1);
        auto *pick=new QPushButton(tr("달력"));pick->setObjectName(field==since_?"historySinceCalendar":"historyUntilCalendar");dateLayout->addWidget(pick);form->addRow(field==since_?tr("시작 날짜"):tr("끝 날짜"),dateRow);
        connect(pick,&QPushButton::clicked,filterDialog,[field,filterDialog]{
            QDialog popup(filterDialog,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);popup.setObjectName("historyCalendarDialog");auto *layout=new QVBoxLayout(&popup);auto *calendar=new QCalendarWidget;layout->addWidget(calendar);
            const auto date=QDate::fromString(field->text(),"yyyy-MM-dd");calendar->setSelectedDate(date.isValid()?date:QDate::currentDate());
            auto *buttons=new QHBoxLayout;auto *apply=new QPushButton(tr("날짜 선택"));auto *cancel=new QPushButton(tr("취소"));buttons->addWidget(apply);buttons->addWidget(cancel);layout->addLayout(buttons);
            connect(apply,&QPushButton::clicked,&popup,&QDialog::accept);connect(cancel,&QPushButton::clicked,&popup,&QDialog::reject);connect(calendar,&QCalendarWidget::activated,&popup,&QDialog::accept);
            if(popup.exec()==QDialog::Accepted)field->setText(calendar->selectedDate().toString("yyyy-MM-dd"));
        });
    }
    auto *filterError=new QLabel;filterError->setWordWrap(true);filterLayout->addWidget(filterError);
    auto *filterActions=new QHBoxLayout;auto *reset=new QPushButton(tr("초기화"));reset->setObjectName("historyResetFilters");auto *applyFilters=new QPushButton(tr("적용"));applyFilters->setObjectName("historyApplyFilters");auto *cancelFilters=new QPushButton(tr("취소"));filterActions->addWidget(reset);filterActions->addStretch();filterActions->addWidget(applyFilters);filterActions->addWidget(cancelFilters);filterLayout->addLayout(filterActions);
    const QList<QLineEdit*> fields{author_,branchFilter_,pathFilter_,since_,until_};
    connect(reset,&QPushButton::clicked,filterDialog,[fields,filterError]{for(auto *field:fields)field->clear();filterError->clear();});
    applyFilters->setDefault(true);
    connect(cancelFilters,&QPushButton::clicked,filterDialog,&QDialog::reject);
    connect(applyFilters,&QPushButton::clicked,filterDialog,[this,filterDialog,filterError]{
        for(auto *field:{since_,until_})if(!field->text().isEmpty()&&(!QDate::fromString(field->text(),"yyyy-MM-dd").isValid()||field->text().size()!=10)){filterError->setText(tr("날짜를 YYYY-MM-DD 형식으로 입력하세요."));return;}
        if(!since_->text().isEmpty()&&!until_->text().isEmpty()&&since_->text()>until_->text()){filterError->setText(tr("시작 날짜는 끝 날짜보다 늦을 수 없습니다."));return;}
        filterDialog->accept();
    });
    connect(advanced,&QPushButton::clicked,this,[this,fields,filterDialog,filterError,search]{
        if(git_->isBusy())return;QStringList previous;for(auto *field:fields)previous.append(field->text());filterError->clear();
        if(filterDialog->exec()==QDialog::Accepted)search();else for(int i=0;i<fields.size();++i)fields[i]->setText(previous[i]);
    });
    connect(git_,&GitClient::busyChanged,advanced,[advanced](bool busy){advanced->setEnabled(!busy);});
    table_=new HistoryTable;table_->setObjectName("historyTable");
    table_->setHorizontalHeaderLabels({tr("그래프"),tr("커밋 메시지"),tr("브랜치 / 태그"),tr("작성자"),tr("날짜"),tr("상세 설명"),tr("커밋 ID"),tr("이메일"),tr("부모 커밋"),tr("기록 시각")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setShowGrid(false);table_->setAlternatingRowColors(true);table_->setWordWrap(false);
    table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(36);
    table_->horizontalHeader()->setHighlightSections(false);table_->horizontalHeader()->setSectionsMovable(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);table_->setColumnWidth(1,330);
    table_->setColumnWidth(0,100);table_->setColumnWidth(2,180);table_->setColumnWidth(3,120);table_->setColumnWidth(4,145);
    for(int col=5;col<10;++col)table_->setColumnWidth(col,col==5?360:240);
    table_->setItemDelegateForColumn(0,new GraphDelegate(table_));
    table_->setStyleSheet("QTableView { background: #151c27; alternate-background-color: #18202c; border: 1px solid #2a3445; border-radius: 6px; } QTableView::item { padding: 0 8px; } QTableView::item:selected { background: #27463f; color: #e3fff4; } QHeaderView::section { background: #202938; color: #aab8cd; border: none; border-bottom: 1px solid #354257; padding: 10px 8px; }");
    layout->addWidget(table_,1);
    detailDialog_=new QDialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);detailDialog_->setObjectName("historyCommitDialog");detailDialog_->resize(1200,760);
    detailDialog_->setSizeGripEnabled(true);
    auto *dialogLayout=new QVBoxLayout(detailDialog_);
    auto *bottom=new QSplitter(Qt::Horizontal);dialogLayout->addWidget(bottom,1);
    details_=new QPlainTextEdit;details_->setObjectName("historyDetails");details_->setReadOnly(true);
    details_->setPlaceholderText(tr("위 목록에서 커밋을 선택하면 제목, 상세 설명과 작성자 정보를 확인할 수 있습니다."));
    details_->setMaximumHeight(170);
    auto *filePanel=new QWidget;auto *fileLayout=new QVBoxLayout(filePanel);fileLayout->setContentsMargins(8,0,0,0);
    filesLabel_=new QLabel(tr("변경 파일"));fileLayout->addWidget(details_);fileLayout->addWidget(filesLabel_);
    parent_=new QComboBox;parent_->setObjectName("historyParent");fileLayout->addWidget(parent_);
    connect(parent_,&QComboBox::activated,this,[this](int){compareBase_.clear();base_=parent_->currentData().toString();loadFiles();});
    files_=new QListWidget;files_->setObjectName("historyFiles");fileLayout->addWidget(files_);bottom->addWidget(filePanel);
    diff_=new DiffWidget(git_);diff_->setPatchActionsVisible(false);diff_->unified()->setObjectName("historyDiff");bottom->addWidget(diff_);bottom->setSizes({350,850});
    connect(files_,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){if(git_->isBusy())return;diff_->loadRevisions(base_,head_,item->text());});
    auto *closeDetails=new QPushButton(tr("닫기"));dialogLayout->addWidget(closeDetails);connect(closeDetails,&QPushButton::clicked,detailDialog_,&QDialog::accept);
    connect(table_,&QTableView::doubleClicked,this,[this](const QModelIndex &){detailDialog_->show();detailDialog_->raise();});
    auto *selectionTimer=new QTimer(this);selectionTimer->setInterval(30);
    connect(selectionTimer,&QTimer::timeout,this,[this]{if(pendingSelection_&&!git_->isBusy()){pendingSelection_=false;showCommit();}});selectionTimer->start();

    auto *actions=new QHBoxLayout;
    for(int direction:{-1,1}){auto *zoom=new QPushButton(direction<0?tr("축소 −"):tr("확대 +"));actions->addWidget(zoom);connect(zoom,&QPushButton::clicked,this,[this,direction]{int previous=table_->verticalHeader()->defaultSectionSize();int size=qBound(24,previous+direction*4,64);table_->verticalHeader()->setDefaultSectionSize(size);table_->setColumnWidth(0,qMax(85,table_->columnWidth(0)*size/previous));table_->viewport()->update();});}
    compare_=new QPushButton(tr("선택 커밋을 비교 기준으로"));compare_->setObjectName("historyCompareBase");actions->addWidget(compare_);
    clearCompare_=new QPushButton(tr("비교 해제"));actions->addWidget(clearCompare_);
    auto *openDetail = new QPushButton(tr("상세 보기")); openDetail->setObjectName("historyOpenDetail"); actions->addWidget(openDetail);
    detailButton_=openDetail;
    auto updateDetail = [this, openDetail] { openDetail->setEnabled(table_->currentRow() >= 0 && table_->currentRow() < commits_.size()); };
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, updateDetail); updateDetail();
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, updateDetail);
    connect(table_->model(), &QAbstractItemModel::modelReset, this, updateDetail);
    connect(openDetail, &QPushButton::clicked, this, [this] { if (table_->currentRow() < 0 || table_->currentRow() >= commits_.size()) return; detailDialog_->show(); detailDialog_->raise(); });
    more_=new QPushButton(tr("100개 더 보기"));more_->setObjectName("historyMore");actions->addStretch();actions->addWidget(more_);layout->addLayout(actions);
    connect(more_,&QPushButton::clicked,this,[this]{limit_=qMin(5000,limit_+100);reload();});
    connect(compare_,&QPushButton::clicked,this,[this]{if(table_->currentRow()<0)return;compareBase_=commits_[table_->currentRow()].hash;compare_->setText(tr("비교 기준 %1 → 다른 커밋 선택").arg(compareBase_.left(8)));});
    connect(clearCompare_,&QPushButton::clicked,this,[this]{compareBase_.clear();compare_->setText(tr("선택 커밋을 비교 기준으로"));showCommit();});
    table_->setContextMenuPolicy(Qt::CustomContextMenu);connect(table_,&QWidget::customContextMenuRequested,this,&HistoryWidget::contextMenu);
    files_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(files_,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){
        auto *item=files_->itemAt(point);if(!item||git_->isBusy())return;const auto path=item->text();
        QMenu menu(this);auto *history=menu.addAction(tr("이 파일의 변경 이력"));auto *blame=menu.addAction(tr("줄별 작성자 (Blame)"));auto *chosen=menu.exec(files_->viewport()->mapToGlobal(point));
        if(chosen==history){pathFilter_->setText(path);branchFilter_->setText(head_);limit_=100;reload();}
        else if(chosen==blame){git_->inspectLimited({"blame","--line-porcelain",head_,"--",path},[this,path](bool ok,const QByteArray &out,const QString &error){
            if(!ok){reportError(error);return;}
            QDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.resize(1000,650);auto *layout=new QVBoxLayout(&dialog);auto *heading=new QLabel(path);heading->setTextFormat(Qt::PlainText);layout->addWidget(heading);
            auto *table=new QTableWidget(0,4);table->setHorizontalHeaderLabels({tr("줄"),tr("작성자"),tr("커밋"),tr("내용")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);layout->addWidget(table);
            QString hash,author,lineNumber;for(const auto &line:out.split('\n')){if(line.startsWith("author "))author=QString::fromUtf8(line.mid(7));else if(line.startsWith('\t')){int row=table->rowCount();table->insertRow(row);QStringList values{lineNumber,author,hash.left(8),QString::fromUtf8(line.mid(1))};for(int col=0;col<4;++col)table->setItem(row,col,new QTableWidgetItem(values[col]));}else{auto parts=line.split(' ');if(parts.size()>=3&&parts[0].size()>=40){hash=QString::fromUtf8(parts[0]);lineNumber=QString::fromUtf8(parts[2]);}}}
            auto *close=new QPushButton(tr("닫기"));layout->addWidget(close);connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
        });}
    });    connect(table_->selectionModel(),&QItemSelectionModel::currentChanged,this,[this]{showCommit();});
    connect(git_,&GitClient::busyChanged,this,[this](bool busy){search_->setEnabled(!busy);author_->setEnabled(!busy);branchFilter_->setEnabled(!busy);pathFilter_->setEnabled(!busy);since_->setEnabled(!busy);until_->setEnabled(!busy);files_->setEnabled(!busy);parent_->setEnabled(!busy);searchButton_->setEnabled(!busy);more_->setEnabled(!busy&&hasMore_);compare_->setEnabled(!busy&&!commits_.isEmpty());clearCompare_->setEnabled(!busy);});
}
void HistoryWidget::reload() {
    if(git_->isBusy())return;
    if(repository_!=git_->repositoryPath()){detailDialog_->hide();pendingSelection_=false;repository_=git_->repositoryPath();limit_=100;search_->clear();author_->clear();branchFilter_->clear();pathFilter_->clear();since_->clear();until_->clear();compareBase_.clear();compare_->setText(tr("선택 커밋을 비교 기준으로"));}
    const auto oldHash=table_->currentRow()>=0 && table_->currentRow()<commits_.size()?commits_[table_->currentRow()].hash:QString();
    {const QSignalBlocker block(table_);table_->replace({});}
    pendingSelection_=false;head_.clear();commits_.clear();details_->clear();files_->clear();filesLabel_->setText(tr("변경 파일"));summary_->setText(tr("커밋 기록을 불러오는 중…"));
    diff_->clear();hasMore_=false;more_->setEnabled(false);
    QStringList args{"log","--fixed-strings","--regexp-ignore-case","--topo-order","--max-count="+QString::number(limit_+1),"-z","--format=%H%x00%P%x00%an%x00%ae%x00%aI%x00%D%x00%s%x00%b%x00%cI"};
    if(!search_->text().isEmpty())args.append({"--fixed-strings","--regexp-ignore-case","--grep="+search_->text()});
    if(!author_->text().isEmpty())args.append("--author="+author_->text());
    if(branchFilter_->text().trimmed().isEmpty())args.append("--all");else {auto ref=branchFilter_->text().trimmed();if(ref.startsWith('-')){summary_->setText(tr("올바른 브랜치/태그/커밋 ID를 입력하세요."));return;}args.append(ref);}
    for(auto *field:{since_,until_})if(!field->text().isEmpty()&&!QDate::fromString(field->text(),"yyyy-MM-dd").isValid()){summary_->setText(tr("날짜를 YYYY-MM-DD 형식으로 입력하세요."));return;}
    if(!since_->text().isEmpty()&&!until_->text().isEmpty()&&since_->text()>until_->text()){summary_->setText(tr("시작 날짜가 끝 날짜보다 늦습니다."));return;}
    if(!since_->text().isEmpty())args.append("--since="+since_->text()+" 00:00:00");
    if(!until_->text().isEmpty())args.append("--until="+until_->text()+" 23:59:59");
    if(!pathFilter_->text().isEmpty())args.append("--follow");
    args.append("--");if(!pathFilter_->text().isEmpty())args.append(pathFilter_->text());
    git_->inspectLimited(args,
        [this,oldHash](bool ok,const QByteArray &output,const QString &error){
            if(!ok){summary_->setText(tr("커밋 기록을 불러오지 못했습니다."));details_->setPlainText(error);return;}
            const auto key=QCryptographicHash::hash(output,QCryptographicHash::Sha256);auto *cached=cache_.object(key);
            const auto parsed=cached?cached->first:parseLog(output);
            commits_=parsed;hasMore_=commits_.size()>limit_;if(hasMore_)commits_.resize(limit_);more_->setEnabled(hasMore_&&limit_<5000);
            auto *delegate=static_cast<GraphDelegate *>(table_->itemDelegateForColumn(0));delegate->rows=cached?cached->second:buildGraph(commits_.mid(0,1000));
            if(!cached){int edges=0;for(const auto &row:delegate->rows)edges+=row.through.size()+row.parents.size();cache_.insert(key,new CachedHistory(parsed,delegate->rows),int(qMin<qint64>(INT_MAX,output.size()*3+edges*int(sizeof(HistoryEdge)))));}
            table_->setColumnHidden(0,!search_->text().isEmpty()||!author_->text().isEmpty()||!pathFilter_->text().isEmpty()||!since_->text().isEmpty()||!until_->text().isEmpty());
            int selected=0,maxLane=0;
            {
                const QSignalBlocker block(table_);const QSignalBlocker selectionBlock(table_->selectionModel());table_->replace(commits_);
                for(int row=0;row<commits_.size();++row) {
                    const auto &c=commits_[row];if(c.hash==oldHash)selected=row;
                    if(row>=delegate->rows.size())continue;
                    const auto &g=delegate->rows[row];maxLane=std::max(maxLane,g.lane);
                    for(const auto &e:g.through)maxLane=std::max({maxLane,e.from,e.to});
                    for(const auto &e:g.parents)maxLane=std::max({maxLane,e.from,e.to});
                }
                table_->setColumnWidth(0,std::max(85,(38+maxLane*20)*table_->verticalHeader()->defaultSectionSize()/36));
                if(!commits_.isEmpty())table_->setCurrentCell(selected,1);
            }
            const bool filtered=!search_->text().isEmpty()||!author_->text().isEmpty()||!branchFilter_->text().isEmpty()||!pathFilter_->text().isEmpty()||!since_->text().isEmpty()||!until_->text().isEmpty();
            summary_->setText(commits_.isEmpty()?(filtered?tr("조건에 맞는 커밋이 없습니다."):tr("아직 커밋이 없습니다. 변경 사항 탭에서 첫 커밋을 만들어보세요.")):tr("커밋 %1개 표시 · %2").arg(commits_.size()).arg(hasMore_?tr("100개 더 보기로 이전 기록 조회"):tr("조회 범위의 마지막 기록")));
            detailButton_->setEnabled(table_->currentRow()>=0&&table_->currentRow()<commits_.size());
            if(!commits_.isEmpty())showCommit();
            if(commits_.size()>1000)summary_->setText(summary_->text()+tr(" · 그래프는 처음 1,000개까지, 목록은 5,000개까지 표시합니다. 검색으로 범위를 좁힐 수 있습니다."));
        });
}
void HistoryWidget::showCommit() {
    int row=table_->currentRow();if(row<0 || row>=commits_.size())return;
    if(git_->isBusy()){pendingSelection_=true;return;}pendingSelection_=false;
    const auto c=commits_[row];
    details_->setPlainText(c.subject+"\n\n"+(c.body.trimmed().isEmpty()?QString():c.body.trimmed()+"\n\n")+
        tr("작성자: %1 <%2>\n날짜: %3\n커밋: %4\n부모: %5").arg(c.author,c.email,QLocale().toString(QDateTime::fromString(c.date,Qt::ISODate).toLocalTime(),QLocale::ShortFormat),c.hash,c.parents.isEmpty()?tr("없음 (첫 커밋)"):c.parents.join(", ")));
    head_=c.hash;
    detailDialog_->setWindowTitle(c.subject);
    {const QSignalBlocker block(parent_);parent_->clear();if(c.parents.isEmpty())parent_->addItem(tr("첫 커밋 · 빈 트리와 비교"),QString());for(int i=0;i<c.parents.size();++i)parent_->addItem(tr("부모 %1 · %2").arg(i+1).arg(c.parents[i].left(8)),c.parents[i]);}
    details_->appendPlainText(tr("기록 시각: %1").arg(QDateTime::fromString(c.committedDate,Qt::ISODate).toLocalTime().toString("yyyy-MM-dd HH:mm:ss")));
    base_=compareBase_.isEmpty()?c.parents.value(0):compareBase_;
    git_->inspectLimited({"rev-list","--left-right","--count","HEAD..."+c.hash},[this](bool ok,const QByteArray &out,const QString &){if(ok){const auto count=QString::fromUtf8(out).trimmed().split(QRegularExpression("\\s+"));if(count.size()==2)details_->appendPlainText(tr("HEAD에만 %1개 / 선택 커밋 이력에만 %2개").arg(count[0],count[1]));}loadFiles();});
}
void HistoryWidget::loadFiles(){
    if(git_->isBusy())return;files_->clear();diff_->clear();
    filesLabel_->setText(tr("%1 → %2 · 파일 클릭으로 Diff").arg(base_.isEmpty()?tr("빈 트리"):base_.left(8),head_.left(8)));
    QStringList args{"diff-tree","--root","--no-commit-id","--name-only","--no-renames","-r","-z"};if(!base_.isEmpty())args.append(base_);args.append({head_,"--"});
    git_->inspectLimited(args,[this](bool ok,const QByteArray &out,const QString &error){
        if(!ok){filesLabel_->setText(error);return;}
        for(const auto &path:out.split('\0'))if(!path.isEmpty()){auto *item=new QListWidgetItem(QString::fromUtf8(path),files_);item->setToolTip(QString::fromUtf8(path));}
        if(files_->count()==0)filesLabel_->setText(tr("변경 파일 없음"));
    });
}
void HistoryWidget::reportError(const QString &message){details_->setPlainText(message);summary_->setWordWrap(true);summary_->setText(message);}
void HistoryWidget::contextMenu(const QPoint &point){
    if(git_->isBusy())return;const auto index=table_->indexAt(point);if(!index.isValid())return;
    const auto c=commits_[index.row()];QMenu menu(this);
    auto *copy=menu.addAction(tr("커밋 ID 복사"));auto *base=menu.addAction(tr("이 커밋을 비교 기준으로"));
    auto *switchRef=menu.addAction(tr("이 커밋의 브랜치로 전환"));auto *tag=menu.addAction(tr("이 커밋에 태그 생성"));auto *checkout=menu.addAction(tr("이 커밋으로 이동 (detached HEAD)"));auto *branch=menu.addAction(tr("이 커밋에서 새 브랜치"));
    menu.addSeparator();auto *merge=menu.addAction(tr("현재 브랜치에 Merge"));auto *rebase=menu.addAction(tr("현재 브랜치를 이 커밋 위로 Rebase"));auto *reset=menu.addAction(tr("현재 브랜치를 이 커밋으로 Reset"));auto *revert=menu.addAction(tr("이 커밋 Revert"));auto *cherry=menu.addAction(tr("이 커밋 Cherry-pick"));
    auto *interactive=menu.addAction(tr("이 커밋 이후 Interactive rebase"));
    auto *chosen=menu.exec(table_->viewport()->mapToGlobal(point));
    if(chosen==interactive){emit rebaseRequested(c.hash);return;}
    if(chosen==switchRef){git_->inspect({"for-each-ref","--points-at="+c.hash,"--format=%(refname:short)","refs/heads"},[this](bool ok,const QByteArray &out,const QString &error){
        if(!ok){reportError(error);return;}const auto names=QString::fromUtf8(out).trimmed().split('\n',Qt::SkipEmptyParts);
        if(names.isEmpty()){reportError(tr("이 커밋을 가리키는 로컬 브랜치가 없습니다. 새 브랜치 생성 또는 detached HEAD 이동을 사용하세요."));return;}
        QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setLabelText(tr("전환할 로컬 브랜치"));dialog.setComboBoxItems(names);if(dialog.exec()!=QDialog::Accepted)return;
        git_->switchBranch(dialog.textValue(),false,[this](bool ok,const QByteArray &,const QString &error){if(!ok)reportError(error);emit repositoryChanged();});
    });return;}
    QStringList operation;
    if(chosen==merge)operation={"merge","--no-edit","--no-autostash",c.hash};
    else if(chosen==rebase)operation={"rebase","--no-autostash",c.hash};
    else if(chosen==reset){QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setLabelText(tr("Reset 방식\nsoft: 커밋만 이동 / mixed: Stage 해제 / hard: 파일도 해당 커밋으로 변경"));dialog.setComboBoxItems({"soft","mixed","hard"});if(dialog.exec()!=QDialog::Accepted)return;operation={"reset","--"+dialog.textValue(),c.hash};}
    else if(chosen==revert||chosen==cherry){operation={chosen==revert?"revert":"cherry-pick","--no-edit"};if(c.parents.size()>1){QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);QStringList choices;for(int i=0;i<c.parents.size();++i)choices.append(QString::number(i+1));dialog.setLabelText(tr("병합 커밋의 기준 부모 번호"));dialog.setComboBoxItems(choices);if(dialog.exec()!=QDialog::Accepted)return;operation.append({"-m",dialog.textValue()});}operation.append(c.hash);}
    if(!operation.isEmpty()){runHistoryAction(operation);return;}
    if(chosen==copy){QApplication::clipboard()->setText(c.hash);return;}
    if(chosen==base){compareBase_=c.hash;compare_->setText(tr("비교 기준 %1 → 다른 커밋 선택").arg(c.hash.left(8)));return;}
    QStringList args;
    if(chosen==tag){QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setLabelText(tr("선택 커밋에 만들 로컬 태그 이름"));if(dialog.exec()!=QDialog::Accepted||dialog.textValue().trimmed().isEmpty())return;args={"tag","--",dialog.textValue().trimmed(),c.hash};}
    else if(chosen==checkout){QMessageBox confirm(QMessageBox::Question,tr("커밋 이동"),tr("%1 커밋의 파일 상태로 이동합니다. 브랜치 없는 상태가 되며 작업 파일과 충돌하면 중단합니다.").arg(c.hash.left(8)),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);if(confirm.exec()!=QMessageBox::Yes)return;args={"switch","--detach",c.hash};}
    else if(chosen==branch){QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setLabelText(tr("선택 커밋에서 만들 브랜치 이름"));if(dialog.exec()!=QDialog::Accepted||dialog.textValue().trimmed().isEmpty())return;args={"switch","-c",dialog.textValue().trimmed(),c.hash};}
    else return;
    git_->inspect(args,[this](bool ok,const QByteArray &,const QString &error){if(!ok){reportError(error);return;}emit repositoryChanged();});
}

void HistoryWidget::runHistoryAction(QStringList arguments){
    if(git_->isBusy())return;
    const auto operation=arguments.first();
    QMessageBox confirm(QMessageBox::Warning,tr("이력 변경 확인"),tr("현재 브랜치에서 다음 작업을 실행합니다.\n%1\n\n변경 파일이 있으면 실행하지 않습니다. 현재 HEAD를 복구용 참조에 저장한 뒤 실행합니다. Reset/Rebase는 기존 이력을 바꿀 수 있습니다. 충돌 시에는 작업을 취소하거나 외부에서 해결할 수 있습니다.").arg(arguments.join(' ')),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);
    confirm.setDefaultButton(QMessageBox::No);if(confirm.exec()!=QMessageBox::Yes)return;
    git_->inspect({"status","--porcelain"},[this,arguments,operation](bool ok,const QByteArray &out,const QString &error){
        if(!ok||!out.trimmed().isEmpty()){reportError(ok?tr("수정 중인 파일이 있습니다. 먼저 커밋하거나 임시 보관하세요."):error);return;}
        git_->inspect({"symbolic-ref","--quiet","HEAD"},[this,arguments,operation](bool ok,const QByteArray &,const QString &){
            if(!ok){reportError(tr("먼저 작업할 브랜치로 전환하세요. detached HEAD에서는 이 작업을 실행하지 않습니다."));return;}
            // Existing sequencer/rebase/merge operations must not be overwritten by another operation.
            git_->inspect({"rev-parse","--absolute-git-dir"},[this,arguments,operation](bool ok,const QByteArray &out,const QString &error){
                if(!ok){reportError(error);return;}
                const QDir gitDirectory(QString::fromUtf8(out).trimmed());
                for(const auto *marker:{"MERGE_HEAD","CHERRY_PICK_HEAD","REVERT_HEAD","rebase-merge","rebase-apply","sequencer","BISECT_LOG"})if(QFileInfo::exists(gitDirectory.filePath(marker))){reportError(tr("이미 진행 중인 Git 작업이 있습니다. 완료하거나 취소한 뒤 다시 실행하세요."));return;}
                const auto backup="refs/gitcanvas/backups/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                git_->inspect({"update-ref",backup,"HEAD"},[this,arguments,operation,backup](bool ok,const QByteArray &,const QString &error){
                    if(!ok){reportError(error);return;}
                    git_->inspect(arguments,[this,operation,backup](bool ok,const QByteArray &,const QString &error){
                        const auto info=tr("\n복구 지점: %1\nHistory의 브랜치/태그/커밋 입력칸에 이 참조를 넣어 이전 HEAD를 확인할 수 있습니다.").arg(backup);
                        if(ok){QMessageBox done(QMessageBox::Information,tr("작업 완료"),tr("이력 작업을 완료했습니다.")+info,QMessageBox::Ok,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);done.exec();emit repositoryChanged();return;}
                        QMessageBox failure(QMessageBox::Warning,tr("이력 작업 확인 필요"),error+info,QMessageBox::Close,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);failure.setTextFormat(Qt::PlainText);
                        QPushButton *abort=nullptr;if(operation!="reset")abort=failure.addButton(tr("진행 중인 작업 취소"),QMessageBox::DestructiveRole);
                        failure.exec();if(abort&&failure.clickedButton()==abort){git_->inspect({operation,"--abort"},[this](bool ok,const QByteArray &,const QString &error){if(!ok){QMessageBox box(QMessageBox::Warning,tr("취소 실패"),error,QMessageBox::Ok,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.exec();}emit repositoryChanged();});}else emit repositoryChanged();
                    });
                });
            });
        });
    });
}
