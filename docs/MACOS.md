# macOS 지원 (작업 순서 14)



macOS용 앱 번들·패키징·서명/공증 절차와 Apple Silicon/Intel CI 구성을 추가했습니다. **현재 환경은 Windows이므로 실제 Mac 빌드·실행·Gatekeeper 검증은 아직 완료하지 않았습니다.** 아래 절차가 성공한 뒤 생성되는 아티팩트를 사용합니다.

## 실행 형태

- 기본 대상: **macOS 15 이상**, Apple Silicon `arm64` 또는 Intel `x86_64`.
- 결과: `GitCanvas.app`, 아키텍처별 ZIP. Qt 프레임워크·플러그인을 앱 안에 포함합니다.
- ZIP을 풀어 실행합니다. 사용자 Mac에 Qt 개발 도구를 설치할 필요는 없습니다.
- **Git은 별도로 필요**합니다. Git/Xcode Command Line Tools 또는 Homebrew Git을 사용합니다. Finder로 실행할 때 기존 사용자 지정 PATH를 보존하면서 `/opt/homebrew/bin`·`/usr/local/bin`을 시스템 경로 앞에 보완합니다. 셸 프로필이나 설치 명령은 자동 실행하지 않습니다.
- GitHub CLI는 번들 안의 `Contents/MacOS/gh`를 우선 찾습니다. 번들에 없으면 PATH의 `gh`를 사용합니다. 키·계정·토큰을 패키지에 복사하지 않습니다.
- Git·Diff·History·GitHub·SSH 화면은 기존 Qt 구현을 공유합니다. 실제 인증 창, Retina/다중 모니터, 단축키, 대소문자 구분 볼륨과 Keychain 동작은 실제 Mac 검증 목록에 포함합니다.

## 빌드·패키징

Mac의 Xcode Command Line Tools, CMake, Ninja, Python 3, Git과 macOS용 Qt SDK가 필요합니다. CI는 Qt **6.8.3** SDK를 사용하며 앱 실행 파일은 각 runner의 네이티브 아키텍처로 따로 빌드합니다. Universal 2 앱 전체를 생성한다고 표시하지 않습니다.

공식 Qt SDK를 준비한 경우:

```bash
export QT_ROOT="$HOME/Qt/6.8.3/macos"
export QT_LICENSE_DIR="/absolute/path/to/matching-qt-notices"
bash scripts/package-macos.sh
```

`QT_LICENSE_DIR`는 SDK 버전의 라이선스·저작권 고지를 포함한 폴더입니다. `LICENSE`·`COPYING`·`NOTICE` 파일과 `LICENSES` 아래 파일을 앱에 보관합니다. 기본값은 SDK 폴더이며 고지를 찾지 못하면 배포를 중단합니다. CI는 같은 Qt 버전의 qtbase 소스에서 라이선스와 제3자 고지를 준비합니다. 별도 모듈이나 외부 라이브러리를 추가하면 해당 고지도 포함해야 합니다.

Homebrew는 `brew install cmake ninja qtbase qttools git gh`로 도구를 준비할 수 있습니다. SDK 경로를 지정하지 않으면 `brew --prefix qtbase`를 사용하고 `macdeployqt`는 qttools에서도 찾습니다. Intel용 최신 Homebrew 패키지의 바이너리 제공 여부에 따라 소스 빌드가 필요할 수 있으므로 CI는 고정 Qt SDK를 사용합니다.

| 환경 변수 | 용도 |
|---|---|
| `QT_ROOT` | macOS Qt SDK 경로 |
| `MACDEPLOYQT` | 별도 위치의 macdeployqt 실행 파일 |
| `QT_LICENSE_DIR` | 해당 SDK의 고지 폴더 |
| `BUILD_JOBS` | 빌드 병렬 수, 기본 2 |
| `MACOS_DEPLOYMENT_TARGET` | 기본 15.0. SDK·의존 라이브러리보다 낮게 설정해도 호환성을 보장하지 않음 |
| `GITHUB_CLI_BINARY` | 포함할 로컬 macOS gh 실행 파일 |
| `GITHUB_CLI_LICENSE` | gh를 포함하는 경우 필수 라이선스 파일 |
| `CODESIGN_IDENTITY` | 서명 ID. 기본 `-`는 개발용 ad hoc 서명 |
| `NOTARY_PROFILE` | 선택적 Apple 공증용 Keychain 프로필 이름 |

