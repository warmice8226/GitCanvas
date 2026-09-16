# Third-party notices

GitCanvas is distributed under GPL-3.0-only; see LICENSE. You may use, modify
and redistribute it, including free distribution, subject to that license.
This statement covers the project's original source and artwork to the extent
that the contributors hold rights. Third-party materials retain their licenses.

| Component | License / source |
| --- | --- |
| Qt Core, Gui, Widgets and plugins | LGPL-3.0 / GPL alternatives and third-party notices: https://www.qt.io/licensing/open-source-lgpl-obligations and https://download.qt.io/archive/qt/ |
| Git manual text and its Korean translation | Git's GPL v2 terms; see licenses/Git-for-Windows-LICENSE.txt, https://github.com/git/git and https://github.com/git-for-windows/git |
| GitHub CLI (when bundled) | MIT, https://github.com/cli/cli; matching license accompanies binary |
| MinGW runtime and Qt transitive dependencies | Individual notices copied into each platform package; exact bundled file hashes in runtime-inventory.json |
| AppImage runtime and deployment tools | https://github.com/AppImage/type2-runtime and https://github.com/linuxdeploy; deployment tools are build dependencies |

The scroll artwork in assets/gitcanvas-scroll.png and .ico was generated for
this project without a stock-image reference. Its generation prompt is retained
in assets/gitcanvas-scroll-prompt.txt. It replaces the former stock icon; the
stock icon is not part of new source or binary packages. No exclusivity or
trademark rights are asserted. Historical Git revisions may still contain the
old asset; source packages deliberately do not include Git history.

Git and optional SSH / Git LFS installed by the user are separate programs,
not included in these portable packages. Translation model and training data
are not shipped. Generated manual translations remain derivatives of Git docs,
distributed as a separate JSON data file with their original input in the source
archive. They are not relicensed under the application's GPL-3.0-only license.

## Redistributing binaries

Keep LICENSE, this notice, component notices and the matching source archive
beside binary downloads. The app source archive does not contain third-party
library sources. A publisher must also make the exact corresponding sources,
patches and build information for bundled copyleft components available as
required by their licenses; a list of upstream homepages alone is insufficient.
Record matching SDK / distribution package versions and retain source packages
for each release. The generated inventory identifies binary contents, not a
legal certification. See docs/PORTABLE_DISTRIBUTION.md for the release procedure.

Windows builds additionally collect the matching MSYS2 source archives for Qt,
GLib, libiconv, gettext and Graphite2, including upstream sources and MSYS2 build
recipes/patches. Their signatures are verified with the MSYS2 package keyring.
Publish `windows-dependency-sources/` beside the Windows ZIP and application
source ZIP. `dependency-sources.json` in the binary package records exact versions
and hashes. GCC runtime libraries are covered by their Runtime Library Exception;
the remaining bundled libraries retain the permissive notices shipped with them.
Reassess the source list whenever runtime dependencies change.
