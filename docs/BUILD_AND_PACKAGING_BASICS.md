# 빌드와 무설치 패키징

현재 정책과 배포 전 확인 사항은 [무설치 배포](PORTABLE_DISTRIBUTION.md)를 따릅니다.

사용자는 운영체제에 맞는 완성된 파일을 내려받습니다. CMake·컴파일러·Qt는
개발자와 CI에서만 필요합니다. Git CLI는 사용자 환경에 별도로 필요합니다.

| 플랫폼 | 빌드 명령 | 산출물 |
| --- | --- | --- |
| Windows x64 | `powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1` | ZIP |
| macOS 15+ arm64 / x86_64 | `bash scripts/package-macos.sh` | app ZIP |
| Linux x86_64 | `bash scripts/package-linux.sh` | AppImage |

각 스크립트는 빌드·CTest·Qt 배치·시작 검사를 수행하고 앱 소스와 해시를
생성합니다. CMake의 설치형 패키지 생성기는 제거했습니다.

빌드에는 CMake 3.21+, C++20, Qt 6.3+ Widgets/Test, Python, Ninja와 Git이 필요합니다.
Windows는 MSYS2 UCRT64를 사용합니다. macOS 설정은 [macOS 안내](MACOS.md)를
따릅니다. Ubuntu 24.04에서는 `build-essential cmake ninja-build qt6-base-dev git
python3 dpkg-dev gh file patchelf`를 설치합니다. Linux 배포 도구는
`packaging/linux-tools.json`의 고정 체크섬으로 검증합니다.

독립 저장소는 `.github/workflows/portable.yml`을 사용합니다. 상위 저장소 안에
있는 프로젝트는 `packaging/ci/gitcanvas.yml`을 상위 workflow 폴더에 배치합니다.
워크플로는 빌드 산출물을 보관하며 릴리스를 공개하지 않습니다.
