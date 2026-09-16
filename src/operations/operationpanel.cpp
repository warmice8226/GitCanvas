#include "operationpanel.h"
#include "git/gitclient.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QMessageBox>
#include <QLocale>
#include "settings/diagnostics.h"

ProcessControls::ProcessControls(GitClient *git,QWidget *parent):QWidget(parent) {
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto *activity=new QLabel;activity->setTextFormat(Qt::PlainText);activity->setWordWrap(true);activity->setObjectName("gitActivity");layout->addWidget(activity);
    auto *bytes=new QLabel;bytes->setObjectName("gitOutputProgress");layout->addWidget(bytes);
    connect(git,&GitClient::outputProgress,bytes,[bytes](qint64 output,qint64 errors){bytes->setText(tr("수신한 출력 %1 · 오류 출력 %2").arg(QLocale().formattedDataSize(output),QLocale().formattedDataSize(errors)));});
    connect(git,&GitClient::commandStarted,bytes,[bytes]{bytes->clear();});
    auto *row=new QHBoxLayout;auto *progress=new QProgressBar;progress->setRange(0,0);progress->setMaximumHeight(8);progress->setTextVisible(false);row->addWidget(progress,1);
    auto *stop=new QPushButton(tr("현재 작업 중단"));stop->setObjectName("cancelGitOperation");row->addWidget(stop);layout->addLayout(row);
    stop->setToolTip(tr("조회·Fetch·Clone을 하위 프로세스까지 중단합니다. 되돌리기 기능은 아니므로 이후 상태를 다시 확인합니다. 커밋·Push·병합 등 쓰기 작업은 중단하지 않습니다."));
    connect(stop,&QPushButton::clicked,git,&GitClient::cancelActive);
    auto update=[=] {activity->setText(git->activityText());progress->setVisible(git->isBusy());stop->setEnabled(git->canCancel());};
    connect(git,&GitClient::activityChanged,this,update);connect(git,&GitClient::busyChanged,this,update);update();
}

