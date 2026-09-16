# SSH 원격 작업공간 (12번)

## 시작하기

Workspace의 **SSH 원격 작업공간**을 누르면 제목표시줄 없는 별도 창이 열립니다. 이 창의 경로·Git·파일 작업은 원격 컴퓨터에만 적용합니다. 메인 창의 로컬 저장소와 별도로 관리합니다.

필요한 환경:

- 로컬 Windows: OpenSSH Client의 `ssh`, `ssh-keyscan`이 PATH에 있어야 합니다. 앱 ZIP에 SSH 서버나 Python을 설치하지 않습니다.
- 원격: POSIX 서버, Python **3.8 이상**, Git **2.31 이상**, OpenSSH의 Ed25519 호스트 키. 현재는 Linux/Ubuntu 서버를 대상으로 합니다.
- SSH 키 또는 ssh-agent로 로그인할 수 있어야 합니다. 암호화된 키는 로컬 ssh-agent에 먼저 로드합니다. 앱은 비밀번호·passphrase 입력창을 제공하지 않습니다.

1. 표시 이름, 실제 호스트/IP, SSH OS 사용자, 포트, 개인키의 **로컬 절대 경로**를 입력합니다. 개인키 경로를 비우면 ssh-agent/기본 키를 사용합니다.
2. 허용할 원격 시작 경로를 지정합니다. 예: `/home/dev/projects`. 저장소와 Git 메타데이터가 이 경로 안에 있어야 쓰기가 가능합니다. 연결된 worktree의 공통 Git 디렉터리가 바깥에 있으면 범위를 명시적으로 넓히세요.
3. **호스트 확인 · 연결 / 재연결**을 선택합니다. 첫 연결은 SHA256 호스트 키 지문을 보여줍니다. 서버 관리자 등 별도 경로로 받은 지문과 비교한 뒤 수락하세요. `ssh-keyscan` 자체는 서버 신원을 인증하지 않습니다. [OpenSSH 설명](https://man.openbsd.org/ssh-keyscan)
4. 연결 화면에서 원격 OS 사용자·Git 버전·HOME을 확인합니다. 원격 저장소 절대 경로를 입력하거나 폴더를 탐색한 뒤 **저장소 열기 · 상태 재확인**을 누릅니다.
5. 열었던 저장소는 SSH 프로필별로 저장됩니다. 더블 클릭으로 다시 열고 즐겨찾기를 상단에 둘 수 있습니다. 동일한 경로라도 프로필 ID·실제 경로·Git common directory가 다르면 별도 등록입니다.

## 현재 제공하는 작업

| 기능 | 동작 |
|---|---|
| 상태 | 변경 파일, 현재/로컬 브랜치, 원격 Git 작성자, remote URL, 진행 작업·잠금·helper 기록 조회 |
| Stage / Unstage | 체크한 파일 단위 처리, 첫 커밋 이전 Unstage도 지원 |
| 커밋 | 제목·설명 입력, 원격 Index 전체로 커밋, 원격 Git 작성자/설정 사용 |
| 브랜치 | 선택 즉시 전환, 현재 커밋에서 새 브랜치 생성, 깨끗한 작업 트리 요구 |
| 삭제 | 서버 이름을 직접 입력해 확인, HEAD 복구 참조를 남기고 main/master로 이동한 뒤 `branch -d`. main/master·마지막 브랜치·미병합 브랜치 강제 삭제는 제공하지 않음 |
| Fetch / Pull / Push | Fetch 선행, 동일 단일 fetch/push URL과 기존 upstream 필요. Pull은 가져온 커밋으로 fast-forward만 수행. 분기된 이력은 서버에서 병합 후 재Fetch |
| Diff | 파일 단위 조회와 기존 통합/나란히/쌓기 표시. 원격 부분 Stage·줄 선택은 제공하지 않음 |
| History | 최근 100개 커밋의 메시지·참조·작성자·날짜·상세 데이터 테이블 |
| 파일 편집 | 변경 파일 선택 또는 상대 경로 입력, 8 MiB 이하 UTF-8 일반 파일. UTF-8 BOM과 일관된 CRLF 유지, 혼합 줄바꿈은 LF로 정규화 |

Push는 원격 추적 커밋이 현재 HEAD의 조상인지 먼저 검사합니다. 그 후 **검사한 원격 OID와 정확히 같을 때만** Push하도록 lease를 적용합니다. 사용자가 임의로 이력을 덮어쓰는 Force Push 기능은 아닙니다. Fetch 티켓은 한 번만 쓰며 HEAD·브랜치·상태가 달라졌으면 다시 Fetch해야 합니다. 원격의 새 브랜치 최초 게시는 아직 별도 범위입니다.

Git 작성자 이름·이메일이 원격에 없으면 원격 Git 설정을 먼저 저장해야 합니다. 로컬 Git 작성자나 로컬 GitHub 계정을 원격 설정으로 복사하지 않습니다.

## 실행·인증 경계

`SshExecutor`가 OpenSSH를 실행하고 실행 파일에 포함된 Python helper를 압축하여 메모리에서 실행합니다. 서버에 상주 agent를 설치하지 않습니다. 고정된 bootstrap 코드와 base64만 SSH command에 들어가며, 경로·메시지·파일 본문·동작은 크기 제한이 있는 버전 1 JSON stdin으로 전달합니다. helper는 허용된 동작을 검증한 후 Git 인자 배열을 실행합니다. 임의 셸 명령이나 Git 고급 인자 입력은 지원하지 않습니다.

stdout은 줄 단위 JSON 진행/결과 프레임입니다. stderr는 별도 수신하고 민감정보 필터를 거쳐 표시합니다. 일반 출력은 8 MiB, stderr는 256 KiB, 파일은 8 MiB, 요청은 12 MiB, 응답 프레임은 16 MiB까지입니다. 큰 저장소에서는 범위를 좁혀야 합니다.

- 앱별 known_hosts를 사용하고 `StrictHostKeyChecking=yes`, `UpdateHostKeys=no`로 키 변경을 차단합니다. 기존 키를 자동 덮어쓰지 않습니다. 호스트/포트를 바꾸면 새 프로필 ID로 저장하여 재확인합니다.
- 개인키 **경로**만 저장하며 키 내용과 passphrase는 저장·복사하지 않습니다.
- `ForwardAgent=no`, `BatchMode=yes`, `-F none`을 사용합니다. 현재 `~/.ssh/config`의 Host/ProxyJump/Include 등은 적용하지 않으며 실제 접속 정보를 직접 입력합니다. [OpenSSH 옵션](https://man.openbsd.org/ssh_config)
- 로컬 GitHub 토큰을 SSH 프로세스 환경에서 제외하고, 로컬 환경변수를 원격으로 전달하는 옵션을 설정하지 않습니다.
- 서버에서 GitHub 등으로 Fetch/Push할 때는 **서버에 이미 설정된** 키·credential helper를 사용합니다. SSH OS 사용자, Git 작성자, GitHub 계정은 독립적입니다. 원격 gh 계정 상세·PR UI는 아직 연결하지 않았습니다.

## 중단·동시 변경·복구

쓰기 직전에 HEAD·브랜치·Git 설정·참조·Index·작업 Diff·미추적 파일의 지문을 다시 검사합니다. 확인 이후 내용이 바뀌면 재확인을 요구합니다. 다른 프로그램의 Git 작업과 원자적으로 잠금을 공유하는 전체 트랜잭션은 아니므로 서버에서 같은 파일을 동시에 편집하지 않는 것이 좋습니다.

파일 저장은 읽었던 바이트의 SHA256를 재확인하고 원본을 백업한 뒤 같은 폴더의 임시 파일을 원자적으로 교체합니다. 불완전하게 수신된 파일 내용을 바로 덮어쓰지 않습니다. symlink 경로와 허용 root 밖의 경로는 차단합니다. 일반적인 변경 충돌은 차단하지만 다른 프로세스의 파일 교체와 완전한 원자적 compare-and-swap을 보장하지는 않습니다.

서버의 Git 디렉터리에 다음 기록을 남깁니다.

- `gitcanvas/ssh-active.json`: 진행 중 helper PID·작업 ID
- `gitcanvas/ssh-results/<id>.json`: 시작·완료·실패 상태. 같은 ID의 쓰기를 재실행하지 않음
- `gitcanvas/ssh-last-result.json`: 마지막 작업 결과. 재연결 후 상태 화면의 `last` 항목에서 확인
- `gitcanvas/ssh-file-backups/<id>.bin`: 파일 저장 전 원본 바이트
- `refs/gitcanvas/ssh-backups/<id>`: 브랜치 삭제 직전 HEAD

백업·작업 결과는 자동 삭제하지 않습니다. 보관 필요성을 확인한 뒤 서버에서 직접 관리합니다.

**SSH 연결 종료**는 전송을 끊는 기능입니다. 이미 시작한 원격 쓰기가 끝났거나 계속 실행 중일 수 있습니다. 재연결 후 상태를 읽고 기록을 확인해야 하며 자동 재실행하지 않습니다. 연결 실패는 오프라인으로 표시하고 저장소 목록은 유지합니다. 연결 제한은 15초, keepalive는 15초/3회, 요청 제한은 180초입니다. 원격 읽기 Git 명령은 120초 제한을 두며 쓰기는 helper가 임의로 강제 종료하지 않습니다.

진행 기록이 남으면 살아 있는 PID 여부를 확인합니다. **중단된 helper 기록 확인 · 해제**는 해당 프로세스가 종료된 경우에만 앱의 기록을 해제합니다. Git의 `index.lock`이나 merge/rebase 상태는 삭제하지 않습니다. PID 재사용 등으로 종료 여부가 불확실하면 서버에서 확인해야 합니다. 진행 중에는 Escape나 창 닫기로 작업창을 닫을 수 없습니다.

## 검증 범위와 후속 작업

실제 임시 Git 저장소로 Stage/Unstage·커밋·브랜치 삭제/복구 참조·Fetch/Pull/Push·파일 백업·오래된 상태 거부·경로 이탈·Git 잠금 보호·작업 ID 재실행 차단을 검사합니다. Qt 테스트는 모의 SSH 프로세스로 host key 정책·프로필·토큰 제외·JSON 전송·연결 실패/중단·GUI 연결을 검증합니다.

접속할 실제 SSH 서버와 계정이 제공되지 않았으므로 **실서버 로그인, 네트워크 단절, 서버별 셸·인증 차이는 검증 대기**입니다. 로컬 Git 화면 전체를 원격으로 전환하는 구조는 아직 아닙니다. SSH config 가져오기/ProxyJump, RSA/ECDSA 호스트 키, 비밀번호/askpass, 원격 gh·PR, 원격 init/clone·최초 Push, 전체 History 그래프·상세, 원격 Stash·Rebase·3-way 충돌 편집과 고급 Git Tools는 후속 범위입니다. 기존 로컬 기능은 메인 창에서 계속 사용합니다.
