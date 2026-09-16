#include "gittoolbox.h"
#include "gitcommandform.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

GitToolbox::GitToolbox(GitClient *git,QWidget *parent):QWidget(parent),git_(git),controller_(git,this),catalog_(ToolboxController::catalog()){
    setObjectName("gitToolbox");auto *layout=new QVBoxLayout(this);
    auto *commandForm=new QPushButton(tr("전체 Git 명령 · 옵션 GUI"));commandForm->setObjectName("openGitCommandForm");layout->addWidget(commandForm);
    connect(commandForm,&QPushButton::clicked,this,[this]{if(git_->isBusy())return;GitCommandForm dialog(git_,this);connect(&dialog,&GitCommandForm::repositoryChanged,this,&GitToolbox::repositoryChanged);dialog.exec();});
    auto *row=new QHBoxLayout;search_=new QLineEdit;search_->setObjectName("gitToolSearch");search_->setPlaceholderText(tr("기능 검색: 태그, worktree, 패치, 설정…"));row->addWidget(search_,1);tools_=new QComboBox;tools_->setObjectName("gitToolChoice");row->addWidget(tools_,2);layout->addLayout(row);
    auto *split=new QSplitter(Qt::Horizontal);auto *input=new QWidget;auto *left=new QVBoxLayout(input);description_=new QLabel;description_->setWordWrap(true);description_->setTextFormat(Qt::PlainText);left->addWidget(description_);
    auto *formWidget=new QWidget;form_=new QFormLayout(formWidget);auto *scroll=new QScrollArea;scroll->setObjectName("gitToolInputs");scroll->setWidgetResizable(true);scroll->setWidget(formWidget);left->addWidget(scroll,1);
    expert_=new QCheckBox(tr("고급 명령의 인자와 영향 범위를 직접 확인했습니다."));expert_->setObjectName("expertGitAcknowledged");left->addWidget(expert_);
    reviewButton_=new QPushButton(tr("조회 / 실행 미리보기"));reviewButton_->setObjectName("previewGitTool");executeButton_=new QPushButton(tr("확인 후 실행"));executeButton_->setObjectName("executeGitTool");auto *actions=new QHBoxLayout;actions->addWidget(reviewButton_);actions->addWidget(executeButton_);left->addLayout(actions);
    preview_=new QPlainTextEdit;preview_->setReadOnly(true);preview_->setObjectName("gitToolPreview");preview_->setMaximumHeight(180);left->addWidget(preview_);split->addWidget(input);
    auto *output=new QWidget;auto *right=new QVBoxLayout(output);table_=new QTableWidget;table_->setObjectName("gitToolTable");table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->horizontalHeader()->setStretchLastSection(true);right->addWidget(table_,1);
    result_=new QPlainTextEdit;result_->setObjectName("gitToolResult");result_->setReadOnly(true);result_->setLineWrapMode(QPlainTextEdit::NoWrap);right->addWidget(result_,2);
    auto *find=new QLineEdit;find->setPlaceholderText(tr("결과에서 찾기 · Enter로 다음 일치"));right->addWidget(find);connect(find,&QLineEdit::returnPressed,this,[this,find]{if(!result_->find(find->text())){result_->moveCursor(QTextCursor::Start);result_->find(find->text());}});
    auto *resultActions=new QHBoxLayout;openButton_=new QPushButton(tr("선택 작업 트리 열기"));openButton_->setObjectName("openToolWorktree");resultActions->addWidget(openButton_);auto *copy=new QPushButton(tr("결과 복사"));resultActions->addWidget(copy);right->addLayout(resultActions);split->addWidget(output);split->setSizes({430,600});layout->addWidget(split,1);
    connect(copy,&QPushButton::clicked,this,[this]{QApplication::clipboard()->setText(result_->toPlainText());});
    connect(openButton_,&QPushButton::clicked,this,[this]{const int row=table_->currentRow();if(row>=0&&table_->item(row,0))emit openWorktree(table_->item(row,0)->text());});
    connect(table_,&QTableWidget::itemSelectionChanged,this,&GitToolbox::update);
    connect(tools_,&QComboBox::currentIndexChanged,this,&GitToolbox::selectTool);
    connect(search_,&QLineEdit::textChanged,this,[this](const QString &text){const QSignalBlocker block(tools_);const auto previous=tools_->currentData();tools_->clear();for(const auto &tool:catalog_)if((tool.id+" "+tool.group+" "+tool.title+" "+tool.description).contains(text,Qt::CaseInsensitive))tools_->addItem(tool.group+" · "+tool.title,tool.id);int index=tools_->findData(previous);if(index>=0)tools_->setCurrentIndex(index);selectTool();});
    connect(reviewButton_,&QPushButton::clicked,this,&GitToolbox::preview);connect(executeButton_,&QPushButton::clicked,this,&GitToolbox::execute);connect(expert_,&QCheckBox::toggled,this,&GitToolbox::update);
    connect(git,&GitClient::busyChanged,this,&GitToolbox::update);connect(git,&GitClient::operationStateChanged,this,&GitToolbox::update);
    {const QSignalBlocker block(tools_);for(const auto &tool:catalog_)tools_->addItem(tool.group+" · "+tool.title,tool.id);}selectTool();
}
void GitToolbox::selectTool(){
    fields_.clear();while(form_->rowCount())form_->removeRow(0);review_={};preview_->clear();table_->clear();table_->setRowCount(0);table_->hide();result_->clear();expert_->setChecked(false);
    const auto id=tools_->currentData().toString();description_->clear();expert_->setVisible(id=="expert");
    for(const auto &tool:catalog_)if(tool.id==id){description_->setText(tool.description);
        for(const auto &field:tool.fields){QWidget *widget=nullptr;
            if(!field.choices.isEmpty()){auto *combo=new QComboBox;combo->addItems(field.choices);combo->setCurrentText(field.initial);widget=combo;connect(combo,&QComboBox::currentTextChanged,this,[this]{review_={};update();});}
            else if(field.key=="arguments"||field.key=="message"||field.key=="stdin"||field.key=="paths"){auto *edit=new QPlainTextEdit(field.initial);edit->setMinimumHeight(field.key=="arguments"?170:80);widget=edit;connect(edit,&QPlainTextEdit::textChanged,this,[this]{review_={};update();});}
            else {auto *edit=new QLineEdit(field.initial);widget=edit;connect(edit,&QLineEdit::textChanged,this,[this]{review_={};update();});}
            widget->setObjectName("gitTool_"+field.key);fields_.insert(field.key,widget);
            if(field.key=="path"||field.key=="destination"){
                auto *container=new QWidget;auto *line=new QHBoxLayout(container);line->setContentsMargins(0,0,0,0);line->addWidget(widget,1);auto *browse=new QPushButton(tr("찾기…"));line->addWidget(browse);form_->addRow(field.label,container);
                connect(browse,&QPushButton::clicked,this,[this,widget,id,field]{QFileDialog dialog(this,tr("경로 선택"),git_->repositoryPath());dialog.setOption(QFileDialog::DontUseNativeDialog);dialog.setWindowFlag(Qt::WindowTitleHint);const bool output=id.startsWith("export.")||id=="worktree.add"||id=="worktree.attach"||id=="worktree.detached"||id=="submodule.add"||field.key=="destination";dialog.setAcceptMode(output?QFileDialog::AcceptSave:QFileDialog::AcceptOpen);dialog.setFileMode(output?QFileDialog::AnyFile:(id.startsWith("worktree.")||id.startsWith("submodule."))?QFileDialog::Directory:QFileDialog::ExistingFile);if(dialog.exec()==QDialog::Accepted&&!dialog.selectedFiles().isEmpty())qobject_cast<QLineEdit*>(widget)->setText(id.startsWith("submodule.")||id.startsWith("lfs.")?QDir(git_->repositoryPath()).relativeFilePath(dialog.selectedFiles().first()):dialog.selectedFiles().first());});
            }else form_->addRow(field.label,widget);
        }break;
    }findChild<QScrollArea*>("gitToolInputs")->setVisible(!fields_.isEmpty());preview_->setMaximumHeight(fields_.isEmpty()?QWIDGETSIZE_MAX:180);update();
}
void GitToolbox::update(){
    if(repository_!=git_->repositoryPath()){repository_=git_->repositoryPath();review_={};preview_->clear();result_->clear();table_->setRowCount(0);}
    const bool ready=!loading_&&!git_->isBusy()&&!repository_.isEmpty();const bool chosen=tools_->currentIndex()>=0;
    tools_->setEnabled(!loading_&&!git_->isBusy());search_->setEnabled(tools_->isEnabled());form_->parentWidget()->setEnabled(tools_->isEnabled());expert_->setEnabled(tools_->isEnabled());
    reviewButton_->setEnabled(ready&&chosen);executeButton_->setEnabled(ready&&!review_.arguments.isEmpty()&&!review_.query&&(review_.id!="expert"||expert_->isChecked()));
    openButton_->setVisible(tools_->currentData().toString()=="worktree.list");openButton_->setEnabled(ready&&table_->currentRow()>=0);
}
void GitToolbox::preview(){
    if(loading_||git_->isBusy())return;QMap<QString,QString> values;
    for(auto it=fields_.begin();it!=fields_.end();++it){if(auto *edit=qobject_cast<QLineEdit*>(it.value()))values.insert(it.key(),edit->text());else if(auto *combo=qobject_cast<QComboBox*>(it.value()))values.insert(it.key(),combo->currentText());else values.insert(it.key(),qobject_cast<QPlainTextEdit*>(it.value())->toPlainText());}
    review_={};loading_=true;update();controller_.review(tools_->currentData().toString(),values,[this](bool ok,ToolReview review,QString error){
        if(!ok){loading_=false;preview_->setPlainText(error);update();return;}review_=review;QJsonArray args;for(const auto &arg:review.arguments)args.append(arg);
        preview_->setPlainText(tr("저장소: %1\n\n%2\n\nGit 인자 배열:\n%3\n\nHEAD 참조는 미커밋 파일·설정·원격의 전체 백업이 아닙니다.").arg(review.repository,review.description,QString::fromUtf8(QJsonDocument(args).toJson()))+"\nUTF-8 stdin:\n"+QString::fromUtf8(review.standardInput));
        if(review.query){controller_.execute(review,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;renderResult(out);if(!error.isEmpty())result_->appendPlainText(error);if(!ok)result_->appendPlainText(tr("조회 실패. 위 진단을 확인하세요."));update();});return;}
        loading_=false;update();if(review.id=="tag.publish"||review.id=="tag.remote-delete")emit repositoryChanged();
    });
}
void GitToolbox::execute(){
    if(loading_||git_->isBusy()||review_.arguments.isEmpty()||(review_.id=="expert"&&!expert_->isChecked()))return;
    QMessageBox box(QMessageBox::Warning,tr("Git 작업 확인"),review_.repository+"\n\n"+review_.description+tr("\n\n미리보기의 인자로 실행합니다. 변경은 자동으로 되돌려지지 않습니다."),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setDetailedText(preview_->toPlainText());box.setTextFormat(Qt::PlainText);box.setDefaultButton(QMessageBox::No);if(box.exec()!=QMessageBox::Yes)return;
    loading_=true;update();controller_.execute(review_,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;review_={};renderResult(out);result_->appendPlainText(ok?tr("작업을 완료했습니다."):tr("작업 실패. 일부 변경이 남았을 수 있습니다. 상태를 확인하세요."));if(!error.isEmpty())result_->appendPlainText(error);update();emit repositoryChanged();});
}
void GitToolbox::renderResult(const QByteArray &bytes){
    auto display=bytes;display.replace('\0','\n');result_->setPlainText(QString::fromUtf8(display));const auto id=tools_->currentData().toString();table_->setRowCount(0);
    auto add=[this](const QStringList &values){const int row=table_->rowCount();table_->insertRow(row);for(int col=0;col<values.size();++col){auto *item=new QTableWidgetItem(values[col]);item->setToolTip(values[col]);table_->setItem(row,col,item);}};
    if(id=="tag.list"){table_->setColumnCount(4);table_->setHorizontalHeaderLabels({tr("이름"),tr("객체 ID"),tr("유형"),tr("설명")});for(const auto &line:QString::fromUtf8(bytes).split('\n',Qt::SkipEmptyParts))add(line.split('\t').mid(0,4));table_->show();}
    else if(id=="worktree.list"){table_->setColumnCount(3);table_->setHorizontalHeaderLabels({tr("경로"),tr("브랜치 / 상태"),"HEAD"});QString path,branch,head;
        for(const auto &part:bytes.split('\0')){const auto line=QString::fromUtf8(part);if(line.startsWith("worktree ")){path=line.mid(9);branch.clear();head.clear();}else if(line.startsWith("branch "))branch=line.mid(7);else if(line.startsWith("HEAD "))head=line.mid(5);else if(line=="detached"||line.startsWith("locked")||line.startsWith("prunable"))branch+=" "+line;else if(line.isEmpty()&&!path.isEmpty()){add({path,branch,head});path.clear();}}
        table_->setColumnWidth(0,300);table_->setColumnWidth(1,180);table_->setWordWrap(false);table_->show();
    }else table_->hide();update();
}
