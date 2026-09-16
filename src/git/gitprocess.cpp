#include "gitclient.h"
#include "settings/diagnostics.h"
#include "history/rebaseeditor.h"
#include <QDir>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QDateTime>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <signal.h>
#endif

namespace {
QString commandName(const QStringList &arguments) {
    for (int i=0; i<arguments.size(); ++i) {
        if (arguments[i]=="-c" || arguments[i]=="-C") { ++i; continue; }
        if (!arguments[i].startsWith('-')) return arguments[i];
    }
    return arguments.contains("--version") ? "version" : QString();
}
bool readOnly(const QString &command, const QStringList &arguments) {
    if(command=="worktree")return arguments.value(1)=="list"||(arguments.value(1)=="prune"&&arguments.contains("--dry-run"));
    if(command=="bisect")return arguments.value(1)=="log";
    if(command=="count-objects")return true;
    if(command=="fsck")return !arguments.contains("--lost-found");
    if(command=="bundle")return arguments.value(1)=="verify"||arguments.value(1)=="list-heads";
    if(command=="apply")return arguments.contains("--check");
    if(command=="submodule")return arguments.value(1)=="status"||arguments.value(1)=="summary";
    if(command=="lfs")return QStringList{"version","status","env","ls-files","locks"}.contains(arguments.value(1))||(arguments.value(1)=="prune"&&arguments.contains("--dry-run"))||(arguments.value(1)=="migrate"&&arguments.value(2)=="info"&&arguments.contains("--skip-fetch"));
    if(command=="notes")return arguments.value(2)=="list"||arguments.value(2)=="show";
    if(command=="replace")return arguments.contains("-l");
    if(command=="sparse-checkout")return arguments.value(1)=="list";
    if(command=="rerere")return QStringList{"status","diff","remaining"}.contains(arguments.value(1));
    if(command=="commit-graph")return arguments.value(1)=="verify";
    if(command=="verify-commit"||command=="verify-tag")return true;
    if(command=="reflog")return arguments.contains("show");
    if (command=="ls-files"||command=="cat-file"||command=="merge-base") return true;
    if (command=="stash") return arguments.value(1)=="list" || arguments.value(1)=="show";
    if (QStringList{"version","status","diff","diff-tree","show","log","rev-parse","rev-list","for-each-ref","check-ref-format","var","blame","ls-remote"}.contains(command)) return true;
    if (command=="symbolic-ref") return arguments.contains("--quiet") || arguments.contains("--short");
    if (command=="config") return arguments.contains("--list") || arguments.contains("--get") || arguments.contains("--get-all") || arguments.contains("--get-regexp");
    if (command=="remote") return arguments.size()==1 || arguments.contains("get-url") || arguments.contains("-v");
    if (command=="branch") return arguments.size()==1 || arguments.contains("--list") || arguments.join(' ').contains("--format=");
    return false;
}
QString redact(QString text) {
    return Diagnostics::redact(text);
}
struct Execution {
    QByteArray output, errors;
    qint64 outputBytes=0,errorBytes=0;
    QElapsedTimer elapsed, lastOutput;
    bool finished=false, overflow=false, deadlineExceeded=false, authHint=false;
#ifdef Q_OS_WIN
    HANDLE job=nullptr;
    ~Execution() { if(job) CloseHandle(job); }
#endif
};
}
void GitClient::setTimeoutMilliseconds(int milliseconds) { if (!isBusy()) timeoutMs_=qBound(10,milliseconds,7200000); }
void GitClient::cancelActive() { if (canCancel()) stopActive(false); }
void GitClient::stopActive(bool timeout) {
    if (!activeProcess_ || !cancellable_ || stopping_) return;
    stopping_=true;
    stopReason_=timeout ? tr("시간 제한으로 작업을 중단했습니다.") : tr("사용자가 작업 중단을 요청했습니다.");
    activity_=tr("하위 프로세스까지 중단 중…"); emit activityChanged();
    if (terminateTree_) terminateTree_();
}

