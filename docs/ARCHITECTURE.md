# 프로그램 구조와 Git 기능 추가 방법

`GitCanvasCore` OBJECT 라이브러리에서 앱과 테스트가 구현·UI 리소스를 공유합니다.
저장소 변경은 `MainWindow::repositoryChanged()`로 모으며 Git/PR 작업 중에는
새로고침을 예약합니다. Git 매뉴얼 번역은 별도 라이선스의 JSON 데이터로 읽습니다.
배포 구조는 [무설치 배포](PORTABLE_DISTRIBUTION.md)를 따릅니다.

## 기본 흐름

11번 구현에서 `settings/AppLanguage`(내장 한/영 번역), `settings/Diagnostics`(출력 제외 구조화 진단·보관), `history/HistoryModel`(가시 영역 중심 테이블)을 추가했습니다. 언어는 다음 실행부터 적용하고, History 캐시는 매번 읽은 Git 출력의 해시로 구분합니다. 범위와 배포 검증 경계는 [언어·진단·성능·배포](QUALITY_AND_DEPLOYMENT.md)를 참고하세요.

```text
사용자가 Qt 버튼 클릭
        ↓
MainWindow가 입력값과 선택 항목 확인
        ↓
GitClient의 의미 있는 메서드 호출
        ↓
QProcess가 시스템 git을 비동기로 실행
        ↓
결과를 파싱해 화면 갱신 또는 오류 표시
```

버튼에서 전체 셸 명령 문자열을 직접 만들지 않습니다. UI와 Git 실행을 분리하면 화면을 바꾸거나 Git 처리 방식을 교체하기 쉽습니다.

사용자에게는 실행될 명령을 읽을 수 있는 문자열로 보여주지만, 내부 실행 데이터는 프로그램과 인자 배열로 유지합니다.

```text
옵션 UI
  ↓
GitCommandRequest(구조화된 값)
  ├─ CommandRenderer → 사용자용 명령 미리보기
  ├─ CommandValidator → 옵션 충돌·버전·위험도 검사
  └─ GitClient → QProcess(program, arguments)
```

명령 미리보기 문자열을 다시 파싱해서 실행하지 않습니다. 미리보기와 실제 인자는 동일한 `GitCommandRequest`에서 각각 생성합니다.

## 실행 대상 추상화

12번의 현재 구현은 `ssh/SshExecutor`와 `SshWorkspace`, `remote_helper.py`로 구성합니다. 원격 파일 경로가 로컬 파일 API로 들어가는 것을 막기 위해 원격 작업창을 분리했습니다. helper가 버전 JSON 요청을 검증하고 원격 Git argv로 실행합니다. 아래 공통 ExecutionContext 구조는 고급 Git 화면까지 통합할 때의 목표 구조이며, 현재 모든 로컬 기능이 SSH executor로 전환되는 것은 아닙니다. [현재 범위](SSH_WORKSPACE.md)를 참고하세요.

Git 명령은 로컬과 SSH 원격에서 동일한 요청 모델을 사용합니다.

```text
ExecutionContext
├─ LocalExecutionContext
└─ SshExecutionContext
   ├─ sshProfileId
   ├─ remoteOsIdentity
   ├─ repositoryRoot
   └─ remoteGitCapabilities

GitCommandRequest
    ↓
IGitExecutor
├─ LocalGitExecutor
└─ SshGitExecutor
```

UI는 executor를 직접 알지 않고 활성 `ExecutionContext`에 command를 전달합니다. 저장소와 cache의 키에는 실행 대상 ID가 포함되어야 하며 로컬과 원격의 같은 경로를 동일 저장소로 취급하지 않습니다.

원격 쓰기 명령의 최종 구조는 원격 셸 문자열 조립보다 versioned protocol을 사용하는 `gitcanvas-remote-helper` 방식이 권장됩니다. helper 버전과 기능을 handshake에서 확인하고 호환되지 않으면 읽기 전용 fallback 또는 명확한 설치 안내를 제공합니다.

## 주요 파일

- `src/main.cpp`: Qt 애플리케이션을 시작합니다.
- `src/mainwindow.h/.cpp`: 창, 버튼과 사용자 상호작용을 담당합니다.
- `src/git/gitclient.h/.cpp`: Git CLI를 실행하고 결과를 반환합니다.
- `src/git/gitstatusentry.h`: Git 상태 한 항목을 표현합니다.

## 버튼 추가 예시

새 기능은 먼저 `GitClient`에 메서드를 만듭니다.

```cpp
void GitClient::push(CommandCallback callback)
{
    run({"push"}, std::move(callback));
}
```

그다음 Qt 버튼에 연결합니다.

```cpp
connect(pushButton, &QPushButton::clicked, this, [this] {
    git_.push([this](bool ok, const QByteArray &, const QString &error) {
        if (!ok) {
            showError(error);
        }
    });
});
```

`QProcess`에는 `"git add file"` 같은 한 문자열이 아니라 프로그램과 인자를 분리해 전달합니다. 공백이 있는 경로를 안전하게 처리하고 셸 명령 삽입 위험을 줄일 수 있습니다.

## 성능 원칙

