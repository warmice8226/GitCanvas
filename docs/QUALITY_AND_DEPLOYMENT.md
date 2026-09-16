# 언어·진단·성능·배포 (작업 순서 11)

> 배포 정책은 [무설치 배포](PORTABLE_DISTRIBUTION.md)로 변경되었습니다. 아래 설치형 패키지 설명은 과거 기록입니다.


## 앱 설정

Workspace 아래 **앱 설정 · 진단**에서 한국어, English, 운영체제 언어를 선택합니다. 다음 실행부터 적용합니다. 기본값은 운영체제 언어이며 한국어 이외 환경은 영어를 사용합니다. History 날짜와 출력 용량은 선택한 locale로 표시합니다. 검색 날짜 입력은 `YYYY-MM-DD`를 유지합니다. 저장소 이름, 커밋 메시지, 파일 내용, Git 원문 오류는 번역하지 않습니다.

`AppLanguage`는 `QTranslator`를 사용하며 한국어 원문을 키로 하는 `i18n/en.json`을 실행 파일에 포함합니다. 별도 번역 파일 배치나 Linguist 설치가 필요하지 않습니다. 초기 설계의 `.ts/.qm` 대신 JSON 카탈로그를 사용하며 같은 원문은 모든 화면에서 같은 번역을 사용합니다. `scripts/check-translations.py`가 한국어 `tr()` 문자열 누락과 `%1` 등 인자 불일치를 검사합니다. Git 기능 판단에는 표시 문구 대신 기존 명령·객체 ID를 유지합니다. Qt 표준 버튼·파일 선택기의 번역은 플랫폼 Qt 구성에 영향을 받을 수 있습니다.

## 진단과 로그

- 작업 로그는 최근 **2,000줄**까지만 유지합니다. 버튼/메뉴 행동과 Git 명령 순서는 유지합니다.
- 알려진 토큰 접두사, HTTP 인증, URL 자격 증명, password/token 값, 이메일과 사용자 홈 경로를 가립니다. 모든 임의의 비밀 문자열을 인식하지는 않으므로 화면 로그나 스크린샷을 공유할 때는 직접 확인하세요.
- **진단 보고서 내보내기**는 JSON입니다. 앱·Qt·OS 버전, 고정된 Git 명령 종류, UI 식별자, 성공 여부, 종료 코드, 경과 시간, 오류 분류와 복구 안내만 기록합니다. 명령 인자·원격 URL·저장소 경로·계정·파일/커밋 본문·Git 출력은 넣지 않습니다.
- 보관은 기본 꺼짐입니다. 켜면 앱 데이터 폴더의 `diagnostics/recent.json`에 최대 **500건·7일**을 저장합니다. 앱 실행/기록/내보내기 시 기간을 검사합니다. 앱이 꺼진 동안 파일을 삭제하는 서비스는 없습니다. 원자적 저장을 사용하고 저장 오류는 설정 화면에 표시합니다.
- 보관 파일은 크기를 제한하고 허용된 필드로 재구성해 읽습니다. 복원된 UI 기록은 일반 식별자로 축약하고 복구 안내는 다시 생성합니다. **로그와 진단 기록 지우기**는 화면·메모리·보관 파일을 정리합니다.
- 작업 상태·복구 화면은 Git 진단과 한국어/영어 복구 안내를 구분합니다. 오류 분류는 메시지 기반의 안내이며 실제 원인을 확정하지 않습니다. 기존 Continue/Abort·복구 참조·쓰기 중단 보호는 유지합니다.

## 큰 출력과 히스토리

Git 출력은 도착할 때 읽으며 작업 상태 화면에 250ms 간격으로 수신 용량을 표시합니다. 완료 콜백에 전달할 출력은 여전히 메모리에 보관합니다. 기존 조회 8 MiB/일반 명령 32 MiB와 stderr 제한을 유지하며 무제한 스트림은 아닙니다. 신규 파일 Diff는 앞 8 MiB, 줄 선택은 기존 제한을 유지합니다.

History는 셀마다 `QTableWidgetItem`을 생성하던 구조에서 `QAbstractTableModel` + `QTableView`로 바꾸었습니다. 화면에 필요한 데이터를 요청해 표시하고, 파싱 결과와 그래프는 약 16 MiB 비용 상한의 캐시에 보관합니다. 새로고침 시 Git 조회는 다시 수행하고 출력 해시를 캐시 키로 사용합니다.