void GitClient::executeReviewed(const QStringList &arguments, CommandCallback callback, const QByteArray &input) {
    run(arguments,callback,input,8*1024*1024,true,true,true);
}
void GitClient::run(QStringList arguments, CommandCallback callback, QByteArray input, qint64 limit, bool requiresRepository, bool checked, bool conservative) {
    if (isBusy()) { callback(false, {}, tr("다른 Git 작업이 진행 중입니다.")); return; }
    if (requiresRepository && repositoryPath_.isEmpty()) { callback(false, {}, tr("먼저 저장소를 선택하세요.")); return; }
    const auto command=commandName(arguments);
    const bool reading=!conservative&&readOnly(command,arguments);
    const bool guarded=(!reading && QStringList{"switch","checkout","commit","reset","merge","rebase","cherry-pick","revert","pull","push","stash","am","clean","rm","mv"}.contains(command)) || (command=="branch"&&!reading) || (command=="restore"&&arguments.contains("--worktree"));
    if (guarded && requiresRepository && !checked) {
        loadOperationState([this,arguments,callback,input,limit,requiresRepository,command](bool ok,const QString &error) {
            if (!ok) { callback(false,{},error); return; }
            const bool recovery=arguments.contains("--abort") || arguments.contains("--continue");
            const bool allowedRecovery=recovery && state_.operation==command && state_.locks.isEmpty() &&
                (arguments.contains("--abort") || state_.conflicts.isEmpty());
            if (!allowedRecovery && (!state_.operation.isEmpty() || !state_.conflicts.isEmpty() || !state_.locks.isEmpty())) {
                diagnostic_=tr("다른 Git 작업, 충돌 또는 잠금이 남아 있어 %1을 실행하지 않았습니다. 작업 상태 · 복구 탭을 확인하세요.").arg(command);
                emit diagnosticChanged(); callback(false,{},diagnostic_); return;
            }
            run(arguments,callback,input,limit,requiresRepository,true);
        });
        return;
    }

    auto *process=new QProcess(this); activeProcess_=process;
    auto execution=std::make_shared<Execution>(); execution->elapsed.start(); execution->lastOutput.start();
    process->setWorkingDirectory(repositoryPath_.isEmpty()?QDir::tempPath():repositoryPath_);
    process->setProgram("git"); process->setArguments(arguments);
    auto environment=QProcessEnvironment::systemEnvironment(); environment.insert("GIT_TERMINAL_PROMPT","0");
    if(conservative){environment.insert("GIT_EDITOR","false");environment.insert("GIT_SEQUENCE_EDITOR","false");}
    if(arguments.contains("--continue")) environment.insert("GIT_EDITOR","true");
    if(command=="rebase"&&(arguments.contains("--interactive")||arguments.contains("--continue")))configureRebaseEditor(environment,state_.gitDirectory,arguments.contains("--interactive"));
    process->setProcessEnvironment(environment);
    cancellable_=!conservative&&(reading || command=="fetch" || command=="clone");
    stopping_=false; stopReason_.clear();
    const bool potentiallyChanged=!reading;
    const auto timeout=timeoutMs_;

#ifdef Q_OS_WIN
    // Suspend before exec, attach to our job, then resume: children cannot escape between launch and assignment.
    execution->job=CreateJobObjectW(nullptr,nullptr);
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_SUSPENDED | CREATE_NO_WINDOW; });
    connect(process,&QProcess::started,this,[this,process,execution] {
        HANDLE native=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE|PROCESS_QUERY_INFORMATION,FALSE,DWORD(process->processId()));
        const bool assigned=native && execution->job && AssignProcessToJobObject(execution->job,native);
        bool resumed=false;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot!=INVALID_HANDLE_VALUE) {
            THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
            if(Thread32First(snapshot,&entry)) do {
                if(entry.th32OwnerProcessID==DWORD(process->processId())) {
                    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);
                    if(thread){resumed=ResumeThread(thread)!=DWORD(-1)||resumed;CloseHandle(thread);}
                }
            } while(Thread32Next(snapshot,&entry));
            CloseHandle(snapshot);
        }
        if(native)CloseHandle(native);
        if(!resumed){process->kill();return;}
        if(!assigned)cancellable_=false;
        terminateTree_=[this,process,execution] {
            if(execution->job) {
                if(!TerminateJobObject(execution->job,1)){stopping_=false;cancellable_=false;activity_=tr("프로세스 중단에 실패했습니다. 종료를 기다리며 상태를 보호합니다.");emit activityChanged();}
            } else process->kill();
        };
        emit activityChanged();
    });