- Git 명령은 UI 스레드에서 기다리지 않습니다.
- 대규모 출력은 최종적으로 스트리밍 또는 점진적으로 파싱합니다.
- 파일과 커밋 목록은 화면에 보이는 항목 중심으로 표시합니다.
- diff와 그래프 계산 결과를 캐시합니다.
- 긴 작업에는 취소 기능을 제공합니다.
- 최적화 전 실제 저장소로 프로파일링합니다.

## macOS 실행과 배포

Qt 화면과 Git 실행 계층을 공유하며, `src/platform/platformenvironment.cpp`가 앱 시작 시 Finder의 PATH에 Homebrew 실행 경로를 보완합니다. 사용자 지정 절대 경로의 순서를 보존하고 현재 디렉터리·상대 경로는 도구 탐색에서 제외합니다. GitHub CLI는 Windows의 `gh.exe`, macOS의 `gh`를 앱 실행 파일 옆에서 찾습니다.

CMake는 macOS에서 Info.plist를 포함한 `.app`을 생성합니다. `scripts/package-macos.sh`는 네이티브 빌드·테스트·Qt 배포·번들 검사·선택적 서명/공증 후 아키텍처별 ZIP를 생성합니다. 실제 Mac 검증은 대기 중이며 세부 범위는 [macOS 안내](MACOS.md)에 기록합니다.

## Windows 프로토타입의 화면 구조

2026-09-15 변경: 기본창과 모든 플로팅 대화상자에 제목표시줄을 표시합니다. 제목표시줄을 끌어 이동하고 닫기 버튼으로 닫을 수 있으며, 작업 중 닫기 제한은 기존 로직을 유지합니다. `src/ui/windowchrome.cpp`는 Windows 11 이상에서 [DWM API](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)로 각 창의 배경색과 글자색을 기본 제목표시줄에 반영합니다. Windows 10에서는 같은 색상의 사용자 정의 제목표시줄을 표시하며, OS의 창 이동·가장자리 크기 변경 기능과 연결합니다. 기본창은 최소화·최대화 버튼과 제목표시줄 더블 클릭을 지원합니다. macOS/Linux에서는 운영체제의 기본 제목표시줄을 사용합니다. 아래 이전 구현 설명의 frameless 표기는 이 변경으로 대체됩니다.

`MainWindow`는 왼쪽 로컬 저장소·브랜치 영역과 변경 사항/커밋 기록/작업 로그 탭으로 구성됩니다. 변경 사항 탭은 파일 목록, 읽기 전용 diff, 커밋 입력 영역으로 나뉩니다. diff의 추가/삭제 줄은 `QSyntaxHighlighter`로 구분합니다.

상태 새로고침은 status → 브랜치 정보 → HEAD 확인 → log 순으로 비동기 실행합니다. 단일 `GitClient`에 활성 프로세스 하나만 두고 실행 중에는 저장소 변경과 Git 작업 버튼을 비활성화합니다. 커밋 메시지는 작업 로그에 기록하지 않습니다. 첫 커밋 이전 unstage는 `git rm --cached`로 처리하고 작업 파일은 보존합니다.

현재 구현은 로컬 executor에 해당하는 초기 형태입니다. 구조화된 `GitCommandRequest`, 원격 executor와 고급 옵션 UI는 아직 구현하지 않았습니다. Git 출력은 메모리에 수집하므로 스트리밍과 대규모 저장소 최적화도 후속 작업입니다.

`tests/prototype_test.cpp`는 임시 Git 저장소와 Qt GUI를 사용한 통합 검증을 수행합니다. `scripts/package-windows.ps1`은 Windows 빌드와 테스트 후 Qt 플러그인, MinGW 및 전이 DLL 의존성을 수집해 무설치 ZIP을 생성합니다.

파일 목록의 체크 상태는 Stage/Unstage 대상이며, 현재 선택 행은 diff 미리보기 대상입니다. 각 목록 헤더의 체크박스는 전체 선택과 부분 선택 상태를 표시합니다. 커밋 입력은 한 줄 제목(필수)과 여러 줄 상세 설명(선택)으로 분리하고, 실행 시 제목과 본문 사이에 빈 줄을 넣습니다. 커밋은 체크 상태와 관계없이 staging 영역 전체를 기록합니다.

## 커밋 기록 GUI

`src/history/historywidget.h/.cpp`의 `HistoryWidget`이 커밋 표, 부모 연결 그래프와 선택 커밋 상세 정보를 담당합니다. `GitClient`의 비동기 실행을 공유하며, 조회 중에는 커밋 선택을 비활성화해 오래된 조회 결과가 다른 선택에 표시되지 않도록 합니다.

`git log --topo-order --max-count=<표시 한도+1> -z`로 SHA, 부모 SHA 목록, 작성자, 이메일, 작성 시각, 참조, 제목, 본문, 커밋 시각의 9개 NUL 구분 필드를 읽습니다. 한 레코드를 더 조회해 추가 로딩 가능 여부를 판별합니다. 참조를 지정하지 않으면 `--all`을 사용하며 메시지·작성자·경로·날짜 필터는 Git 조회에 적용합니다. 위상 순서를 유지하고 표의 정렬은 제공하지 않습니다.

`buildGraph()`는 아직 표시하지 않은 부모 커밋을 레인에 배치하고, 각 행의 통과선·부모 연결선·노드 위치를 계산합니다. `GraphDelegate`는 이를 QPainter로 그립니다. 확대/축소로 행 높이를 조절하며 표와 그래프가 함께 스크롤됩니다. 로딩 경계에서는 부모 연결선이 아래로 이어집니다. 일부 커밋을 생략하는 검색에서는 그래프 열을 숨깁니다.

