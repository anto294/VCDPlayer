cmake_minimum_required(VERSION 3.20)
project(VCDPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VCDPLAYER_BUILD_APP "Build the Qt application (needs Qt6 and libmpv)" ON)
include(CTest)  # BUILD_TESTING, on by default

# Disc-image reading (CUE/CD-XA/ISO9660/VCD metadata) and the PBC state
# machine: plain C++17 with no Qt or mpv dependency, shared by the app and
# the unit tests.
add_library(vcdcore STATIC
    src/core/Cue.cpp
    src/core/Cdxa.cpp
    src/core/Iso9660.cpp
    src/core/VcdMeta.cpp
    src/core/PbcEngine.cpp
    src/core/DiscLoader.cpp
    src/core/MpegTiming.cpp
)
target_include_directories(vcdcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# -DVCDPLAYER_BUILD_APP=OFF builds just vcdcore and its tests, without
# needing Qt or libmpv installed (the CI's sanitizer test job does this).
if(NOT VCDPLAYER_BUILD_APP)
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL LinguistTools)

# App icon: resources.rc gives the .exe itself a native Windows icon (shown
# in Explorer/taskbar/shortcuts); resources.qrc embeds the same artwork as a
# Qt resource so the running window can set it too (QApplication::setWindowIcon
# in main.cpp), without needing to ship img/ alongside the exe.
set(APP_ICON_RESOURCE "")
if(WIN32)
    set(APP_ICON_RESOURCE resources.rc)
endif()

add_executable(VCDPlayer WIN32
    src/main.cpp
    src/app/DiscSession.cpp
    src/app/SleepInhibitor.cpp
    src/mpv/MpvPlayer.cpp
    src/mpv/VcdStreamProtocol.cpp
    src/ui/VideoWidget.cpp
    src/ui/RemotePanel.cpp
    src/ui/MainWindow.cpp
    resources.qrc
    ${APP_ICON_RESOURCE}
)

target_link_libraries(VCDPlayer PRIVATE vcdcore Qt6::Widgets Qt6::OpenGL)
# SleepInhibitor (keeps the screen on during playback): D-Bus on Linux/BSD,
# an IOKit power assertion on macOS, plain Win32 on Windows.
if(APPLE)
    target_link_libraries(VCDPlayer PRIVATE "-framework IOKit" "-framework CoreFoundation")
elseif(UNIX)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(VCDPlayer PRIVATE Qt6::DBus)
    # MPRIS: media keys, the desktop's media widget, playerctl, ...
    target_sources(VCDPlayer PRIVATE src/app/Mpris.cpp)
    target_compile_definitions(VCDPlayer PRIVATE VCDPLAYER_HAVE_MPRIS)
endif()
# Single source of the version number (project() above): shown in the
# About dialog and set as QCoreApplication::applicationVersion().
target_compile_definitions(VCDPlayer PRIVATE VCDPLAYER_VERSION="${PROJECT_VERSION}")

# libmpv lookup:
# - Linux/macOS: a system libmpv (distro package, Homebrew, ...) is the
#   normal way to get it, and it's discoverable via pkg-config.
# - Windows: MSYS2 packages mpv with a pkg-config file too (see README.md),
#   so this also covers a UCRT64/CLANGARM64 shell. Plain MSVC has no
#   pkg-config, so it falls back to a source checkout + local meson build
#   under vendor/mpv-src instead — also the fallback on any platform where
#   pkg-config can't find a system libmpv.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MPV QUIET mpv)
endif()

if(MPV_FOUND)
    target_include_directories(VCDPlayer PRIVATE ${MPV_INCLUDE_DIRS})
    target_link_directories(VCDPlayer PRIVATE ${MPV_LIBRARY_DIRS})
    target_link_libraries(VCDPlayer PRIVATE ${MPV_LIBRARIES})
else()
    set(MPV_SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/mpv-src)
    target_include_directories(VCDPlayer PRIVATE ${MPV_SRC_DIR}/include)
    if(WIN32)
        target_link_libraries(VCDPlayer PRIVATE ${MPV_SRC_DIR}/build/libmpv.dll.a)
    else()
        target_link_libraries(VCDPlayer PRIVATE ${MPV_SRC_DIR}/build/libmpv.so)
        set_target_properties(VCDPlayer PROPERTIES
            BUILD_RPATH ${MPV_SRC_DIR}/build
        )
    endif()
endif()

# UI language: English is the source language written directly in the code
# (tr() calls), so it needs no .ts/.qm file. Italian is the only extra
# translation shipped; qt_add_translations runs lrelease at build time and
# embeds the resulting .qm as a Qt resource (:/i18n/vcdplayer_it.qm), so no
# extra file needs to be deployed alongside the exe.
qt_add_translations(VCDPlayer
    TS_FILES i18n/vcdplayer_it.ts
)

# Windows has no rpath/ldconfig equivalent: when using the vendored build,
# libmpv-2.dll must be copied next to the executable to be found at runtime.
if(WIN32 AND NOT MPV_FOUND)
    add_custom_command(TARGET VCDPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MPV_SRC_DIR}/build/libmpv-2.dll
            $<TARGET_FILE_DIR:VCDPlayer>
    )
endif()

# Bundled GLSL user shaders (Experimental > AI upscaling — see
# shaders/README.md) are plain files mpv reads by path at runtime, not Qt
# resources: mpv has no notion of Qt's :/ resource paths. Copied next to the
# executable on every platform so applicationDirPath()+"/shaders" always
# resolves the same way in a dev build and in a packaged one (see
# ci/package-windows.sh / ci/package-appimage.sh for the latter).
add_custom_command(TARGET VCDPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:VCDPlayer>/shaders
)

# Installed layout for system packages (.deb, Flatpak): binary in bin/,
# shaders in share/vcdplayer/shaders (where the app also looks, next to
# its executable's ../share), desktop entry with the .cue association,
# AppStream metadata and the icon set, all under the reverse-DNS app id.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(VCDPLAYER_APP_ID io.github.anto294.VCDPlayer)
    install(TARGETS VCDPlayer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/vcdplayer/shaders)
    install(FILES packaging/linux/${VCDPLAYER_APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/${VCDPLAYER_APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 16 32 48 64 128 256 512)
        install(FILES img/icon/${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME ${VCDPLAYER_APP_ID}.png)
    endforeach()

    # `cpack -G DEB` in a configured build tree makes the .deb (CI builds it
    # on Ubuntu 24.04, so it targets Ubuntu 24.04+ / Debian 13+). Library
    # dependencies are worked out by dpkg-shlibdeps; the Qt platform plugins
    # are loaded at runtime, so shlibdeps can't see them and they're added
    # by hand.
    set(CPACK_PACKAGE_NAME vcdplayer)
    set(CPACK_PACKAGE_VENDOR anto294)
    set(CPACK_PACKAGE_CONTACT "anto294 <anto294@users.noreply.github.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Video CD player with full PBC menu support")
    string(CONCAT CPACK_DEBIAN_PACKAGE_DESCRIPTION
        "Plays Video CD disc images (.cue/.bin) like a hardware VCD player:\n"
        "it follows the disc's own menus (PBC, Playback Control) through an\n"
        "on-screen remote control, and also offers a chapter list, resume,\n"
        "karaoke channel selection and optional GPU upscaling shaders.")
    set(CPACK_DEBIAN_PACKAGE_SECTION video)
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE https://github.com/anto294/VCDPlayer)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_STRIP_FILES ON)
    include(CPack)
endif()