GitHub CLI 포함 예시:

```bash
export GITHUB_CLI_BINARY="$(command -v gh)"
export GITHUB_CLI_LICENSE="$(brew --prefix gh)/LICENSE"
bash scripts/package-macos.sh
```

스크립트는 빌드 → 전체 CTest → 앱 설치 → Qt/선택 gh/고지 배치 → 서명 → 번들 검사 → 배포 실행 파일의 offscreen 시작 검사 → ZIP → SHA256 순서로 진행합니다. 라이브러리 포함은 [Qt macOS 배포 문서](https://doc.qt.io/qt-6/macos-deployment.html)를 따릅니다.

출력은 `out/macos-arm64/` 또는 `out/macos-x86_64/` 아래 `GitCanvas-0.1.0-macos-<arch>.zip`, `.dmg`, `SHA256SUMS`, `build-environment.txt`입니다. 확인용 `stage.*`·`dmg.*` 폴더도 보존하며 기존 폴더를 재귀 삭제하지 않습니다.

## 서명과 공증

기본 ad hoc 서명은 개발용이며 인터넷 배포에 대한 Gatekeeper 승인을 의미하지 않습니다. Gatekeeper를 전역 해제하거나 격리 속성을 자동 제거하지 않습니다.

배포 준비가 된 Mac에서 Keychain의 Developer ID Application 인증서와 notarytool 프로필을 명시합니다.

```bash
export CODESIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export NOTARY_PROFILE='gitcanvas-notary'
bash scripts/package-macos.sh
```

프로필을 지정한 경우에만 앱 압축본을 Apple에 업로드합니다. Hardened Runtime·타임스탬프 서명 후 `Accepted` 결과를 확인하고 앱에 티켓을 staple합니다. 인증서·비밀번호를 코드에 기록하지 않으며 이번 작업에서는 인증서 사용이나 업로드를 수행하지 않았습니다. [Apple 공증 절차](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)를 참고하세요.

## CI와 검증 경계

`packaging/ci/gitcanvas.yml`에 `macos-15`(arm64), `macos-15-intel`(x86_64) 작업을 추가했습니다. 구분은 [GitHub 공식 runner 목록](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)을 기준으로 합니다. CI는 개발용 ad hoc 아티팩트를 생성하며 인증서·공증·릴리스 게시를 자동 수행하지 않습니다.

`scripts/verify-macos-bundle.py`는 Info.plist 식별자, Mach-O 형식·CPU, 번들 밖 심볼릭 링크와 개발 디렉터리의 절대 라이브러리 의존성을 검사합니다. 실제 Mac의 lipo·otool을 사용합니다. Windows에서는 모의 출력으로 검증 로직을 테스트했으며 실제 Mach-O 로딩이나 서명 검증 결과는 아닙니다.

실제 Mac에서 남은 확인:

1. 두 CPU의 CI 빌드·전체 테스트·ZIP 생성.
2. Qt 개발 환경이 없는 Mac에서 Finder 실행, Git 탐색, 한글·공백 경로, Commit/Fetch/Push·GitHub 로그인.
3. Rebase 편집기 재진입·하위 프로세스 중단·SSH·키/에이전트 동작.
4. Developer ID 서명·공증·Gatekeeper와 격리된 다운로드 파일 실행.

Windows에서는 빌드·관련 회귀, PATH 보완·번들 검증 로직, 셸 문법을 검사합니다. Mac 아티팩트 생성이나 실기기 실행으로 표시하지 않습니다.