커밋 선택 시 전체 SHA와 메시지 및 HEAD와의 관계를 표시하고, `git diff-tree`의 NUL 구분 경로 출력으로 변경 파일을 표시합니다. 첫 커밋은 빈 트리와 비교하며 병합 커밋은 부모를 선택할 수 있습니다. 비교 기준 SHA를 지정하면 기준→선택 커밋의 diff를 조회합니다. 파일 클릭은 공유 `DiffWidget`으로 연결됩니다. 같은 SHA가 목록에 있으면 새로고침 후 선택을 유지합니다.

`src/diff/diffwidget.*`은 통합/나란히 표시와 인코딩 해석, raw hunk 분리를 담당합니다. 기존 추적 파일의 부분 Stage/Unstage는 diff 스냅샷 재검사 → `git apply --cached --check` → 실제 적용 순입니다. 화면 인코딩은 패치 바이트를 변환하지 않습니다. `GitClient::inspectLimited`는 stdout 수신 시 8 MiB 초과를 감지하여 프로세스를 중단합니다. 일반 명령 출력·stderr까지 일괄 제한하는 구조는 아닙니다.

History 우클릭은 이동·브랜치/태그 생성과 기본 이력 변경을 제공합니다. 이력 변경은 미커밋 변경·detached HEAD·진행 중 작업을 검사하고 `refs/gitcanvas/backups/<UUID>`로 HEAD를 보존합니다. 실패 시 Abort를 선택할 수 있으며 자체 충돌 편집기는 아직 없습니다. 쓰기 이후 `repositoryChanged`로 MainWindow에 알리고 동기화를 다시 Fetch 상태로 돌린 뒤 화면을 갱신합니다. 자세한 사용자 동작과 한계는 [Diff·History 안내](DIFF_AND_HISTORY.md)를 참고합니다.

GUI 통합 테스트는 실제 분기·병합 저장소에서 노드 연결, 커밋 제목·참조, 병합 및 첫 커밋의 변경 파일, 한글 경로, 제어 문자를 포함한 본문 파싱을 검증합니다.

## 공통 실행·작업 상태·복구

`src/git/gitprocess.cpp`는 QProcess 실행, stdout/stderr 수신, 경과 시간·제한·중단 정책, 단일 완료 콜백과 결과 진단을 담당합니다. 조회·Fetch·Clone을 중단 가능 작업으로 분류합니다. 쓰기 명령은 시간 초과를 알리고 종료를 기다리며 강제 종료하지 않습니다. Windows에서는 CREATE_SUSPENDED로 생성한 프로세스를 Job Object에 연결한 후 재개하여 하위 프로세스 중단 범위를 고정합니다. 프로세스 묶음 연결 실패 시 중단을 비활성화합니다. Unix 구현은 별도 세션/프로세스 그룹을 사용하지만 Windows만 검증했습니다.

`src/git/operationstate.cpp`는 rev-parse로 실제 Git 디렉터리를 찾고 status의 unmerged 상태·작업 표시 파일·주요 잠금을 읽습니다. linked worktree의 공통 잠금 디렉터리도 확인합니다. 상태 fingerprint는 저장소·status 출력·작업 표시·Index·HEAD를 포함합니다. 전환·일반 커밋·이력 변경·Pull/Push 등의 실행은 이 조회를 먼저 거쳐 충돌·기존 작업·잠금이 있으면 차단합니다. Stage/Unstage는 충돌 해결에 필요하므로 계속 허용하며 Git의 실제 잠금 검사를 따릅니다.

`OperationPanel`은 작업 상태 탭, 충돌/잠금 목록, Continue/Abort와 최근 진단을 표시합니다. `ProcessControls`는 같은 GitClient의 실행 표시·중단 버튼을 메인 탭과 모달 설정창에 재사용합니다. Continue/Abort는 확인한 fingerprint를 재검사하고 HEAD 복구 참조를 남긴 뒤 실행합니다. 계속 실행은 기존 메시지를 사용합니다. 종료 후 작업 상태를 재조회하며 모든 미커밋 파일을 자동 백업하거나 임의 작업을 Undo하는 기능은 아닙니다.

변경 작업의 실패·중단은 recoveryRequired 신호로 MainWindow의 Fetch 권한을 초기화하고 Git이 유휴 상태가 되면 화면을 다시 읽습니다. 중단한 읽기 조회는 자동 재시작하지 않습니다. 회복할 수 없는 상태 조회 반복이나 대형 이력 출력 제한에 따른 재시도 루프를 방지합니다. 공통 시간 제한은 QSettings에 저장하며 다음 프로세스에 적용합니다. [작업별 정책과 검증 범위](OPERATIONS_AND_RECOVERY.md)를 참고합니다.

## Git 환경과 작성자 설정 상세

`src/settings/gitsettingsdialog.*`의 `GitSettingsDialog`는 Workspace에서 여는 frameless 설정창입니다. MainWindow와 동일한 GitClient를 사용하여 Git 명령의 직렬 실행 규칙을 유지합니다. `inspectEnvironment()`는 저장소가 없어도 실행 가능하며 이때 작업 디렉터리는 임시 폴더를 사용합니다. 기존 저장소 필수 API의 검증은 유지합니다.

