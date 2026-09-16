# GitCanvas 문서 안내

현재 배포 정책과 리팩터링 검증 범위는 [무설치 배포·점검](PORTABLE_DISTRIBUTION.md)에 기록합니다.

이 디렉터리는 GitCanvas를 개발하면서 결정한 내용과 다시 찾아볼 가능성이 있는 절차를 보관합니다. 대화에서 프로젝트에 영향을 주는 결정이 나오면 관련 문서를 함께 갱신하는 것을 원칙으로 합니다.

## 처음 읽는 순서

사용자를 위한 [HTML 사용 설명서](USER_GUIDE.html)는 브라우저에서 바로 열 수 있습니다. 시작하기·커밋·동기화·복구·Git Tools 사용법과 검색·인쇄 기능을 포함하며, 인터넷 연결 없이 읽을 수 있습니다.

Git Tools의 한/영 설명과 원문 보기는 [Git Tools 설명 언어](GIT_TOOLS_TRANSLATIONS.md)에 정리했습니다.

14번의 앱 번들·빌드·서명/공증·검증 범위는 [macOS 지원](MACOS.md)에 정리했습니다.

13번 전체 Git GUI 확장은 [명령별 옵션 폼·추가 기능·지원 범위](GIT_COMMAND_FORMS.md)에 정리했습니다.

12번의 접속 방법·원격 작업·복구·지원 경계는 [SSH 원격 작업공간](SSH_WORKSPACE.md)에 정리했습니다.

11번 기능 사용법과 배포 검증 범위는 [언어·진단·성능·배포](QUALITY_AND_DEPLOYMENT.md)에 정리했습니다.

현재 기능과 다음 작업을 확인하려면 [구현 현황과 작업 예정 순서](IMPLEMENTATION_STATUS.md)를 먼저 읽습니다. 구현·부분 구현·미구현 범위와 우선순위를 정리한 문서입니다.

1. [프로젝트 개요와 기술 선택](PROJECT_OVERVIEW.md)
2. [구조와 Git 기능 추가 방법](ARCHITECTURE.md)
3. [필수 기능 명세와 개발 순서](필수%20기능%20명세.md)
4. [CMake, 빌드, 패키징 용어 설명](BUILD_AND_PACKAGING_BASICS.md)
5. [Linux 개발 환경과 실제 배포 명령](LINUX_DEVELOPMENT_AND_PACKAGING.md)
6. [Java 개발자를 위한 C++/Qt 안내](CPP_FOR_JAVA_DEVELOPERS.md)
7. [라이선스와 배포 원칙](LICENSE_AND_DISTRIBUTION.md)

## 문서 관리 원칙

- 설치 명령, 빌드 명령 또는 배포 방법이 바뀌면 관련 문서를 코드와 함께 수정합니다.
- 새로운 Git 버튼이나 기능을 추가하면서 구조가 달라지면 `ARCHITECTURE.md`를 수정합니다.
- 필수 기능의 범위, 우선순위 또는 완료 조건이 바뀌면 `필수 기능 명세.md`를 수정합니다.
- 지원 운영체제, Qt 최소 버전 또는 라이선스가 바뀌면 `PROJECT_OVERVIEW.md`와 `LICENSE_AND_DISTRIBUTION.md`를 수정합니다.
- 문서에 없는 중요한 결정을 발견하면 이 목차에 새 문서를 추가합니다.
- 명령만 기록하지 않고 그 명령을 실행하는 이유와 생성 결과도 함께 기록합니다.

## Windows 프로토타입 (2026-09-14)

현재 프로토타입은 저장소 열기, 변경 파일 조회, diff 미리보기, 파일 단위 stage/unstage, 커밋, 로컬 브랜치 생성·전환, 최근 커밋 그래프와 fetch/pull/push를 지원합니다. UI는 한국어와 어두운 테마로 제공됩니다.

- [Windows 실행 및 패키징 안내](WINDOWS_PROTOTYPE.md)
- [GitHub 로그인·다중 계정·저장소 조회와 Clone](GITHUB_ACCOUNTS_AND_REPOSITORIES.md)
- [GitHub PR 생성·리뷰·CI·병합과 로컬 체크아웃](GITHUB_PULL_REQUESTS.md)
- [공통 작업 상태·중단·복구 안내](OPERATIONS_AND_RECOVERY.md)
- [Stash·작업 트리 정리와 백업 복구 안내](STASH_AND_WORKTREE.md)
- [고급 커밋·줄 선택 Diff·병합·3-way 충돌 해결](ADVANCED_COMMIT_AND_MERGE.md)
- [Interactive rebase·Reflog·이력 복구](HISTORY_REWRITE_AND_RECOVERY.md)
- [Git Tools: 태그·Worktree·Bisect·설정·패치와 전체 Git 기능 확장 방향](GIT_TOOLS_AND_COVERAGE.md)
- [원격·최초 Push·init·Clone·Workspace 사용 안내](REMOTES_AND_WORKSPACE.md)
- [Git 환경·작성자 설정](GIT_ENVIRONMENT_AND_IDENTITY.md): Git 진단, Local/Global 이름·이메일 설정, 실제 커밋 신원 확인
- [Diff·History 사용법과 구현 범위](DIFF_AND_HISTORY.md): 나란히 비교, 구간 Stage, 검색·추가 로딩, 부모/커밋 비교, 이력 작업
- 무설치 ZIP: `out/GitCanvas-0.1.0-windows-x64.zip`
- [테마·파일 목록·명령 조립 화면](UI_THEME_AND_COMMANDS.md): 라이트/다크 전환, 목록 접기·확장, 상세 보기 버튼, Git 옵션 값 선택과 명령문 조립
- 압축 해제 후 `GitCanvas/bin/GitCanvas.exe` 실행

GitHub 계정·저장소는 [gh 기반 연동 안내](GITHUB_ACCOUNTS_AND_REPOSITORIES.md), PR 협업은 [PR 안내](GITHUB_PULL_REQUESTS.md)를 참고하세요. SSH와 PR의 고급 메타데이터·discussion 등은 후속 구현 대상입니다.
