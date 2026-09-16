#include "pullrequestdialog.h"
#include "githubclient.h"
#include "diff/diffwidget.h"
#include "git/gitclient.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QLabel *label(const QString &text){auto *w=new QLabel(text);w->setTextFormat(Qt::PlainText);w->setWordWrap(true);return w;}
bool confirm(QWidget *parent,const QString &text){QMessageBox box(QMessageBox::Question,QObject::tr("GitHub에 작업 반영"),text,QMessageBox::Yes|QMessageBox::No,parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setDefaultButton(QMessageBox::No);return box.exec()==QMessageBox::Yes;}
QString sha(const QJsonObject &pr){return pr.value("head").toObject().value("sha").toString();}
QString branch(const QJsonObject &pr,const char *key){return pr.value(key).toObject().value("label").toString();}
class CreateDialog final : public QDialog {
public:
    explicit CreateDialog(PullRequests *requests,QWidget *parent):QDialog(parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint),requests_(requests){}
    void reject() override {if(!requests_->busy())QDialog::reject();}
private: PullRequests *requests_;
};
}
PullRequestDialog::PullRequestDialog(GithubClient *client,GitClient *git,const QString &host,const QString &login,const QString &repository,QWidget *parent)
    :QDialog(parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint),requests_(client,git,host,login,repository,this),host_(host),login_(login){
    setObjectName("pullRequestDialog");resize(1280,820);auto *layout=new QVBoxLayout(this);
    layout->addWidget(label(tr("Pull Request · %1 / %2\n브랜치의 변경 사항을 함께 검토하고 병합하는 공간입니다. 댓글·리뷰·검사를 확인하고 GitHub에 작업을 반영할 수 있습니다.").arg(host,repository)));
    auto button=[this](QHBoxLayout *row,const QString &text,const char *name){auto *w=new QPushButton(text);w->setObjectName(name);row->addWidget(w);controls_.append(w);return w;};
    auto *toolbar=new QHBoxLayout;state_=new QComboBox;state_->setObjectName("prState");state_->addItem(tr("열린 PR"),"open");state_->addItem(tr("닫힌 PR·병합됨"),"closed");state_->addItem(tr("전체"),"all");toolbar->addWidget(state_);controls_<<state_;
    state_->addItem(tr("병합된 PR"),"merged");state_->addItem(tr("초안 PR"),"draft");
    search_=new QLineEdit;search_->setObjectName("prSearch");search_->setPlaceholderText(tr("불러온 제목·번호·작성자·브랜치·리뷰어·라벨 검색"));toolbar->addWidget(search_,1);
    auto *refresh=button(toolbar,tr("목록 새로고침"),"refreshPullRequests");more_=button(toolbar,tr("다음 100개"),"morePullRequests");auto *newPr=button(toolbar,tr("PR 만들기"),"createPullRequest");cancel_=button(toolbar,tr("요청 중단"),"cancelPullRequest");auto *close=button(toolbar,tr("닫기"),"closePullRequests");layout->addLayout(toolbar);
    auto *split=new QSplitter;list_=new QTableWidget(0,4);list_->setObjectName("pullRequests");list_->setHorizontalHeaderLabels({tr("번호"),tr("제목"),tr("작성자"),tr("상태")});list_->setEditTriggers(QAbstractItemView::NoEditTriggers);list_->setSelectionBehavior(QAbstractItemView::SelectRows);list_->setSelectionMode(QAbstractItemView::SingleSelection);list_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);list_->verticalHeader()->hide();split->addWidget(list_);controls_<<list_;
    auto *right=new QWidget;auto *rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(0,0,0,0);summary_=label(tr("PR을 선택하세요."));summary_->setObjectName("prSummary");rightLayout->addWidget(summary_);
    auto *detailsActions=new QHBoxLayout;reload_=button(detailsActions,tr("상세·CI 다시 읽기"),"reloadPullRequest");web_=button(detailsActions,tr("GitHub에서 열기"),"browsePullRequest");checkout_=button(detailsActions,tr("로컬에서 열기"),"checkoutPullRequest");rightLayout->addLayout(detailsActions);
    for(int col:{0,2,3})list_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::ResizeToContents);list_->setWordWrap(false);
    auto *vertical=new QSplitter(Qt::Vertical);description_=new QPlainTextEdit;description_->setObjectName("prDescription");description_->setReadOnly(true);vertical->addWidget(description_);
    auto *changes=new QSplitter;files_=new QTableWidget(0,3);files_->setObjectName("prFiles");files_->setHorizontalHeaderLabels({tr("변경 파일"),tr("상태"),"+ / −"});files_->setEditTriggers(QAbstractItemView::NoEditTriggers);files_->setSelectionBehavior(QAbstractItemView::SelectRows);files_->setSelectionMode(QAbstractItemView::SingleSelection);files_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);files_->verticalHeader()->hide();changes->addWidget(files_);
    for(int col:{1,2})files_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::ResizeToContents);files_->setWordWrap(false);
    diff_=new DiffWidget(git);diff_->setObjectName("prDiff");diff_->setPatchActionsVisible(false);changes->addWidget(diff_);changes->setSizes({220,520});vertical->addWidget(changes);vertical->setSizes({220,340});rightLayout->addWidget(vertical,1);
    auto *reviewRow=new QHBoxLayout;action_=new QComboBox;action_->setObjectName("prReviewAction");action_->addItem(tr("댓글 작성"),"comment");action_->addItem(tr("승인 리뷰"),"approve");action_->addItem(tr("변경 요청 리뷰"),"request_changes");action_->addItem(tr("리뷰어 요청"),"reviewers");reviewRow->addWidget(action_);send_=button(reviewRow,tr("GitHub에 보내기"),"submitPullRequestReview");rightLayout->addLayout(reviewRow);controls_<<action_;
    action_->addItem(tr("리뷰 요청 취소"),"remove_reviewers");action_->addItem(tr("Base 변경 사항으로 PR 갱신"),"update_branch");action_->addItem(tr("초안 → 리뷰 준비"),"ready");
    input_=new QPlainTextEdit;input_->setObjectName("prReviewBody");input_->setPlaceholderText(tr("댓글·리뷰 내용을 입력하세요. 리뷰어 요청/취소는 로그인 이름을 쉼표로 구분하며 팀은 team:팀이름으로 입력합니다."));input_->setMaximumHeight(85);rightLayout->addWidget(input_);controls_<<input_;
    auto *mergeRow=new QHBoxLayout;method_=new QComboBox;method_->setObjectName("prMergeMethod");method_->addItem(tr("Merge 커밋"),"merge");method_->addItem(tr("Squash · 하나로 합치기"),"squash");method_->addItem(tr("Rebase · 순서대로 합치기"),"rebase");mergeRow->addWidget(method_);controls_<<method_;merge_=button(mergeRow,tr("PR 병합"),"mergePullRequest");closePr_=button(mergeRow,tr("PR 닫기"),"closePullRequest");reopen_=button(mergeRow,tr("PR 다시 열기"),"reopenPullRequest");rightLayout->addLayout(mergeRow);
    split->addWidget(right);split->setSizes({380,860});layout->addWidget(split,1);status_=label({});status_->setObjectName("prStatus");layout->addWidget(status_);
    connect(close,&QPushButton::clicked,this,&PullRequestDialog::reject);connect(cancel_,&QPushButton::clicked,&requests_,&PullRequests::cancel);
    connect(method_,&QComboBox::currentIndexChanged,this,&PullRequestDialog::update);
    connect(refresh,&QPushButton::clicked,this,[this]{valid_=false;detail_={};files_->setRowCount(0);diff_->clear();description_->clear();summary_->setText(tr("PR을 선택하세요."));list_->setRowCount(0);requests_.list((state_->currentIndex()==3?QString("closed"):state_->currentIndex()==4?QString("open"):state_->currentData().toString()),1);});
    connect(state_,&QComboBox::currentIndexChanged,refresh,&QPushButton::click);connect(more_,&QPushButton::clicked,this,[this]{requests_.list((state_->currentIndex()==3?QString("closed"):state_->currentIndex()==4?QString("open"):state_->currentData().toString()),page_+1);});connect(newPr,&QPushButton::clicked,this,&PullRequestDialog::create);
    connect(search_,&QLineEdit::textChanged,this,[this]{for(int row=0;row<list_->rowCount();++row){QString text;for(int col=0;col<4;++col)text+=list_->item(row,col)->text()+" ";const auto pr=list_->item(row,0)->data(Qt::UserRole+1).toJsonObject();text+=branch(pr,"head")+" "+branch(pr,"base");
        for(const auto &v:pr.value("requested_reviewers").toArray())text+=" "+v.toObject().value("login").toString();for(const auto &v:pr.value("labels").toArray())text+=" "+v.toObject().value("name").toString();
        const bool merged=!pr.value("merged_at").isNull()&&!pr.value("merged_at").isUndefined();list_->setRowHidden(row,!text.contains(search_->text(),Qt::CaseInsensitive)||(state_->currentIndex()==3&&!merged)||(state_->currentIndex()==4&&!pr.value("draft").toBool()));}});
    connect(list_,&QTableWidget::itemSelectionChanged,this,[this]{if(requests_.busy())return;const int row=list_->currentRow();if(row<0)return;valid_=false;requests_.detail(list_->item(row,0)->data(Qt::UserRole).toInt());});
    connect(reload_,&QPushButton::clicked,this,[this]{valid_=false;requests_.detail(detail_.value("number").toInt());});
    connect(web_,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl("https://"+host_+"/"+requests_.repository()+"/pull/"+QString::number(detail_.value("number").toInt())));});
    connect(checkout_,&QPushButton::clicked,this,[this,git]{if(confirm(this,tr("PR #%1의 %2 커밋을 현재 로컬 폴더 %3에 가져와 pr/번호-커밋 브랜치로 전환합니다. 원격 주소가 일치하고 작업 폴더가 깨끗해야 합니다.").arg(detail_.value("number").toInt()).arg(sha(detail_).left(12),git->repositoryPath())))requests_.checkout(detail_);});
    connect(send_,&QPushButton::clicked,this,[this]{submit(action_->currentData().toString(),input_->toPlainText());});connect(merge_,&QPushButton::clicked,this,[this]{submit(method_->currentData().toString());});connect(closePr_,&QPushButton::clicked,this,[this]{submit("close");});connect(reopen_,&QPushButton::clicked,this,[this]{submit("reopen");});
    connect(files_,&QTableWidget::itemSelectionChanged,this,[this]{const int row=files_->currentRow();diff_->clear();if(row<0)return;const auto file=files_->item(row,0)->data(Qt::UserRole).toJsonObject();const auto patch=file.value("patch").toString();if(patch.isEmpty()){diff_->unified()->setPlainText(tr("이 파일의 Diff는 API에서 제공되지 않았습니다. 바이너리·큰 파일 등은 GitHub 또는 로컬 체크아웃에서 확인하세요."));return;}const auto name=file.value("filename").toString();diff_->showPatch(("diff --git a/"+name+" b/"+name+"\n--- a/"+file.value("previous_filename").toString(name)+"\n+++ b/"+name+"\n"+patch+"\n").toUtf8());});
    connect(&requests_,&PullRequests::busyChanged,this,[this](bool busy){update();emit busyChanged(busy);});connect(&requests_,&PullRequests::message,status_,&QLabel::setText);connect(&requests_,&PullRequests::checkedOut,this,&PullRequestDialog::checkedOut);
    connect(&requests_,&PullRequests::listed,this,[this](const QJsonArray &rows,int page){if(page==1)list_->setRowCount(0);page_=page;moreAvailable_=rows.size()==100;for(const auto &value:rows){const auto pr=value.toObject();const int row=list_->rowCount();list_->insertRow(row);QStringList columns{QString::number(pr.value("number").toInt()),pr.value("title").toString(),pr.value("user").toObject().value("login").toString(),!pr.value("merged_at").isNull()&&!pr.value("merged_at").isUndefined()?tr("병합됨"):pr.value("draft").toBool()?tr("초안"):pr.value("state").toString()};for(int col=0;col<4;++col)list_->setItem(row,col,new QTableWidgetItem(columns[col]));list_->item(row,0)->setData(Qt::UserRole,pr.value("number").toInt());list_->item(row,0)->setData(Qt::UserRole+1,pr);}emit search_->textChanged(search_->text());});
    connect(&requests_,&PullRequests::loaded,this,&PullRequestDialog::display);
    connect(&requests_,&PullRequests::completed,this,[this](bool ok){if(!ok)valid_=false;const bool refresh=mutating_&&ok;mutating_=false;if(refresh){input_->clear();valid_=false;requests_.detail(detail_.value("number").toInt());}update();});
    update();QTimer::singleShot(0,this,[this]{requests_.list("open",1);});
}
void PullRequestDialog::reject(){if(!requests_.busy())QDialog::reject();}
void PullRequestDialog::update(){
    const bool idle=!requests_.busy();for(auto *control:controls_)control->setEnabled(idle);cancel_->setEnabled(!idle);more_->setEnabled(idle&&moreAvailable_);
    const bool present=detail_.value("number").toInt()>0;reload_->setEnabled(idle&&present);web_->setEnabled(idle&&present);checkout_->setEnabled(idle&&valid_);
    const auto method=method_->currentData().toString();const bool allowed=detail_.value("repository_settings").toObject().value(method=="merge"?"allow_merge_commit":"allow_"+method+"_merge").toBool();
    send_->setEnabled(idle&&valid_);merge_->setEnabled(idle&&valid_&&PullRequests::canMerge(detail_)&&allowed);closePr_->setEnabled(idle&&valid_&&detail_.value("state")=="open");reopen_->setEnabled(idle&&valid_&&detail_.value("state")=="closed"&&!detail_.value("merged").toBool());
}
void PullRequestDialog::display(const QJsonObject &pr){
    const int number=pr.value("number").toInt();if(number!=reviewNumber_){if(reviewNumber_>0)reviewDrafts_[reviewNumber_]=input_->toPlainText();reviewNumber_=number;input_->setPlainText(reviewDrafts_.value(number));}
    detail_=pr;valid_=pr.contains("combined_status");summary_->setText(tr("#%1 %2\n%3 → %4 · %5 · %6\n병합 상태: %7 · 변경 파일 %8개 (+%9 / −%10)").arg(pr.value("number").toInt()).arg(pr.value("title").toString(),branch(pr,"head"),branch(pr,"base"),sha(pr).left(12),pr.value("merged").toBool()?tr("병합됨"):pr.value("state").toString(),pr.value("mergeable_state").toString(tr("아직 확인되지 않음"))).arg(pr.value("changed_files").toInt()).arg(pr.value("additions").toInt()).arg(pr.value("deletions").toInt()));
    QString text=pr.value("body").toString()+tr("\n\n── 커밋 ──\n");for(const auto &v:pr.value("commits").toArray()){const auto c=v.toObject();text+=c.value("sha").toString().left(12)+" · "+c.value("commit").toObject().value("message").toString()+"\n";}
    text+=tr("\n── 요청한 리뷰어 ──\n");for(const auto &v:pr.value("requested_reviewers").toArray())text+=v.toObject().value("login").toString()+"\n";for(const auto &v:pr.value("requested_teams").toArray())text+="team:"+v.toObject().value("slug").toString()+"\n";
    text+=tr("\n── CI 검사 ──\n");for(const auto &v:pr.value("check_runs").toArray()){const auto c=v.toObject();text+=c.value("name").toString()+" · "+c.value("status").toString()+" / "+c.value("conclusion").toString()+"\n";}
    const auto combined=pr.value("combined_status").toObject();text+=tr("Commit status: %1 (%2)\n").arg(combined.value("state").toString()).arg(combined.value("total_count").toInt());for(const auto &v:combined.value("statuses").toArray()){const auto c=v.toObject();text+=c.value("context").toString()+" · "+c.value("state").toString()+" · "+c.value("description").toString()+"\n";}
    text+=tr("\n── 리뷰 ──\n");for(const auto &v:pr.value("reviews").toArray()){const auto r=v.toObject();text+=r.value("user").toObject().value("login").toString()+" · "+r.value("state").toString()+" · "+r.value("submitted_at").toString()+"\n"+r.value("body").toString()+"\n\n";}
    text+=tr("\n── 댓글 ──\n");for(const auto &v:pr.value("comments").toArray()){const auto c=v.toObject();text+=c.value("user").toObject().value("login").toString()+" · "+c.value("created_at").toString()+"\n"+c.value("body").toString()+"\n\n";}
    text+=tr("\n── 코드 리뷰 댓글 ──\n");for(const auto &v:pr.value("review_comments").toArray()){const auto c=v.toObject();text+=c.value("path").toString()+":"+QString::number(c.value("line").toInt(c.value("original_line").toInt()))+" · "+c.value("user").toObject().value("login").toString()+"\n"+c.value("body").toString()+"\n\n";}description_->setPlainText(text);
    files_->setRowCount(0);diff_->clear();for(const auto &v:pr.value("files").toArray()){const auto f=v.toObject();const int row=files_->rowCount();files_->insertRow(row);files_->setItem(row,0,new QTableWidgetItem(f.value("filename").toString()));files_->item(row,0)->setData(Qt::UserRole,f);files_->setItem(row,1,new QTableWidgetItem(f.value("status").toString()));files_->setItem(row,2,new QTableWidgetItem(QString("+%1 / -%2").arg(f.value("additions").toInt()).arg(f.value("deletions").toInt())));}if(files_->rowCount())files_->selectRow(0);
    status_->setText(PullRequests::canMerge(pr)?tr("검사 상태를 읽었습니다. 병합 시 커밋과 조건을 다시 확인합니다."):tr("병합은 초안 해제, 충돌 해결, 리뷰·검사 조건 충족과 clean 상태 확인 후 사용할 수 있습니다. 검사/파일 응답이 생략되면 GitHub 웹에서도 확인하세요."));update();
}
void PullRequestDialog::submit(const QString &action,const QString &text){
    if(!valid_)return;
    const auto actionName=action_->findData(action)>=0?action_->itemText(action_->findData(action)):method_->findData(action)>=0?tr("PR 병합 · ")+method_->itemText(method_->findData(action)):action=="close"?tr("PR 닫기"):tr("PR 다시 열기");
    if(!confirm(this,tr("%1 · %2 계정\n%3 PR #%4\n%5 → %6\n확인한 커밋: %7\n작업: %8\n\n%9\n\n이 내용을 GitHub에 반영할까요?").arg(host_,login_,requests_.repository()).arg(detail_.value("number").toInt()).arg(branch(detail_,"head"),branch(detail_,"base"),sha(detail_),actionName,text)))return;
    mutating_=true;requests_.act(action,detail_,text);if(!requests_.busy())mutating_=false;
}
void PullRequestDialog::create(){
    CreateDialog dialog(&requests_,this);dialog.setObjectName("createPullRequestDialog");dialog.resize(720,560);auto *layout=new QVBoxLayout(&dialog);layout->addWidget(label(tr("새 Pull Request · %1\nGitHub에 이미 올린 브랜치를 선택하세요. 로컬 파일을 자동으로 Push하지 않습니다.").arg(requests_.repository())));
    auto *form=new QFormLayout;auto *source=new QLineEdit(requests_.repository());source->setObjectName("prSourceRepository");auto *load=new QPushButton(tr("브랜치 불러오기"));load->setObjectName("loadPrBranches");auto *base=new QComboBox;auto *head=new QComboBox;base->setObjectName("prBaseBranch");head->setObjectName("prHeadBranch");auto *title=new QLineEdit;title->setObjectName("prTitle");title->setMaxLength(256);auto *body=new QPlainTextEdit;body->setObjectName("prBody");auto *draft=new QCheckBox(tr("초안 PR로 생성"));draft->setObjectName("prDraft");form->addRow(tr("변경 브랜치 저장소"),source);form->addRow(load);form->addRow(tr("합류할 브랜치 (base)"),base);form->addRow(tr("변경 브랜치 (head)"),head);form->addRow(tr("제목"),title);form->addRow(tr("설명"),body);form->addRow(draft);layout->addLayout(form);
    auto *previewRow=new QHBoxLayout;auto *previewButton=new QPushButton(tr("커밋·Diff 미리보기"));previewButton->setObjectName("previewNewPullRequest");previewRow->addWidget(previewButton);auto *templatePath=new QComboBox;templatePath->addItems({".github/pull_request_template.md",".github/PULL_REQUEST_TEMPLATE.md","pull_request_template.md","PULL_REQUEST_TEMPLATE.md","docs/pull_request_template.md"});previewRow->addWidget(templatePath,1);auto *templateButton=new QPushButton(tr("템플릿 읽기"));previewRow->addWidget(templateButton);layout->addLayout(previewRow);
    auto *preview=new QPlainTextEdit;preview->setObjectName("prCreationPreview");preview->setReadOnly(true);preview->setMaximumHeight(130);layout->addWidget(preview);
    auto *notice=label({});layout->addWidget(notice);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setText(tr("PR 생성"));buttons->button(QDialogButtonBox::Save)->setObjectName("submitNewPullRequest");layout->addWidget(buttons);bool creating=false,loadingHead=false;QString loadedSource;
    auto *stop=new QPushButton(tr("현재 요청 중단"));stop->setObjectName("cancelNewPullRequest");stop->setEnabled(false);layout->addWidget(stop);connect(stop,&QPushButton::clicked,&requests_,&PullRequests::cancel);connect(&requests_,&PullRequests::busyChanged,stop,&QPushButton::setEnabled);
    connect(previewButton,&QPushButton::clicked,&dialog,[&]{requests_.preview(source->text().trimmed(),head->currentText(),base->currentText());});
    connect(templateButton,&QPushButton::clicked,&dialog,[&]{if(!body->toPlainText().isEmpty()&&!confirm(&dialog,tr("입력한 설명을 저장소의 PR 템플릿으로 바꿀까요?")))return;requests_.loadTemplate(templatePath->currentText());});
    connect(&requests_,&PullRequests::templateReady,&dialog,[body](const QString &text){body->setPlainText(text);});
    connect(&requests_,&PullRequests::previewReady,&dialog,[preview](const QJsonObject &data){QString text=tr("비교: %1 · 커밋 %2개\n처음 100개 커밋·최대 300개 파일 미리보기입니다. 전체 변경은 생성 후 상세 또는 GitHub에서 확인하세요.\n").arg(data.value("status").toString()).arg(data.value("total_commits").toInt());for(const auto &v:data.value("commits").toArray()){const auto c=v.toObject();text+=c.value("sha").toString().left(12)+" "+c.value("commit").toObject().value("message").toString()+"\n";}for(const auto &v:data.value("files").toArray()){const auto f=v.toObject();text+="\n"+f.value("filename").toString()+"\n"+f.value("patch").toString(tr("Diff 생략"))+"\n";}preview->setPlainText(text);});
    connect(base,&QComboBox::currentIndexChanged,preview,&QPlainTextEdit::clear);connect(head,&QComboBox::currentIndexChanged,preview,&QPlainTextEdit::clear);
    connect(&requests_,&PullRequests::message,&dialog,[notice](const QString &text){notice->setText(text);});
    connect(source,&QLineEdit::textChanged,&dialog,[&]{head->clear();loadedSource.clear();});
    connect(load,&QPushButton::clicked,&dialog,[&]{base->clear();head->clear();loadedSource=source->text().trimmed();loadingHead=false;requests_.branches(requests_.repository());});
    connect(&requests_,&PullRequests::branchesLoaded,&dialog,[&](const QString &repository,const QJsonArray &rows){auto fill=[&](QComboBox *combo){for(const auto &v:rows)combo->addItem(v.toObject().value("name").toString());};if(repository==requests_.repository()&&!loadingHead){fill(base);if(loadedSource==requests_.repository()){fill(head);if(head->count()>1)head->setCurrentIndex(1);}else loadingHead=true;}else if(repository==loadedSource)fill(head);});
    connect(&requests_,&PullRequests::busyChanged,&dialog,[&](bool busy){for(auto *w:QList<QWidget*>{source,load,base,head,title,body,draft,buttons,previewButton,templatePath,templateButton})w->setEnabled(!busy);});
    connect(&requests_,&PullRequests::completed,&dialog,[&](bool ok){if(creating){creating=false;if(ok)dialog.accept();return;}if(ok&&loadingHead&&head->count()==0){loadingHead=false;requests_.branches(loadedSource);}});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(base->currentText().isEmpty()||head->currentText().isEmpty()||loadedSource!=source->text().trimmed()||title->text().trimmed().isEmpty()){notice->setText(tr("브랜치를 불러온 뒤 base·head와 제목을 입력하세요."));return;}if(!confirm(&dialog,tr("%1 계정으로 %2에 PR을 생성합니다.\n%3:%4 → %5\n%6\n\n%7\n\n%8").arg(login_,requests_.repository(),loadedSource,head->currentText(),base->currentText(),title->text(),body->toPlainText(),draft->isChecked()?tr("초안"):tr("리뷰 가능한 PR"))))return;creating=true;requests_.create(loadedSource,head->currentText(),base->currentText(),title->text(),body->toPlainText(),draft->isChecked());if(!requests_.busy())creating=false;});
    QTimer::singleShot(0,load,&QPushButton::click);if(dialog.exec()==QDialog::Accepted&&detail_.value("number").toInt()>0)requests_.detail(detail_.value("number").toInt());
}