OperationPanel::OperationPanel(GitClient *git,QWidget *parent):QWidget(parent),git_(git) {
    setObjectName("operationPanel");auto *layout=new QVBoxLayout(this);layout->addWidget(new ProcessControls(git));
    auto *timeoutRow=new QHBoxLayout;timeoutRow->addWidget(new QLabel(tr("명령별 시간 제한 (초)")));
    auto *timeout=new QSpinBox;timeout->setObjectName("gitTimeoutSeconds");timeout->setRange(1,7200);timeout->setValue(qBound(1,QSettings().value("operations/timeoutSeconds",120).toInt(),7200));timeoutRow->addWidget(timeout);timeoutRow->addStretch();layout->addLayout(timeoutRow);
    git->setTimeoutMilliseconds(timeout->value()*1000);
    connect(timeout,&QSpinBox::valueChanged,this,[git](int seconds){git->setTimeoutMilliseconds(seconds*1000);QSettings().setValue("operations/timeoutSeconds",seconds);});
    connect(git,&GitClient::busyChanged,timeout,[timeout](bool busy){timeout->setEnabled(!busy);});
    auto *policy=new QLabel(tr("조회·Fetch·Clone은 시간 제한에서 중단합니다. 쓰기 작업은 시간 초과를 알리고 종료를 기다립니다. 출력이 없다는 것만으로 인증 대기인지 단정하지 않습니다."));policy->setWordWrap(true);layout->addWidget(policy);
    state_=new QLabel;state_->setObjectName("pendingOperationState");state_->setWordWrap(true);state_->setTextFormat(Qt::PlainText);layout->addWidget(state_);
    auto *lists=new QHBoxLayout;
    auto *left=new QVBoxLayout;left->addWidget(new QLabel(tr("해결할 충돌 파일")));conflicts_=new QListWidget;conflicts_->setObjectName("conflictFiles");left->addWidget(conflicts_);lists->addLayout(left,1);
    auto *right=new QVBoxLayout;right->addWidget(new QLabel(tr("감지한 Git 잠금 (자동 삭제하지 않음)")));locks_=new QListWidget;locks_->setObjectName("gitLocks");right->addWidget(locks_);lists->addLayout(right,1);layout->addLayout(lists,1);
    auto *help=new QLabel(tr("충돌 파일은 외부 편집기로 수정하고 변경 사항 탭에서 Stage하세요. 계속은 준비된 결과로 작업을 이어갑니다. Abort는 해당 Git 작업을 취소하며 해결 중인 변경을 버릴 수 있습니다. 잠금이 있으면 다른 Git 프로세스가 끝났는지 확인하세요."));help->setWordWrap(true);layout->addWidget(help);
    auto *buttons=new QHBoxLayout;refresh_=new QPushButton(tr("상태 다시 확인"));continue_=new QPushButton(tr("계속 · Continue"));abort_=new QPushButton(tr("진행 작업 취소 · Abort"));
    continue_->setObjectName("continueGitOperation");abort_->setObjectName("abortGitOperation");
    buttons->addWidget(refresh_);buttons->addWidget(continue_);buttons->addWidget(abort_);layout->addLayout(buttons);
    connect(refresh_,&QPushButton::clicked,this,&OperationPanel::refreshRequested);connect(continue_,&QPushButton::clicked,this,[this]{recover(false);});connect(abort_,&QPushButton::clicked,this,[this]{recover(true);});
    auto *recoveryHint=new QLabel;recoveryHint->setWordWrap(true);recoveryHint->setObjectName("recoveryGuidance");layout->addWidget(recoveryHint);
    connect(git,&GitClient::commandFinished,this,[recoveryHint](const QString &,bool ok,int exitCode,qint64,const QString &error,bool writes){
        if(ok)return;
        const auto code=Diagnostics::classify(error,exitCode,writes).value("code").toString();
        QString hint=tr("복구 안내 · 작업 상태와 원문 오류를 확인한 뒤 다시 실행하세요. 원격 쓰기는 Fetch로 결과부터 확인하세요.");
        if(code=="authentication_or_permission")hint=tr("복구 안내 · 로그인 계정, 저장소 접근 권한과 조직 SSO 승인을 확인하세요.");
        else if(code=="possible_conflict")hint=tr("복구 안내 · 충돌 파일을 해결하고 Stage한 뒤 Continue하세요. 취소하려면 Abort의 영향을 먼저 확인하세요.");
        else if(code=="possible_lock")hint=tr("복구 안내 · 다른 Git 프로세스가 끝났는지 먼저 확인하세요. 잠금 파일은 자동 삭제하지 않습니다.");
        else if(code=="process_unavailable")hint=tr("복구 안내 · Git 환경 설정에서 실행 파일과 PATH를 확인하세요.");
        recoveryHint->setText(hint);
    });
    layout->addWidget(new QLabel(tr("Git 진단 · 원문 오류와 실행 정보 (민감정보 일부 가림)")));
    diagnostic_=new QPlainTextEdit;diagnostic_->setObjectName("operationDiagnostic");diagnostic_->setReadOnly(true);diagnostic_->setMaximumHeight(140);layout->addWidget(diagnostic_);
    connect(git,&GitClient::operationStateChanged,this,&OperationPanel::update);connect(git,&GitClient::busyChanged,this,&OperationPanel::update);
    connect(git,&GitClient::diagnosticChanged,this,[this]{diagnostic_->setPlainText(git_->lastDiagnostic());});update();
}
void OperationPanel::update() {
    const auto &state=git_->operationState();const bool valid=state.known && state.repository==git_->repositoryPath();
    state_->setText(!valid?tr("저장소 작업 상태 확인이 필요합니다."):state.operation.isEmpty()?tr("진행 중인 병합/이력 작업 없음 · 충돌 %1개").arg(state.conflicts.size()):tr("진행 중: %1 · 충돌 %2개\n%3").arg(state.operation).arg(state.conflicts.size()).arg(state.repository));
    conflicts_->clear();locks_->clear();if(valid){conflicts_->addItems(state.conflicts);locks_->addItems(state.locks);}
    const bool supported=QStringList{"merge","rebase","cherry-pick","revert","am"}.contains(state.operation);
    const bool ready=valid&&!git_->isBusy()&&supported&&state.locks.isEmpty();
    continue_->setEnabled(ready&&state.conflicts.isEmpty());abort_->setEnabled(ready);refresh_->setEnabled(!git_->isBusy()&&!git_->repositoryPath().isEmpty());
}
void OperationPanel::recover(bool abort) {
    const auto state=git_->operationState();
    QMessageBox confirm(QMessageBox::Warning,tr("진행 작업 확인"),abort?tr("%1 작업을 Abort합니다. 충돌 해결 중인 수정이 사라질 수 있습니다. 실행 전 HEAD 참조를 남기지만 미커밋 파일 전체를 백업하지는 않습니다.").arg(state.operation):tr("%1 작업을 계속합니다. 충돌을 해결하고 Stage한 내용을 사용하며 커밋 메시지 편집 없이 기존 메시지로 진행합니다.").arg(state.operation),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);
    confirm.setDefaultButton(QMessageBox::No);if(confirm.exec()!=QMessageBox::Yes)return;
    git_->recoverOperation(state.fingerprint,abort,[this](bool ok,const QByteArray &,const QString &error){if(!ok)diagnostic_->setPlainText(error);emit refreshRequested();});
}