`git --version`으로 실행 여부·버전을 확인하고 `QStandardPaths::findExecutable`로 PATH 실행 파일을 표시합니다. `git config --null --list --show-origin --show-scope`의 범위·출처·키/값을 읽어 user.name/user.email만 화면에 표시합니다. Git이 처리하는 include·우선순위를 유지하며, `git var GIT_AUTHOR_IDENT`와 `GIT_COMMITTER_IDENT`로 환경 변수와 기본 신원 추론까지 반영한 실제 값을 조회합니다.

쓰기 대상은 `--local` 또는 `--global`이며, `--replace-all user.name` → `--replace-all user.email` → 유효 값 재조회 순서입니다. 두 쓰기는 원자적 묶음이 아니므로 부분 저장 시 이를 안내합니다. 설정 값은 명령 로그에서 가립니다. 비동기 콜백이 끝나기 전에 대화상자가 닫히지 않도록 편집·닫기·Escape를 제한합니다.

MainWindow의 커밋 실행은 신원 사전 조회를 거칩니다. 실패 시 설정창을 열고 커밋 초안과 Index를 보존합니다. 설정창이 닫혔다고 커밋을 자동 실행하지 않습니다. 테스트는 GIT_CONFIG_GLOBAL을 임시 파일로 분리하고 GIT_CONFIG_NOSYSTEM을 지정하여 실제 사용자 설정 변경 없이 검증합니다. [사용자 안내](GIT_ENVIRONMENT_AND_IDENTITY.md)를 참고합니다.

## Workspace 저장소 목록 상세

Workspace의 저장소 추가는 유효한 Git 저장소 루트를 확인한 뒤 목록에 등록합니다. QSettings의 `workspace/repositories`에 경로·즐겨찾기·표시명·목록 순서를 저장합니다. 기존 `repositoryPath` 설정만 있는 경우 목록으로 옮기며, 마지막 저장소가 존재하면 시작할 때 다시 엽니다. 원본 폴더가 없어져도 목록에서 자동 삭제하지 않습니다. 우클릭으로 표시명 변경·경로 재검증 후 재지정·등록 정보 제거를 제공합니다. 폴더 자체를 이동하거나 삭제하지 않습니다.

즐겨찾기는 stable partition으로 상단에 모으고, ↑/↓ 버튼은 같은 그룹 안에서만 순서를 교체합니다. 저장소 클릭 또는 키보드 활성화는 기존 비동기 열기를 사용합니다. 저장소 루트의 canonical path를 사용하며 Windows에서는 경로의 대소문자를 무시해 중복 등록을 막습니다. 추가에 실패하면 기존 활성 저장소를 유지합니다.

브랜치 선택·전환·생성 컨트롤은 상단 저장소 이름 옆에 배치합니다. 커밋 제목·본문은 `workspace/commitDrafts`에 저장소 경로별로 저장하여 앱 재실행 후 복원합니다. QListWidget의 InternalMove와 rowsMoved 신호로 드래그 결과를 목록 데이터에 반영하며, 이벤트 처리 후 즐겨찾기 그룹을 다시 위에 배치합니다.

Workspace 통합 테스트는 이전 설정 이전, 저장소 추가·중복 방지·클릭 전환, 즐겨찾기 정렬, 순서 변경, 창 재생성 후 복원과 입력 초안 보존을 확인합니다.

## 상태에 따른 단일 동기화 버튼

`SyncController`가 Fetch 성공 여부와 현재 실행 scope를 메모리에 관리합니다. 앱 시작·저장소 열기·브랜치 또는 원격 설정 변경은 Fetch 상태로 돌아갑니다. 마지막 상태를 설정 파일에 저장해 다음 실행에 재사용하지 않습니다.

Fetch는 upstream remote의 `refs/heads/*`를 `refs/remotes/<remote>/*`로 명시하여 가져오며 prune을 적용합니다. upstream 설정은 있지만 서버 브랜치가 아직 없어도 Fetch가 가능합니다. upstream 설정 자체가 없으면 Fetch만 가능하며 Pull/Push는 차단합니다. Git status porcelain v2의 branch 헤더로 ahead/behind와 작업 트리 상태를 읽고, 추적 ref의 실제 존재·OID 및 remote URL을 함께 확인합니다. 변경이 없다면 Fetch, behind만 있으면 Pull, ahead만 있으면 Push이며, Fetch 후 대상 ref가 없으면 Publish입니다. 둘 다 앞서면 Pull 우선의 이력 통합 필요 상태이며 자동 merge/rebase와 Push를 막습니다.

Publish는 실제 커밋이 있는 현재 브랜치만 대상으로 합니다. 실행 직전 snapshot을 재검증한 뒤 `push --set-upstream --force-with-lease=<대상 ref>: <remote> HEAD:<대상 ref>`를 실행합니다. 빈 기대값은 서버 대상이 없어야 한다는 조건으로, Fetch 이후 같은 이름의 브랜치가 생기면 fast-forward 가능한 경우에도 게시를 거절합니다. 일반 Push의 강제 덮어쓰기 옵션은 제공하지 않습니다.

## 원격 설정과 저장소 생성

