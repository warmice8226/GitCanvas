# 주의사항
이 저장소의 문서 및 코드는 전부 AI를 활용하여 제작하였으며 문제가 생길 경우 언제든 삭제 될 수 있습니다.

# GitCanvas
Windows ZIP · macOS 앱 ZIP · Linux AppImage로 배포합니다. 사용자는 빌드나 Qt 설치 없이 실행하며 Git CLI는 별도로 필요합니다. [배포·라이선스 점검](docs/PORTABLE_DISTRIBUTION.md)을 참고하세요.


GitCanvas는 C++20, Qt 6, Git CLI로 만드는 크로스 플랫폼 Git GUI입니다.

처음 참여하거나 용어가 익숙하지 않다면 [`docs/README.md`](docs/README.md)에서 문서를 순서대로 읽으세요. 필수 개발 범위는 [`docs/필수 기능 명세.md`](docs/필수%20기능%20명세.md)에 있습니다. 프로젝트에 영향을 주는 결정과 절차는 대화에만 남기지 않고 `docs`에 함께 기록합니다.

## 개발 목표

- **Git 기능 전체를 GUI로 제공:** 일반 작업뿐 아니라 설정, worktree, tag, stash, submodule, bisect, reflog과 고급 이력 작업까지 Git CLI가 제공하는 기능을 단계적으로 지원합니다. 아직 전용 화면이 없는 명령은 안전한 고급 명령 실행 화면으로 보완합니다.
- **Git CLI 실행을 GUI가 대행:** 사용자가 Git Bash나 터미널에서 명령을 직접 조합하지 않아도 되도록 입력 폼, 선택 항목과 확인 창을 Git 명령 및 인자로 변환해 실행합니다.
- **한국어와 영어 지원:** 화면, 도움말과 Git 오류에 대한 앱 설명을 한국어와 영어로 제공합니다. Git이 출력한 원문은 문제 진단을 위해 함께 확인할 수 있습니다.
- **기능별 짧은 설명 제공:** 버튼, 메뉴와 고급 옵션에 무엇을 하는 기능인지, 작업 트리와 이력에 어떤 영향을 주는지를 툴팁이나 설명 영역으로 안내합니다.
- **기본/자세히 모드와 명령 미리보기:** 초보자는 권장 기본값으로 바로 작업하고, 숙련자는 `자세히`에서 체크박스와 드롭다운으로 Git 옵션을 조합합니다. 모든 작업은 실제 실행될 Git 명령을 확인하고 복사할 수 있게 합니다.
- **로컬 및 SSH 원격 작업공간:** 로컬 컴퓨터뿐 아니라 등록한 SSH 컴퓨터의 저장소와 Git 실행 파일을 같은 GUI에서 사용하고, 컴퓨터·SSH 사용자·Git 작성자·GitHub 계정별 scope를 명확히 표시합니다.
- **조작 가능한 Git History 그래프:** commit, branch와 merge 관계를 시각화하고 그래프에서 commit을 선택해 checkout/switch, branch·tag 생성, 비교, merge, rebase와 reset 같은 작업을 시작할 수 있게 합니다.
- **이력 범위를 선택하는 Clone:** 기본값은 전체 Git log를 포함하는 일반 clone이며, 사용자가 원할 때만 최근 이력만 받는 shallow clone 또는 blob을 지연 수신하는 partial clone을 선택할 수 있게 합니다.

위 목표에서 모든 기능은 Git 버전과 저장소 상태에 따라 사용 가능한 Git CLI 기능을 의미합니다. 위험하거나 되돌리기 어려운 작업은 영향 미리보기, 명시적 확인과 복구 안내를 갖춘 뒤 노출합니다.

## 현재 기능

- Git 저장소 선택 및 마지막 저장소 기억
- 변경 파일과 staged 파일 조회
- 여러 파일 stage/unstage
- 커밋 메시지 작성 및 커밋
- 모든 Git 명령 비동기 실행

## 필요한 도구

- CMake 3.21 이상
- Qt 6.3 이상 (`Widgets` 모듈, Windows 자동 배포는 Qt 6.3 이상 권장)
- C++20 컴파일러
  - Windows: Visual Studio 2022의 MSVC
  - Ubuntu: GCC 또는 Clang
- Git CLI

## 빌드

Qt가 설치된 환경에서 다음을 실행합니다.

```sh
cmake -S . -B build
cmake --build build --config Release
```

Qt를 자동으로 찾지 못하면 Qt 설치 경로를 지정합니다.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/compiler
```

운영체제별 의존성과 무설치 패키지 제작 방법은
[`docs/LINUX_DEVELOPMENT_AND_PACKAGING.md`](docs/LINUX_DEVELOPMENT_AND_PACKAGING.md)를 참고하세요.

## 새 Git 기능 추가하기

1. `GitClient`에 의미가 분명한 메서드를 추가합니다. 예: `push()`, `pull()`, `createBranch()`.
2. 메서드 내부에서 `run()`에 Git 인자를 전달합니다.
3. `MainWindow`의 버튼을 해당 메서드와 연결합니다.

문자열로 전체 셸 명령을 만들지 않고 프로그램과 인자를 분리하므로 공백이 있는 파일명도 안전하게 처리됩니다.

## 라이선스

GPL-3.0-only

## Windows 프로토타입 실행

실제 기본 Git 기능을 갖춘 Windows x64 프로토타입과 무설치 패키징 스크립트를 추가했습니다. 자세한 사용법과 범위는 [Windows 프로토타입 안내](docs/WINDOWS_PROTOTYPE.md)를 참고하세요.

- 실행: ZIP 전체 압축 해제 후 `GitCanvas/bin/GitCanvas.exe`
- 패키징: `powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1`
- 검증: `ctest --test-dir build --output-on-failure`
