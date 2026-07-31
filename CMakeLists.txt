cmake_minimum_required(VERSION 3.20)
project(VCDPlayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

set(MPV_SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/mpv-src)

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
)

target_include_directories(VCDPlayer PRIVATE
    ${MPV_SRC_DIR}/include
)

target_link_libraries(VCDPlayer PRIVATE
    Qt6::Widgets
    ${MPV_SRC_DIR}/build/libmpv.dll.a
)

# UI language: English is the source language written directly in the code
# (tr() calls), so it needs no .ts/.qm file. Italian is the only extra
# translation shipped; qt_add_translations runs lrelease at build time and
# embeds the resulting .qm as a Qt resource (:/i18n/vcdplayer_it.qm), so no
# extra file needs to be deployed alongside the exe.
qt_add_translations(VCDPlayer
    TS_FILES i18n/vcdplayer_it.ts
)

# libmpv-2.dll must sit next to the executable to be found at runtime.
add_custom_command(TARGET VCDPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MPV_SRC_DIR}/build/libmpv-2.dll
        $<TARGET_FILE_DIR:VCDPlayer>
)