`src/settings/repositorydialogs.*`의 RemoteDialog는 remote 주소 추가·수정·제거, Push 주소 통일, 원격 브랜치 조회와 현재 브랜치 upstream 설정을 담당합니다. GitClient의 전용 메서드가 원격 이름·Git ref와 현재 브랜치를 검증하며, upstream의 remote/merge 두 키를 순차 저장합니다. 부분 실패를 안내하고 configured 신호로 Fetch 권한을 초기화합니다.

RepositoryDialog는 init과 전체/Shallow/Partial Clone 입력을 받아 GitClient를 호출합니다. GitClient는 새 절대 경로 또는 빈 디렉터리만 허용하고 성공 여부와 관계없이 활성 저장소 경로를 직접 바꾸지 않습니다. 성공한 경우 MainWindow가 반환 경로를 기존 openRepository 경로로 등록합니다. Clone stderr는 전송 메시지로 전달하며 진단용 최근 256 KiB, 화면은 최근 300블록을 유지합니다. 명령 로그에서 Clone/remote 저장 URL을 가립니다. 작업 중 닫기를 막고 실패한 디렉터리를 자동 삭제하지 않습니다.

이 기능의 사용 범위와 제약은 [원격·저장소·Workspace 안내](REMOTES_AND_WORKSPACE.md)를 참고합니다.

실행 직전에 비교를 다시 수행하고 snapshot이 다르면 Fetch 권한을 초기화합니다. 명시적인 upstream refspec으로 Push하여 push.default나 remote.push가 다른 브랜치를 선택하지 않도록 합니다. Pull은 fast-forward, no-rebase, no-autostash이며 쓰기 실패 후에는 반드시 다시 Fetch해야 합니다. 새로고침과 커밋 완료 후에도 상태를 재평가합니다.

통합 테스트는 최초 Fetch의 비변경성, ahead에서 Push, behind에서 Pull, 양쪽 분기에서 Push 차단, Fetch 실패 후 잠금을 실제 임시 bare 원격 저장소로 검증합니다.

## 상단 구역과 브랜치 조작

상단의 repositoryHeaderPanel, branchControlsPanel, syncControlsPanel은 같은 높이의 별도 영역입니다. 저장소 정보에는 폴더명·현재 브랜치와 작은 경로를 표시합니다. 브랜치 콤보의 activated 신호만 전환을 실행하므로 목록 새로고침으로 프로그램이 항목을 설정할 때는 전환하지 않습니다.

새 브랜치는 `git switch -c <name> HEAD`, 이름 변경은 `git branch -m -- <old> <new>`, 삭제는 `git branch -d -- <name>`으로 실행합니다. 삭제 목록에서 현재 브랜치를 제외하고 강제 삭제 옵션은 제공하지 않습니다. 생성·전환·이름 변경 후 동기화 상태는 다시 Fetch부터 시작합니다.

앱이 소유하는 입력·삭제·오류 대화상자에는 FramelessWindowHint를 사용합니다. 폴더 선택은 비네이티브 QFileDialog를 사용해 같은 정책을 적용합니다. 메인 창과 외부 인증 도구의 창에는 이 설정을 적용하지 않습니다.

GUI 테스트는 즉시 전환, 현재 HEAD 기반 생성, 이름 변경·삭제, 대화상자의 프레임 제거와 미병합 브랜치 삭제 거부를 확인합니다.

## 전체 Stage와 현재 브랜치 삭제

전체 Stage/Unstage 버튼은 현재 상태 목록의 모든 경로를 GitClient에 전달하며 체크 선택과 독립적으로 동작합니다.

현재 브랜치 삭제창은 이름을 고정 표시하고 다른 로컬 브랜치로 이동한 후 삭제합니다. 로컬 브랜치가 하나만 남으면 UI 삭제를 비활성화합니다. 강제 삭제 체크의 모든 false→true 전환에 frameless 경고를 표시하며, 경고를 거부하면 체크를 해제합니다. upstream이 없는 경우 원격 삭제 체크는 비활성화합니다.