#else
    process->setChildProcessModifier([] { ::setsid(); });
    connect(process,&QProcess::started,this,[this,process] {
        const auto pid=pid_t(process->processId());
        terminateTree_=[pid] { ::kill(-pid,SIGKILL); };
        emit activityChanged();
    });
#endif
    auto display=arguments;
    if(conservative)display={command,"<reviewed arguments>"};
    if(command=="commit") display={"commit","<message>"};
    if(!conservative&&command=="stash"&&arguments.contains("-m")) display[arguments.indexOf("-m")+1]="<message>";
    if(command=="config"&&arguments.contains("--replace-all"))display.last()="<value>";
    if(command=="remote"&&(arguments.value(1)=="add"||arguments.value(1)=="set-url"))display.last()="<URL>";
    if(command=="clone")display[display.size()-2]="<URL>";
    const auto safeCommand=redact("git "+display.join(' '));
    activity_=tr("%1 · 시작 중").arg(command);
    emit busyChanged(true);emit commandStarted(safeCommand);emit activityChanged();
    const qint64 outputLimit=limit>0?limit:32*1024*1024;
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,execution,outputLimit,reading] {
        const auto chunk=process->readAllStandardOutput();execution->outputBytes+=chunk.size();execution->lastOutput.restart();
        if(execution->output.size()+chunk.size()>outputLimit){execution->overflow=true;if(reading)stopActive(false);return;}
        execution->output.append(chunk);
    });
    connect(process,&QProcess::readyReadStandardError,this,[this,process,execution,command] {
        const auto chunk=process->readAllStandardError();execution->errorBytes+=chunk.size();execution->lastOutput.restart();execution->errors.append(chunk);
        if(execution->errors.size()>256*1024)execution->errors=execution->errors.right(256*1024);
        const auto text=redact(QString::fromLocal8Bit(chunk).replace('\r','\n'));
        execution->authHint=QRegularExpression("authentication|password|passphrase|credential|username",QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
        activity_=tr("%1 · %2").arg(command,text.trimmed().section('\n',-1).left(220));emit activityChanged();
        if(command=="clone")emit cloneProgress(text);
    });
    auto *timer=new QTimer(process);timer->setInterval(250);
    connect(timer,&QTimer::timeout,this,[this,execution,command,timeout] {
        emit outputProgress(execution->outputBytes,execution->errorBytes);
        if(stopping_)return;
        const auto seconds=execution->elapsed.elapsed()/1000;
        if(execution->elapsed.elapsed()>=timeout) {
            execution->deadlineExceeded=true;
            if(canCancel()){stopActive(true);return;}
            activity_=tr("%1 · %2초 · 시간 제한 초과. 쓰기 작업/프로세스 보호를 위해 종료를 기다립니다.").arg(command).arg(seconds);
        } else if(execution->lastOutput.elapsed()>15000) {
            activity_=execution->authHint?tr("%1 · %2초 · 인증 관련 응답 대기: 인증 창을 확인하세요.").arg(command).arg(seconds)
                :tr("%1 · %2초 · 새 출력 대기: 네트워크·인증 창·Git hook을 확인하세요.").arg(command).arg(seconds);
        } else if(!execution->errors.isEmpty()) {
            activity_=tr("%1 · %2초 · %3").arg(command).arg(seconds).arg(redact(QString::fromLocal8Bit(execution->errors).replace('\r','\n')).trimmed().section('\n',-1).left(150));
        } else activity_=tr("%1 · %2초 · 실행 중").arg(command).arg(seconds);
        emit activityChanged();
    });timer->start();
    const bool diffExitOne=command=="diff"&&arguments.contains("--no-index");
    auto complete=[this,process,execution,timer,callback,potentiallyChanged,safeCommand,outputLimit,diffExitOne,command](int exitCode,bool normal,bool startFailed) {
        if(execution->finished)return;execution->finished=true;timer->stop();
        if(command=="rebase")finishRebaseEditor(state_.gitDirectory);
        const auto errorTail=process->readAllStandardError();execution->errorBytes+=errorTail.size();execution->errors.append(errorTail);if(execution->errors.size()>256*1024)execution->errors=execution->errors.right(256*1024);
        const auto tail=process->readAllStandardOutput();
        execution->outputBytes+=tail.size();emit outputProgress(execution->outputBytes,execution->errorBytes);
        if(execution->output.size()+tail.size()>outputLimit)execution->overflow=true;else execution->output.append(tail);
        QString error=redact(QString::fromLocal8Bit(execution->errors).trimmed());
        bool ok=normal&&(exitCode==0||(diffExitOne&&exitCode==1))&&stopReason_.isEmpty()&&!execution->overflow&&!startFailed;
        if(startFailed)error=tr("git 실행 파일을 실행할 수 없습니다. Git 설치와 PATH를 확인하세요.");
        else if(execution->overflow)error=tr("출력 크기 제한을 초과했습니다. 조회 범위를 좁혀주세요.");
        else if(!stopReason_.isEmpty())error=stopReason_+tr("\n중단은 되돌리기가 아닙니다. 작업 상태와 잠금을 다시 확인하세요.");
        else if(!ok&&error.isEmpty())error=tr("Git 명령이 종료 코드 %1로 끝났습니다.").arg(exitCode);
        if((!ok&&potentiallyChanged)||!stopReason_.isEmpty()||startFailed||execution->deadlineExceeded) {
            diagnostic_=tr("%1\n%2\n종료 코드: %3 · 경과: %4초\n%5").arg(QDateTime::currentDateTime().toString(Qt::ISODate),safeCommand).arg(exitCode).arg(execution->elapsed.elapsed()/1000)
                .arg(ok?tr("시간 제한을 넘긴 뒤 명령이 완료되었습니다."):error);
            if(potentiallyChanged)diagnostic_+=tr("\n일부 데이터가 바뀌었을 수 있습니다. 원격 작업은 다시 Fetch하여 확인하세요.");
            emit diagnosticChanged();
        }
        // Do not restart a cancelled read (or repeatedly retry an oversized history query).
        const bool refresh=!ok&&potentiallyChanged;
        activeProcess_=nullptr;terminateTree_={};cancellable_=false;stopping_=false;activity_=ok?tr("준비됨"):tr("작업 종료 · 결과 확인 필요");
        process->deleteLater();emit busyChanged(false);emit activityChanged();
        emit commandFinished(command,ok,exitCode,execution->elapsed.elapsed(),error,potentiallyChanged);
        if(refresh)emit recoveryRequired();
        callback(ok,execution->overflow?QByteArray():execution->output,error);
    };
    connect(process,&QProcess::finished,this,[complete](int code,QProcess::ExitStatus status){complete(code,status==QProcess::NormalExit,false);});
    connect(process,&QProcess::errorOccurred,this,[complete](QProcess::ProcessError error){if(error==QProcess::FailedToStart)complete(-1,false,true);});
    process->start();if(!input.isEmpty())process->write(input);process->closeWriteChannel();
}