목록은 100개씩 최대 **5,000개**, 그래프는 처음 **1,000개**까지 표시합니다. 더 큰 이력은 검색으로 범위를 좁히세요. 모델에 10,000행을 넣어 마지막 행 접근/스크롤/초기화를 검사하지만 대규모 실저장소 응답 시간을 보장하는 벤치마크는 아닙니다. 다른 목록의 가상화와 Git 로그 점진 파싱은 후속 범위입니다.

## Windows

```powershell
./scripts/package-windows.ps1 -QtRoot C:\msys64\ucrt64
```

Git, MSYS2 UCRT64의 GCC·Qt6·CMake·Ninja·Python이 필요합니다. 빌드, 전체 테스트, Qt/런타임·gh 배치, 도구 PATH를 제거한 배포 실행 검사, ZIP 및 SHA256 생성을 수행합니다. 결과는 `out/GitCanvas-0.1.0-windows-x64.zip`입니다. 압축을 풀고 `GitCanvas/bin/GitCanvas.exe`를 실행합니다. 앱 설치는 필요 없지만 **Git for Windows는 별도 필요**합니다. `build-environment.txt`에 도구 버전을 남깁니다.


## Ubuntu

Ubuntu 24.04 x86_64 기준:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build qt6-base-dev git python3 dpkg-dev gh file patchelf
bash scripts/package-linux.sh
```

빌드·전체 테스트·offscreen 시작 검사 후 `out/linux`에 AppImage, 앱 소스 ZIP과 SHA256SUMS를 저장합니다. Qt 런타임과 gh를 포함하며 Git CLI는 별도로 필요합니다.

linuxdeploy와 Qt/AppImage 플러그인은 `packaging/linux-tools.json`의 고정 주소에서 받고 세 파일의 SHA256를 모두 검증합니다. 불일치하면 실행하지 않습니다.

```bash
bash scripts/package-appimage.sh
```

[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy/releases/), [Qt 플러그인](https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases), [AppImage 플러그인](https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases)의 공식 배포를 사용합니다. AppDir은 검사할 수 있도록 남깁니다.

## macOS

작업 순서 14에서 macOS 15 이상 Apple Silicon·Intel의 앱 번들, Qt 배포, ZIP, 선택적 Developer ID 서명·공증 절차를 추가했습니다. 실행 조건과 환경 변수는 [macOS 안내](MACOS.md)를 참고하세요. Windows에서 관련 코드와 검증 로직을 검사했으며 **실제 Mac 빌드·실행·서명·공증은 검증 대기**입니다.

## CI와 검증 경계

작업 순서 14에서 아래 Windows·Ubuntu 작업에 더해 macOS 두 아키텍처의 빌드·전체 테스트·ZIP 보관 작업을 추가했습니다. macOS CI의 원격 실행 결과는 아직 확인하지 않았습니다.

`packaging/ci/gitcanvas.yml`은 Windows ZIP과 Ubuntu DEB/TGZ의 빌드·전체 테스트·아티팩트 보관 워크플로입니다. 현재 Git 최상위인 `git-gui` 부모의 `.github/workflows/gitcanvas.yml`에도 연결했습니다. 변경을 GitHub에 올리면 해당 프로젝트 Push/PR 또는 수동 실행으로 시작합니다. 이번 작업에서는 Push·원격 실행·공개 릴리스를 하지 않았습니다. 독립 저장소로 분리하면 `PROJECT_DIR`와 경로 필터를 조정합니다. OS 패키지 버전은 바뀔 수 있으므로 반복 실행 가능한 절차이며 비트 단위 재현 빌드 보장은 아닙니다.

Windows 빌드·Qt 테스트·ZIP 실행 검증을 수행합니다. 이 개발 환경에는 macOS/Linux가 없어 **macOS 앱 실행, AppImage 실행과 CI 원격 실행은 검증 대기**입니다. 별도의 깨끗한 Windows PC, 코드 서명, 대규모 실저장소 성능 검증도 남아 있습니다. 설치형 패키지는 배포 범위에서 제거했습니다.

최종 검사는 `build/prototype-tests.txt`와 CTest 결과에 기록합니다. 현재 자동 검사에는 Qt 통합 테스트, 한국어 원문 1,206개의 영어 번역·인자 검사, SSH helper, macOS 번들 검증 로직과 소스 패키지·해시 검사가 포함됩니다. macOS 검증기의 단위 테스트는 실제 Mac 실행을 대신하지 않습니다.
