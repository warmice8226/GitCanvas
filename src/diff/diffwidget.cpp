#include "ui/apptheme.h"
#include "diffwidget.h"
#include "git/gitclient.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QStringDecoder>
#include <QSyntaxHighlighter>
#include <QStackedWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QListWidget>
namespace {
class Highlight:public QSyntaxHighlighter {
public:using QSyntaxHighlighter::QSyntaxHighlighter;
void highlightBlock(const QString &s)override {QTextCharFormat f;if(s.startsWith('+'))f.setForeground(AppTheme::color("#8ce7bb"));else if(s.startsWith('-'))f.setForeground(AppTheme::color("#ff9cad"));else if(s.startsWith("@@"))f.setForeground(AppTheme::color("#8cbcff"));else return;setFormat(0,s.size(),f);}
};
QPlainTextEdit *view(){auto *w=new QPlainTextEdit;w->setReadOnly(true);w->setLineWrapMode(QPlainTextEdit::NoWrap);w->setFont(QFont("Consolas",10));new Highlight(w->document());return w;}
}
DiffWidget::DiffWidget(GitClient *git,QWidget *parent):QWidget(parent),git_(git){
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto *toolbar=new QHBoxLayout;info_=new QLabel(tr("파일을 선택하세요"));info_->setTextFormat(Qt::PlainText);info_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);toolbar->addWidget(info_,1);
    encoding_=new QComboBox;encoding_->setObjectName("diffEncoding");encoding_->addItems({"UTF-8","UTF-16LE","UTF-16BE",tr("시스템 인코딩"),"Latin-1"});encoding_->setToolTip(tr("화면에 해석할 인코딩입니다. 원본 파일이나 부분 Stage의 바이트는 변환하지 않습니다."));toolbar->addWidget(encoding_);layout->addLayout(toolbar);
    auto *mode=new QComboBox;mode->setObjectName("diffViewMode");
    mode->addItems({tr("통합 Diff"),tr("나란히 확인"),tr("쌓기형 확인"),tr("줄 선택")});
    mode->setToolTip(tr("동일한 변경을 통합 패치, 이전/이후 비교 또는 줄 선택으로 표시합니다."));toolbar->insertWidget(1,mode);
    auto *views=new QStackedWidget;unified_=view();views->addWidget(unified_);
    auto *split=new QSplitter;left_=view();right_=view();
    for(int i=0;i<2;++i){auto *column=new QWidget;auto *columnLayout=new QVBoxLayout(column);columnLayout->setContentsMargins(0,0,0,0);columnLayout->addWidget(new QLabel(i==0?tr("이전 내용"):tr("이후 내용")));columnLayout->addWidget(i==0?left_:right_,1);split->addWidget(column);}
    views->addWidget(split);layout->addWidget(views,1);
    lines_=new QListWidget;lines_->setObjectName("diffLineSelection");lines_->setFont(QFont("Consolas",10));views->addWidget(lines_);
    connect(mode,&QComboBox::currentIndexChanged,this,[views,split](int index){split->setOrientation(index==2?Qt::Vertical:Qt::Horizontal);views->setCurrentIndex(index==0?0:index==3?2:1);});
    auto *actions=new QHBoxLayout;hunks_=new QComboBox;hunks_->setObjectName("diffHunks");actions->addWidget(hunks_,1);apply_=new QPushButton(tr("선택 구간 Stage"));apply_->setObjectName("applyHunk");actions->addWidget(apply_);layout->addLayout(actions);
    applyLines_=new QPushButton(tr("체크한 줄 Stage"));applyLines_->setObjectName("applyDiffLines");applyLines_->setToolTip(tr("줄 선택 보기에서 체크한 +/− 줄만 반영합니다. 교체는 삭제 줄과 추가 줄을 각각 선택하세요. 작업 파일은 바꾸지 않습니다."));actions->addWidget(applyLines_);
    connect(applyLines_,&QPushButton::clicked,this,&DiffWidget::applyLines);
    connect(encoding_,&QComboBox::currentIndexChanged,this,[this]{display(bytes_,isPatch_);});
    connect(left_->verticalScrollBar(),&QScrollBar::valueChanged,right_->verticalScrollBar(),&QScrollBar::setValue);
    connect(right_->verticalScrollBar(),&QScrollBar::valueChanged,left_->verticalScrollBar(),&QScrollBar::setValue);
    connect(apply_,&QPushButton::clicked,this,&DiffWidget::applySelected);
    connect(hunks_,&QComboBox::currentIndexChanged,this,[this](int index){if(index<0||index>=patches_.size())return;auto start=patches_[index].indexOf("@@ ");auto header=QString::fromUtf8(patches_[index].mid(start).split('\n').first());auto cursor=unified_->textCursor();cursor.movePosition(QTextCursor::Start);unified_->setTextCursor(cursor);unified_->find(header);});
    connect(git_,&GitClient::busyChanged,this,[this](bool busy){apply_->setEnabled(!busy&&editable_&&!patches_.isEmpty());applyLines_->setEnabled(!busy&&editable_);lines_->setEnabled(!busy);});clear();
}
void DiffWidget::setPatchActionsVisible(bool visible){auto *mode=findChild<QComboBox*>("diffViewMode");if(!visible&&mode->count()>3){if(mode->currentIndex()==3)mode->setCurrentIndex(0);mode->removeItem(3);}hunks_->setVisible(visible);apply_->setVisible(visible);applyLines_->setVisible(visible);}
void DiffWidget::clear(){bytes_.clear();encoding_->setCurrentIndex(0);patches_.clear();editable_=false;isPatch_=false;unified_->clear();left_->clear();right_->clear();lines_->clear();hunks_->clear();apply_->setEnabled(false);applyLines_->setEnabled(false);info_->setText(tr("파일을 선택하세요"));}
QList<QByteArray> DiffWidget::splitHunks(const QByteArray &patch){
    QList<QByteArray> result;QByteArray header,current;
    for(auto line:patch.split('\n')){line+='\n';if(line.startsWith("@@ ")){if(!current.isEmpty())result.append(header+current);current=line;}else if(current.isEmpty())header+=line;else current+=line;}
    // split() adds an empty final record for newline-terminated patches.
    if(!current.isEmpty()){if(patch.endsWith('\n'))current.chop(1);result.append(header+current);}return result;
}
void DiffWidget::showPatch(const QByteArray &patch){clear();setPatchActionsVisible(false);info_->setText(tr("조회 전용 · 이전 → 이후"));display(patch,true);}
void DiffWidget::display(const QByteArray &bytes,bool patch){
    bytes_=bytes;isPatch_=patch;
    QString text;bool invalid=false;
    if(encoding_->currentIndex()==0){QStringDecoder decode(QStringDecoder::Utf8);text=decode(bytes);invalid=decode.hasError();}
    else if(encoding_->currentIndex()==1)text=QStringDecoder(QStringDecoder::Utf16LE)(bytes);
    else if(encoding_->currentIndex()==2)text=QStringDecoder(QStringDecoder::Utf16BE)(bytes);
    else if(encoding_->currentIndex()==3)text=QString::fromLocal8Bit(bytes);
    else text=QString::fromLatin1(bytes);
    if(bytes.contains('\0')&&encoding_->currentIndex()!=1&&encoding_->currentIndex()!=2){text=tr("바이너리 또는 UTF-16 파일입니다. 텍스트라면 인코딩을 선택하세요.");patch=false;}
    unified_->setPlainText(text);
    if(invalid)info_->setText(tr("인코딩 확인 필요 · 원본 바이트는 유지됩니다"));
    if(!patch){left_->clear();right_->setPlainText(text);return;}
    QStringList before,after,removed,added;int oldLine=0,newLine=0;
    auto flush=[&]{for(int i=0;i<qMax(removed.size(),added.size());++i){before.append(i<removed.size()?QString("- %1  %2").arg(oldLine++).arg(removed[i]):QString());after.append(i<added.size()?QString("+ %1  %2").arg(newLine++).arg(added[i]):QString());}removed.clear();added.clear();};
    QRegularExpression header("^@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@");bool inHunk=false;
    for(const auto &line:text.split('\n')){auto match=header.match(line);if(match.hasMatch()){flush();oldLine=match.captured(1).toInt();newLine=match.captured(2).toInt();before.append(line);after.append(line);inHunk=true;}else if(inHunk&&line.startsWith('-'))removed.append(line.mid(1));else if(inHunk&&line.startsWith('+'))added.append(line.mid(1));else if(inHunk&&line.startsWith(' ')){flush();before.append(QString("%1  %2").arg(oldLine++).arg(line.mid(1)));after.append(QString("%1  %2").arg(newLine++).arg(line.mid(1)));}else if(line.startsWith("diff --git")){flush();inHunk=false;before.append(line);after.append(line);}}
    flush();left_->setPlainText(before.join('\n'));right_->setPlainText(after.join('\n'));
}
void DiffWidget::loadWorking(const QString &path,bool staged){
    if(git_->isBusy())return;clear();reverse_=staged;
    arguments_={"--literal-pathspecs","-c","core.quotePath=false","diff","--no-ext-diff","--no-textconv","--no-color","--no-renames","--unified=3"};if(staged)arguments_.append("--cached");arguments_.append({"--",path});
    git_->inspectLimited(arguments_,[this,path,staged](bool ok,const QByteArray &out,const QString &error){
        if(!ok){info_->setText(error);info_->setToolTip(error);unified_->setPlainText(error);return;}
        info_->setText(staged?tr("HEAD → Index · 선택 구간은 Index에서 제외"):tr("Index → 작업 파일 · 선택 구간은 Index에 추가"));
        if(out.isEmpty()&&!staged){
            git_->inspect({"--literal-pathspecs","ls-files","--error-unmatch","--",path},[this,path](bool tracked,const QByteArray &,const QString &){
                if(tracked){workingPatch({},false);return;}
                QFile file(QDir(git_->repositoryPath()).filePath(path));
                if(!file.open(QIODevice::ReadOnly)){info_->setText(tr("파일을 읽을 수 없습니다."));return;}
                if(file.size()>8*1024*1024){display(file.read(8*1024*1024),false);info_->setText(tr("큰 파일 · 앞 8 MiB만 표시"));return;}
                const auto raw=file.readAll();if(raw.contains('\0')){display(raw,false);info_->setText(tr("바이너리 또는 UTF-16 · 인코딩을 선택해 미리보기"));return;}
                arguments_={"-c","core.quotePath=false","diff","--no-index","--no-ext-diff","--no-textconv","--no-color","--","/dev/null",path};
                git_->inspectLimited(arguments_,[this](bool ok,const QByteArray &out,const QString &error){if(ok)workingPatch(out,false);else info_->setText(error);});
            });return;
        }
        workingPatch(out,staged);
    });
}
void DiffWidget::workingPatch(const QByteArray &out,bool staged){
        const auto header=out.left(out.indexOf("@@ "));const bool tooMany=out.count('\n')>20000;
        QStringDecoder decoder(QStringDecoder::Utf8);decoder(out);
        patches_=splitHunks(out);editable_=!tooMany&&!decoder.hasError()&&!patches_.isEmpty()&&!out.contains("old mode")&&!out.contains("GIT binary patch")&&!out.contains('\0')&&!header.contains("120000")&&!header.contains("160000");
        for(int i=0;i<patches_.size();++i){auto start=patches_[i].indexOf("@@ ");hunks_->addItem(QString::number(i+1)+" · "+QString::fromUtf8(patches_[i].mid(start).split('\n').first()));}
        apply_->setText(staged?tr("선택 구간 Unstage"):tr("선택 구간 Stage"));apply_->setEnabled(editable_);display(out,true);
        // File creation/deletion metadata cannot be applied one hunk at a time; line selection rebuilds it.
        if(out.contains("new file mode")||out.contains("deleted file mode")){patches_.clear();hunks_->clear();apply_->setEnabled(false);}
        applyLines_->setText(staged?tr("체크한 줄 Unstage"):tr("체크한 줄 Stage"));applyLines_->setEnabled(editable_);
        bool inHunk=false;int row=0;
        if(!tooMany)for(const auto &line:out.split('\n')){if(line.startsWith("@@ "))inHunk=true;auto *item=new QListWidgetItem(QString::fromUtf8(line),lines_);item->setData(Qt::UserRole,row++);if(inHunk&&(line.startsWith('+')||line.startsWith('-'))){item->setCheckState(Qt::Unchecked);item->setData(Qt::UserRole+100,line.startsWith('+')?QByteArray("#8ce7bb"):QByteArray("#ff9cad"));item->setForeground(line.startsWith('+')?AppTheme::color("#8ce7bb"):AppTheme::color("#ff9cad"));}}
        if(!editable_)info_->setText(tr("조회 전용 · 2만 줄 초과/비 UTF-8/모드 변경/바이너리는 파일 단위 작업 사용"));
}
void DiffWidget::loadRevisions(const QString &base,const QString &head,const QString &path){
    if(git_->isBusy())return;clear();
    QStringList args{"-c","core.quotePath=false"};
    if(base.isEmpty())args.append({"show","--format=","--root",head});else args.append({"diff",base,head});
    args.append({"--no-ext-diff","--no-textconv","--no-color","--no-renames","--unified=3","--",path});
    git_->inspectLimited(args,[this](bool ok,const QByteArray &out,const QString &error){info_->setText(ok?tr("이전 → 이후 · 변경 구간과 주변 문맥 비교"):error);info_->setToolTip(ok?QString():error);if(ok)display(out,true);else unified_->setPlainText(error);});
}
void DiffWidget::applySelected(){
    int at=hunks_->currentIndex();if(!editable_||git_->isBusy()||at<0||at>=patches_.size())return;
    applyReviewed(patches_[at],reverse_);
}
void DiffWidget::applyLines(){
    if(!editable_||git_->isBusy())return;QSet<int> selected;
    for(int i=0;i<lines_->count();++i)if(lines_->item(i)->checkState()==Qt::Checked)selected.insert(lines_->item(i)->data(Qt::UserRole).toInt());
    const auto patch=selectedLinesPatch(bytes_,selected,reverse_);if(patch.isEmpty()){info_->setText(tr("반영할 +/− 줄을 체크하세요."));return;}applyReviewed(patch,false);
}
void DiffWidget::applyReviewed(const QByteArray &patch,bool reverse){
    const auto snapshot=bytes_;
    git_->inspectLimited(arguments_,[this,patch,snapshot,reverse](bool ok,const QByteArray &out,const QString &error){
        if(!ok||out!=snapshot){editable_=false;apply_->setEnabled(false);info_->setText(ok?tr("파일 또는 Index가 바뀌었습니다. 다시 선택해 최신 Diff를 확인하세요."):error);return;}
        git_->applyPatch(patch,reverse,[this](bool ok,const QByteArray &,const QString &error){if(!ok){info_->setText(error);info_->setToolTip(error);unified_->setPlainText(error);return;}clear();emit indexChanged();});
    });
}