GitClient::deleteCurrentBranch는 실행 직전 현재 브랜치 이름을 검증하고 switch → branch -d/-D → 선택적 remote ref 삭제를 순차 실행합니다. 실패한 단계 뒤의 작업은 진행하지 않으며, 로컬 삭제 이후 원격 실패는 부분 성공으로 안내합니다. 원격 삭제는 지정한 refs/heads/* ref만 대상으로 하고 강제 push·mirror·tag 동반 push를 사용하지 않습니다.

GUI 테스트는 전체 Stage/Unstage, 경고 재표시와 원격 미연결 체크 비활성화를 검사합니다. 임시 bare remote 통합 테스트는 현재 브랜치 삭제 후 전환, 원격 삭제, 일반 삭제의 미병합 보호, 강제 삭제 및 마지막 브랜치 삭제 후 HEAD 보존을 검증합니다.

현재 브랜치 삭제의 이동 대상은 사용자가 선택하지 않습니다. 삭제 대상 자신을 제외한 로컬 목록에서 main, master, 나머지 첫 브랜치 순으로 자동 결정합니다. 다른 브랜치가 없으면 UI에서 삭제할 수 없습니다. 삭제창에는 결정된 이동 위치를 안내 문구로만 표시합니다. 하위 GitClient의 마지막 브랜치 삭제 지원은 보존하지만 상단 삭제 버튼에서는 호출하지 않습니다.

## 단일 탭과 별도 비교창

커밋 History의 Diff는 조회 전용이라 줄 선택을 표시하지 않습니다. 변경 사항의 부분 Stage/Unstage는 그대로 유지합니다. MergePanel은 비편집 QComboBox에 실제 브랜치의 표시명과 전체 ref를 저장하며 심볼릭 원격 HEAD를 제외합니다. Sync에서 전달한 전체 upstream ref는 목록을 읽은 뒤 선택·비교합니다.

주 기능의 QTabWidget 하나만 유지합니다. `FlatSections`는 Stash·이력 수정·병합 페이지의 하위 탭을 스크롤 가능한 구역으로 표시합니다. Diff는 QStackedWidget과 보기 선택 목록을 사용해 통합/나란히/쌓기형/줄 선택을 전환합니다. MainWindow의 기능 소개 문구는 선택한 주 탭에 맞춰 갱신됩니다.

History의 표는 주 화면 전체를 사용하며 상세 필드는 오른쪽 열에 포함합니다. 메시지 검색 외의 조건은 frameless 대화상자에서 편집하고, 날짜는 직접 입력하거나 QCalendarWidget으로 선택합니다. 취소 시 이전 조건을 복원합니다. 행 더블 클릭은 파일 목록과 Diff를 함께 가진 별도 비교창을 엽니다. 조회 중 다른 행을 선택하면 공유 GitClient가 유휴 상태가 된 후 마지막 선택을 읽습니다.

MergePanel은 충돌 파일을 선택하면 GuardedDialog에 stage 2 / 편집 결과 / stage 3을 가로로 표시합니다. 화면의 Base는 현재 쪽(stage 2)이며 공통 조상(stage 1)과 구분합니다. 저장되지 않은 편집과 실행 중인 작업은 창 닫기에서 보호하고 오류는 편집창 안에도 표시합니다. Controller의 백업·fingerprint·Stage 보호는 유지합니다.

`ActionLogger`는 버튼의 pressed 신호를 기록하므로 clicked에서 시작하는 Git 명령보다 행동 로그가 먼저 남습니다. 새 대화상자가 표시될 때 버튼과 메뉴도 등록합니다. 입력 필드의 내용은 수집하지 않습니다.

## GitHub 계정·저장소

`github/GithubClient`는 별도 QProcess로 gh를 비동기 실행하며 API 45초·로그인 5분·출력 4 MiB 제한과 중단을 적용합니다. 토큰 환경 변수와 디버그 출력을 자식 프로세스에서 제외합니다. auth status JSON에서 login/host/active/state/scopes/tokenSource/gitProtocol만 모델로 전달하고 token/error 원문은 UI에 전달하지 않습니다. 로그인 stderr는 일회용 코드와 동일 호스트 HTTPS 로그인 URL만 추출합니다.

`GithubPanel`은 gh 경로·호스트·로그인 후 자동 Git 연결 옵션을 QSettings에 저장합니다. 기본 화면은 브라우저 로그인과 저장소이며 인증 세부 항목은 frameless 고급 설정 창으로 분리합니다. 로그인 URL을 검증한 뒤 QDesktopServices로 자동으로 엽니다. 로그인 성공→선택적으로 setup-git→계정 상태→저장소를 순차 실행하며 자동 Git 연결은 기본 활성화이고 호스트의 전역 HTTPS helper에 적용됩니다. 오류·중단 시 자동 진행을 멈춥니다. 첫 표시에서는 기존 계정과 목록만 자동 복원합니다. 사용자 목록과 저장소는 메모리에만 보유하며 계정·호스트 변경 및 실패 시 버립니다. user/repos 페이지 조회 전후 user API로 활성 계정을 재검증합니다. Clone은 RepositoryDialog로 URL을 전달하고 성공 경로를 MainWindow의 Workspace에 등록합니다. Windows 패키지는 체크섬을 검증한 공식 gh 실행 파일과 라이선스를 함께 배포합니다.

검증은 실제 계정 대신 테스트 실행 파일의 모의 gh 모드로 진행합니다. 브라우저 승인·Enterprise·조직 SSO의 실서버 검증은 후속이며 [지원 범위](GITHUB_ACCOUNTS_AND_REPOSITORIES.md)에 기록합니다.

## GitHub PR 협업

`github/PullRequests`는 선택한 host/login/repository 컨텍스트에 묶인 작업 컨트롤러입니다. `GithubClient::api`가 활성 계정을 검증하고 gh api를 호출합니다. 쓰기 본문은 JSON 표준입력으로 전달하며 로그에는 메서드와 endpoint만 남깁니다. 조회 후 계정을 다시 검증하고 PR 상세 조회 전후 head/base SHA·상태·갱신 시각을 비교합니다. 초안의 리뷰 준비 전환에만 지정된 GraphQL mutation을 사용합니다.

`PullRequestDialog`는 단일 큰 frameless 창으로 목록·상세·CI·파일·Diff·리뷰 입력을 배치합니다. 생성은 별도 frameless 창에서 원격 브랜치·fork·초안·비교·템플릿을 처리합니다. 작업 전체의 busy 상태를 GithubPanel과 MainWindow로 전달해 gh 호출 사이와 로컬 체크아웃까지 잠금을 유지합니다. 쓰기 직전 상세 재조회와 Merge API의 sha, 리뷰의 commit_id, update-branch의 expected_head_sha를 사용합니다. 권한·저장소 허용 방식·clean 상태와 조회된 모든 검사 성공 여부를 보수적으로 확인하고 관리자 우회 옵션은 제공하지 않습니다.

체크아웃은 기존 GitClient를 사용해 원격 URL 일치와 미커밋 변경을 검사하고 PR ref를 가져온 뒤 SHA를 확인합니다. switch의 --no-overwrite-ignore로 무시 파일도 보호합니다. 상세 Diff는 원격 patch를 읽기 전용으로 표시하는 `DiffWidget::showPatch`를 재사용합니다. 모의 API와 실제 임시 Git 저장소 검증 및 남은 고급 범위는 [PR 안내](GITHUB_PULL_REQUESTS.md)에 기록합니다.

## Stash와 작업 트리 정리

`src/worktree/worktreepanel.*`는 Stash 목록·상세·쓰기, 체크 파일 정리와 .gitignore 편집을 제공합니다. `worktreecontroller.*`는 작업 상태 지문에 파일 내용과 Stash 목록을 더한 `WorktreeReview`를 만들고, UI가 확인한 지문과 실행 직전 상태를 비교합니다. 공유 GitClient가 메타데이터·History 조회를 끝낸 뒤에만 지연 새로고침하므로 명령을 중첩하지 않습니다. .gitignore 초안은 앱 실행 중 저장소별로 보존하고 초안의 원래 지문을 사용합니다.

Stash는 선택 OID로 적용하고 삭제 직전 reflog selector의 OID를 다시 확인합니다. Pop은 apply 성공 후에만 drop하며, 삭제 계열은 보관본 커밋의 복구 참조를 남깁니다. Clear는 모든 복구 참조를 만든 다음 목록을 재검사합니다. Branch는 원래 부모에서 switch -c → apply --index → drop 순서이며 실패한 단계 뒤는 실행하지 않습니다.

파일 정리는 리터럴 경로, 저장소 내부 경로 검증, 일반 파일만 허용하는 정책을 사용합니다. 실행 전 선택 파일·인덱스·선택 Stage 파일을 실제 gitDirectory 아래 복구 폴더에 저장하고 지문을 다시 확인합니다. Clean은 명시적으로 체크한 미추적 일반 파일에만 -f를 사용합니다. .gitignore는 QSaveFile로 저장합니다. GitClient의 작업/충돌/잠금 보호 및 쓰기 강제 종료 금지를 재사용합니다. Stash list/show는 중단 가능한 조회로 분류합니다.

구체적인 영향, 실패 시 보존 범위와 수동 복구 방법은 [Stash·작업 트리 안내](STASH_AND_WORKTREE.md)에 기록합니다. 쓰기 완료 후 MainWindow가 로컬 상태를 갱신하고 동기화 권한을 다시 Fetch부터 시작합니다.

## 커밋·Diff 고급 기능과 병합 편집기

`git/advancedcommit.cpp`는 Amend·sign-off 옵션을 실행합니다. Amend는 확인한 HEAD를 검증하고 원래 커밋의 복구 참조를 남긴 뒤 기존 공통 commit 보호 검사를 거칩니다. MainWindow는 빈 입력에만 기존 메시지를 불러오며 확인창과 작성자 설정 흐름을 재사용합니다.

`diff/linepatch.cpp`는 선택한 원본 패치 행으로 문맥·hunk 범위와 신규/삭제 메타데이터를 재구성합니다. Unstage는 패치를 뒤집은 Index 기준으로 계산합니다. DiffWidget의 줄 체크 UI가 이 함수를 사용하고 원본 diff 재검사 → apply --check → 실제 적용 순서를 따릅니다. 미추적 파일은 읽기 전용 diff --no-index로 조회하며 그 명령의 차이 있음(exit 1)을 정상 결과로 처리합니다. UTF-8 일반 텍스트, 8 MiB·2만 줄 제한을 적용합니다.

`merge/mergecontroller.*`는 비교한 대상 OID·HEAD·공통 상태를 묶은 MergePreview를 재검사하고 HEAD 백업 후 일반 병합을 실행합니다. --no-autostash와 --no-overwrite-ignore로 무시 파일과 미커밋 변경을 보호합니다. SyncController의 Diverged 실행은 최신 비교가 같고 Fetch 권한이 있을 때 mergeRequested 신호를 보내며 MainWindow가 해당 upstream 비교 화면을 엽니다.

`merge/mergepanel.*`는 브랜치 비교와 별도 Base(stage 2)/결과/Merge(stage 3) 편집 UI를 제공합니다. ConflictFile은 unmerged 인덱스의 모드·OID, 존재 여부·세 버전 바이트·작업 파일·작업 상태 지문을 담습니다. 저장 전 재조회 → 파일/세 버전 백업 → 상태 재확인 → QSaveFile 저장 또는 삭제 → 선택 경로만 add -A → 작업 상태 조회 순서입니다. 모든 쓰기는 확인창을 거치며 충돌 표시·오래된 편집을 차단합니다. Continue/Abort는 기존 recoverOperation을 재사용합니다.

바이너리/비 UTF-8은 전체 버전 선택만 제공하며 모드·심볼릭 링크·서브모듈·대규모/복잡한 경로 충돌은 제한합니다. 인코딩·Rebase의 stage 의미·파일 백업 위치·복구 한계는 [고급 커밋·병합 안내](ADVANCED_COMMIT_AND_MERGE.md)에 기록합니다.

## Interactive rebase와 이력 복구

Git의 추가 기능을 확장하는 공통 화면은 아래 Git Tools 절을 참고합니다.

`history/rewritecontroller.*`는 선형 커밋 범위를 읽고 계획의 OID 집합·동작·순서와 HEAD/Index/작업 상태를 검증합니다. 무시 파일과 변경 경로가 겹치면 중단하며 시작 HEAD를 복구 참조에, 계획을 실제 Git 디렉터리의 JSON에 저장합니다. `rewritepanel.*`는 계획 표·Reword 메시지·순서 이동, HEAD reflog·앱 참조 검색과 복구 버튼을 제공합니다. History의 기준 선택, MergePanel의 충돌 편집, 메인 Amend와 연결합니다.

`history/rebaseeditor.*`는 앱의 `--gitcanvas-rebase-editor` 진입점을 QCoreApplication으로 실행합니다. GIT_SEQUENCE_EDITOR는 Git이 생성한 커밋 집합을 검증한 뒤 허용한 여섯 동작만 기록합니다. GIT_EDITOR는 계획의 Reword 메시지 또는 Git의 Squash 메시지를 사용합니다. Continue는 orig-head와 보관 계획이 맞을 때 도우미를 다시 연결하며 완료·Abort 후 활성 계획을 제거합니다. MinGW 빌드는 Git 셸의 런타임 DLL 충돌을 피하도록 실행 파일 옆에 컴파일러 런타임을 복사합니다.

공통 상태에 rebase-merge/amend를 포함하여 Edit 정지에서만 Amend를 허용합니다. 복구 UI는 OID로 브랜치 보존, 깨끗한 브랜치의 reset --keep와 직전 HEAD 보관, 삭제 Stash의 store, 예상 OID를 지정한 앱 참조 삭제를 실행합니다. 파일 백업은 폴더 탐색과 수동 복사를 제공합니다. 자세한 범위는 [이력 수정·복구](HISTORY_REWRITE_AND_RECOVERY.md)를 참고합니다.

## Git Tools 기능 레지스트리

`tooladvanced.cpp`는 Notes·Replace·Sparse·서명·Submodule·LFS 등의 추가 기능 정의와 인자 변환을 담당합니다. `gitcommandform.*`은 설치된 Git의 HTML 설명서를 읽어 명령별 옵션 선택·값·순서·반복 폼을 구성합니다. 옵션 이름은 설명서에서 선택하며, 사용자 값은 JSON 배열을 거쳐 개행을 포함한 하나의 인자로 보존합니다. 생성된 폼은 기존 `ToolboxController`의 미리보기·상태 재검사·쓰기 실행 경로를 사용합니다. 설명서 기반 표기 파싱과 명령별 의미 검증은 구분하며, 모든 옵션 조합이 검증된 것으로 취급하지 않습니다.

고급 표준 입력은 UTF-8 텍스트 또는 해시로 검증한 최대 8 MiB 파일을 지원합니다. stdout 파일은 명령 성공 후 원본 바이트를 `QIODevice::NewOnly`로 저장하며 stderr·복구 참조를 섞지 않습니다. 이 경로는 대용량 스트리밍이 아니고, 명령 완료 뒤 출력 저장이 실패할 수도 있으므로 자동 재실행하지 않습니다.

`src/tools/toolcatalog.cpp`의 `GitTool` 정의는 분류·설명·입력 필드·조회/쓰기·깨끗한 작업 트리 요구 조건을 제공합니다. `arguments`가 입력 값을 QStringList로 변환합니다. `gittoolbox.cpp`는 정의로 입력 폼을 만들고 검색·미리보기·확인·결과 및 태그/작업 트리 표를 표시합니다. MainWindow에서 Ctrl+Shift+P와 Workspace 열기를 연결합니다.

`toolboxcontroller.cpp`는 공통 작업 상태와 전체 참조·유효 설정·작업 트리 등록을 해시로 묶어 미리보기와 실행 상태를 비교합니다. 파일 입력은 별도 해시로 검사합니다. 태그 원격 작업은 Fetch·ls-remote 이후 예상 원격 OID를 lease에 고정합니다. 로컬 태그 삭제는 원래 객체를 보관하고 예상 OID를 지정해 update-ref로 삭제합니다. 작업 트리 제거는 Git의 기본 보호에 더해 무시 파일도 검사합니다.

`GitClient::executeReviewed`는 검토한 쓰기를 실행합니다. 임의 옵션의 조회/쓰기 분류를 추측하지 않고 보수적으로 쓰기·중단 불가·인자 로그 가림으로 처리합니다. 표준 입력은 UTF-8 바이트를 전달한 뒤 닫고 외부 편집기는 실패하도록 설정합니다. 이 API의 호출자는 사전에 상태·영향 검사를 수행해야 하며 일반 화면이 임의 인자를 바로 넘기지 않습니다.

고급 실행은 설치된 내장 명령과 lfs를 허용하고, 직접 Push/Pull·서버·대화형 화면·별칭 실행은 제한합니다. 이는 Git hook이나 하위 명령의 외부 실행을 격리하는 샌드박스가 아닙니다. 모든 옵션의 개별 안전 검증은 [전체 기능 확장 계획](GIT_TOOLS_AND_COVERAGE.md)의 명령별 어댑터로 확장합니다.
