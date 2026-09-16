# 무설치 배포와 리팩터링 점검

> 배포 정책은 [무설치 배포](PORTABLE_DISTRIBUTION.md)로 변경되었습니다. 아래 설치형 패키지 설명은 과거 기록입니다.


## 배포 범위

사용자는 자신의 운영체제에 맞는 파일을 내려받아 실행합니다. 사용자 PC에서
컴파일하거나 Qt를 설치할 필요가 없습니다. 한 실행 파일이 세 운영체제에서
공통 실행되는 방식은 아니며, 개발자 또는 CI가 운영체제별 파일을 만듭니다.

| 플랫폼 | 파일 | 실행 |
| --- | --- | --- |
| Windows x64 | `GitCanvas-0.1.0-windows-x64.zip` | 전체 압축 해제 후 `GitCanvas/bin/GitCanvas.exe` |
| macOS 15+ Apple Silicon / Intel | `GitCanvas-0.1.0-macos-arm64.zip` / `x86_64.zip` | 압축 해제 후 `GitCanvas.app` |
| Linux x86_64 | `GitCanvas-0.1.0-linux-x86_64.AppImage` | 실행 권한 부여 후 실행 |

Linux 기준 빌드 환경은 Ubuntu 24.04입니다. 모든 Linux 배포판·과거 버전·ARM을
지원한다는 뜻은 아닙니다. FUSE가 없는 환경은 AppImage의
`--appimage-extract-and-run` 방식으로 실행할 수 있습니다.
MSI, DEB, TGZ, DMG 설치 패키지는 더 이상 생성하지 않습니다.

Git CLI는 기존처럼 사용자 환경에 필요합니다. SSH·Git LFS 기능에는 해당 도구가
추가로 필요합니다. 이 도구까지 포함하는 완전 독립 배포는 현재 범위가 아닙니다.
앱 설정·인증 정보는 사용자 설정 영역에 보관하므로 무설치는 무기록을 뜻하지 않습니다.

## 무료 배포와 소스

프로젝트 자체 코드는 GPL-3.0-only로 무료 사용·수정·재배포할 수 있습니다.
상세 조건은 루트 `LICENSE`, 외부 구성요소는 `THIRD_PARTY_NOTICES.md`를 따릅니다.
기존 외부 스톡 아이콘은 제거하고 프로젝트에서 생성한 두루마리로 교체했습니다.
기존 Git 이력에 남은 스톡 파일은 삭제되지 않습니다. 공개 소스 배포에는 이력을
포함하지 않는 자동 생성 소스 ZIP을 사용하거나 과거 자산의 권리를 별도 확인합니다.

모든 배포 스크립트는 `GitCanvas-0.1.0-source.zip`, SHA256, 런타임 파일 목록을
생성합니다. 소스 ZIP은 src/tests/scripts/packaging/assets/i18n/docs/licenses 및
빌드 파일을 포함하며 build/out/개인 설정/Git 이력을 제외합니다.

**바이너리 공개 전 필요한 절차:**

1. 빌드·테스트·패키지 시작 검사를 통과한 산출물만 선택합니다.
2. 바이너리와 정확히 일치하는 앱 소스 ZIP·해시를 같은 다운로드 위치에 둡니다.
3. 포함된 copyleft 라이브러리의 대응 소스를 함께 제공합니다. Windows는
   `out/windows-dependency-sources/`에 Qt·GLib·libiconv·gettext·Graphite2의
   동일 버전 원본·MSYS2 빌드 레시피·패치를 수집하고 서명을 검증합니다.
   이 폴더 전체도 공개 다운로드에 포함해야 합니다. Mac/Linux는 실제 빌드에
   사용한 버전의 대응 소스를 추가 확보해야 합니다. upstream 홈페이지 링크만으로
   외부 라이브러리 소스 제공 의무가 해결되지는 않습니다.
4. 개별 라이선스 고지와 빌드 환경 기록을 유지합니다. Git 매뉴얼 번역의 입력
   원문은 `licenses/git-manual-source.en.json`에 보존합니다.
5. 실제 대상 OS의 깨끗한 환경에서 실행·Git 작업·인증 흐름을 점검합니다.

현재 도구는 고지·앱 소스·Windows 대응 소스·구성 목록을 생성합니다.
법적 권리를 자동 인증하는 도구는 아닙니다. Mac/Linux 대응 소스 확보와
실행 검증 전에는 해당 플랫폼의 정식 공개 배포 완료로 취급하지 않습니다.

## 빌드 자동화

- Windows: `powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1`
- macOS: `bash scripts/package-macos.sh` (`QT_ROOT`, `QT_LICENSE_DIR` 지정)
- Linux: `bash scripts/package-linux.sh`
- 독립 저장소: `.github/workflows/portable.yml`
- 상위 저장소의 git-gui 폴더: `packaging/ci/gitcanvas.yml`을 상위 `.github/workflows`에 배치

빌드에는 CMake 3.21+, C++20, Qt 6.3+와 Python이 필요합니다. Linux CI는
linuxdeploy와 두 플러그인을 고정 SHA256으로 모두 검증합니다. 고정 자산을
받지 못하거나 해시가 다르면 빌드를 중단하며 이동하는 최신 버전을 실행하지 않습니다.
macOS는 기본 임시 서명이며 Developer ID 서명·공증은 별도 자격 증명이 필요합니다.
기본 CI는 빌드 결과를 보관할 뿐 외부 릴리스를 공개하지 않습니다.

## 구조 변경

- `GitCanvasCore`에서 앱과 테스트가 같은 구현·UI 번역·SSH 리소스를 공유합니다.
  소스 목록과 컴파일 중복을 제거했습니다.
- 각 기능의 저장소 변경 알림을 MainWindow의 공통 경로로 모았습니다.
  Git/PR 작업 중 요청한 새로고침은 버리지 않고 작업이 끝나면 수행합니다.
  작업 중 연속 요청은 타이머로 합치고 원격 비교 상태를 무효화합니다.
- Git 매뉴얼 번역은 앱에 링크하지 않고 별도 JSON으로 배치하여 원래 Git 문서의
  라이선스를 유지합니다. 앱 UI 번역은 기존처럼 내장합니다.
- PR 창을 닫은 뒤 저장소 상태를 재확인하여 checkout·merge 이후 화면을 갱신합니다.
- Windows는 새로운 staging 폴더에 배포하여 이전 패키지 파일 혼입을 방지합니다.
- 모든 플랫폼이 동일한 원본 아이콘과 배포 메타데이터 생성기를 사용합니다.

## 검증 경계

Windows에서 빌드·통합 테스트·무설치 시작 검사를 수행합니다. macOS 번들 검증기의
자동 테스트는 Windows에서도 실행되지만 실제 macOS 실행 검증을 대체하지 않습니다.
이 환경에는 macOS/Linux 실행 환경이 없어 해당 플랫폼 산출물은 CI 실행 후 확인해야
합니다. 세 플랫폼 지원 구성과 세 플랫폼 실기기 검증 완료를 구분합니다.

2026-09-15 Windows 최종 검사: CTest 5개 그룹 모두 통과(실패 0).
Qt 통합 테스트 41개, UI 번역 1,206문구 검사, SSH helper, macOS 번들 검증 로직,
소스 패키지·해시·셸 줄바꿈 검사를 포함합니다. 별도로 소스 ZIP 압축 해제 후
CMake 구성 성공과 Linux 배포 도구 3개의 실제 다운로드·SHA256 검증을 확인했습니다.
