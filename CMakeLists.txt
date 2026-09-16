cmake_minimum_required(VERSION 3.21)

if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
endif()

project(GitCanvas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Git's shell prepends its own runtime DLL directory. Keep the matching MinGW
# runtime beside the executable so the headless rebase editor can start there.
if(WIN32 AND MINGW)
    get_filename_component(GITCANVAS_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(runtime libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${GITCANVAS_COMPILER_BIN}/${runtime}")
            configure_file("${GITCANVAS_COMPILER_BIN}/${runtime}" "${CMAKE_CURRENT_BINARY_DIR}/${runtime}" COPYONLY)
        endif()
    endforeach()
endif()

find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

# One compiled implementation is shared by the application and integration tests.
add_library(GitCanvasCore OBJECT
    src/mainwindow.cpp
    src/mainwindow.h
    src/github/githubclient.cpp src/github/githubclient.h
    src/github/githubpanel.cpp src/github/githubpanel.h
    src/github/pullrequests.cpp src/github/pullrequests.h
    src/github/pullrequestdialog.cpp src/github/pullrequestdialog.h
    src/git/gitclient.cpp
    src/git/gitclient.h
    src/tools/gittoolbox.h src/tools/gittoolbox.cpp src/tools/toolcatalog.cpp src/tools/toolboxcontroller.cpp src/tools/tooladvanced.cpp src/tools/gitcommandform.cpp src/tools/gitcommandform.h
    src/git/gitprocess.cpp src/git/operationstate.cpp
    src/operations/operationpanel.cpp src/operations/operationpanel.h
    src/worktree/worktreecontroller.cpp src/worktree/worktreecontroller.h
    src/worktree/worktreepanel.cpp src/worktree/worktreepanel.h
    src/history/rebaseeditor.cpp src/history/rebaseeditor.h
    src/history/rewritecontroller.cpp src/history/rewritecontroller.h
    src/history/rewritepanel.cpp src/history/rewritepanel.h
    src/git/advancedcommit.cpp src/diff/linepatch.cpp
    src/merge/mergecontroller.cpp src/merge/mergecontroller.h
    src/merge/mergepanel.cpp src/merge/mergepanel.h
    src/git/gitstatusentry.h
    src/history/historytable.cpp src/history/historytable.h
    src/settings/diagnostics.cpp src/settings/diagnostics.h
    src/settings/applanguage.cpp src/settings/applanguage.h
    src/settings/appsettings.cpp
    src/history/historywidget.cpp
    src/history/historywidget.h
    src/sync/synccontroller.cpp src/sync/synccontroller.h
    src/diff/diffwidget.cpp src/diff/diffwidget.h
    src/settings/gitsettingsdialog.cpp src/settings/gitsettingsdialog.h
    src/settings/repositorydialogs.cpp src/settings/repositorydialogs.h
    src/ui/windowchrome.h src/ui/windowchrome.cpp
    src/ui/apptheme.h src/ui/apptheme.cpp
    src/platform/platformenvironment.h src/platform/platformenvironment.cpp
    src/ssh/sshworkspace.h src/ssh/sshexecutor.cpp src/ssh/sshworkspace.cpp
)
target_link_libraries(GitCanvasCore PUBLIC Qt6::Widgets)
target_include_directories(GitCanvasCore PUBLIC src)
if(WIN32)
    target_link_libraries(GitCanvasCore PUBLIC dwmapi)
endif()
if(MSVC)
    target_compile_options(GitCanvasCore PUBLIC /utf-8)
endif()
qt_add_resources(GitCanvasCore app_languages PREFIX "/i18n" BASE "i18n" FILES "i18n/en.json")
qt_add_resources(GitCanvasCore ssh_helper PREFIX "/ssh" BASE "src/ssh" FILES "src/ssh/remote_helper.py")
qt_add_resources(GitCanvasCore git_manuals PREFIX "/i18n" BASE "i18n" FILES "i18n/git-command-summaries.ko.json")
# Git manual translations remain independently licensed data, not linked code.
configure_file(i18n/git-manuals.ko.json share/gitcanvas/git-manuals.ko.json COPYONLY)
if(APPLE)
    install(FILES i18n/git-manuals.ko.json DESTINATION GitCanvas.app/Contents/Resources)
else()
    install(FILES i18n/git-manuals.ko.json DESTINATION share/gitcanvas)
endif()
install(DIRECTORY licenses/ DESTINATION share/doc/gitcanvas/licenses)
qt_add_executable(GitCanvas src/main.cpp)
target_link_libraries(GitCanvas PRIVATE GitCanvasCore)
qt_add_resources(GitCanvas app_icon PREFIX "/app" BASE "assets" FILES "assets/gitcanvas-scroll.png")
if(WIN32)
    enable_language(RC)
    set(GITCANVAS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/gitcanvas-scroll.ico")
    configure_file(packaging/windows/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/gitcanvas-icon.rc" @ONLY)
    target_sources(GitCanvas PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gitcanvas-icon.rc")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/gitcanvas-icon.rc" APPEND PROPERTY OBJECT_DEPENDS "${GITCANVAS_ICON}")
endif()

if(MSVC)
    target_compile_options(GitCanvas PRIVATE /utf-8)
endif()

set_target_properties(GitCanvas PROPERTIES
    WIN32_EXECUTABLE ON
)
if(APPLE)
    set_target_properties(GitCanvas PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.gitcanvas.GitCanvas"
        MACOSX_BUNDLE_BUNDLE_NAME "GitCanvas"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    )
    add_custom_command(TARGET GitCanvas POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:GitCanvas>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/i18n/git-manuals.ko.json" "$<TARGET_BUNDLE_DIR:GitCanvas>/Contents/Resources/git-manuals.ko.json"
    )
endif()

install(TARGETS GitCanvas
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Qt 6.3+ deploys platform dependencies into the Windows install tree or macOS
# app bundle. The platform packaging scripts perform additional verification.
if((WIN32 OR APPLE) AND Qt6_VERSION VERSION_GREATER_EQUAL 6.3)
    qt_generate_deploy_app_script(
        TARGET GitCanvas
        OUTPUT_SCRIPT gitcanvas_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${gitcanvas_deploy_script})
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/io.github.gitcanvas.GitCanvas.desktop
        DESTINATION share/applications
    )
endif()

# Only portable packages are supported; platform scripts collect runtime dependencies.
install(FILES assets/io.github.gitcanvas.GitCanvas.png DESTINATION share/icons/hicolor/256x256/apps RENAME io.github.gitcanvas.GitCanvas.png)
install(DIRECTORY docs/ DESTINATION share/doc/gitcanvas FILES_MATCHING PATTERN "*.md" PATTERN "*.html")
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION share/doc/gitcanvas)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(GitCanvasTests tests/prototype_test.cpp)
    target_link_libraries(GitCanvasTests PRIVATE GitCanvasCore Qt6::Test)
    add_test(NAME prototype COMMAND GitCanvasTests -platform offscreen -o prototype-tests.txt,txt)
    set_tests_properties(prototype PROPERTIES TIMEOUT 900)
    if(APPLE)
        set_target_properties(GitCanvasTests PROPERTIES MACOSX_BUNDLE FALSE)
        set_tests_properties(prototype PROPERTIES TIMEOUT 900)
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME translations COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-translations.py")
    add_test(NAME ssh_helper COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/remote_helper_test.py")
    add_test(NAME macos_bundle_validation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/macos_bundle_test.py")
    add_test(NAME portable_metadata COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/portable_metadata_test.py")
    set_tests_properties(ssh_helper PROPERTIES TIMEOUT 180)
endif()
