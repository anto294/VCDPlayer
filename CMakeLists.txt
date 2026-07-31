cmake_minimum_required(VERSION 3.20)
project(VCDPlayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

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
    src/core/Cue.cpp
    src/core/Cdxa.cpp
    src/core/Iso9660.cpp
    src/core/VcdMeta.cpp
    src/core/PbcEngine.cpp
    src/core/DiscLoader.cpp
    src/app/DiscSession.cpp
    src/mpv/MpvPlayer.cpp
    src/mpv/VcdStreamProtocol.cpp
    src/ui/VideoWidget.cpp
    src/ui/RemotePanel.cpp
    src/ui/MainWindow.cpp
    resources.qrc
    ${APP_ICON_RESOURCE}
)

target_link_libraries(VCDPlayer PRIVATE Qt6::Widgets)

# libmpv lookup:
# - Linux/macOS: a system libmpv (distro package, Homebrew, ...) is the
#   normal way to get it, and it's discoverable via pkg-config.
# - Windows: MSYS2 packages mpv with a pkg-config file too (see README.md),
#   so this also covers a MINGW64/CLANGARM64 shell. Plain MSVC has no
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
