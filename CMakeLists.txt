cmake_minimum_required(VERSION 3.21)

project(g923mac VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64")
endif()

option(BUILD_MAC_APP "Build the macOS menu bar proxy app" ${APPLE})
option(BUILD_WINDOWS_PROXY "Build the Windows dinput8 bridge proxy" OFF)

add_compile_options(-Wall -Wextra -pedantic -Werror -fno-exceptions -fno-rtti -O3 -DUTI_RELEASE)

set(G923_WHEEL_CORE_SOURCES
    src/types.cpp
    src/utilities.cpp
    src/device.cpp
    src/command.cpp
    src/wheel.cpp
)

if(BUILD_MAC_APP)
    add_executable(G923Mac MACOSX_BUNDLE
        ${G923_WHEEL_CORE_SOURCES}
        bridge/macos/bridge_server.cpp
        bridge/macos/main.mm
    )

    target_include_directories(G923Mac PRIVATE
        include
        bridge/include
    )

    set_source_files_properties(bridge/macos/main.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(bridge/macos/Resources/G923Mac.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(G923Mac PRIVATE bridge/macos/Resources/G923Mac.icns)
    set_target_properties(G923Mac PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/bridge/macos/Info.plist.in
        MACOSX_BUNDLE_ICON_FILE G923Mac.icns
        OUTPUT_NAME G923Mac
    )

    target_link_libraries(G923Mac
        "-framework AppKit"
        "-framework CoreFoundation"
        "-framework IOKit"
    )
endif()

if(BUILD_WINDOWS_PROXY)
    add_library(g923mac_dinput8 SHARED
        bridge/windows/bridge_client.cpp
        bridge/windows/dinput8_proxy.cpp
    )

    target_include_directories(g923mac_dinput8 PRIVATE
        bridge/include
        bridge/windows/include
    )

    set_target_properties(g923mac_dinput8 PROPERTIES
        PREFIX ""
        OUTPUT_NAME dinput8
    )

    target_link_options(g923mac_dinput8 PRIVATE
        -static-libgcc
        -static-libstdc++
    )

    target_link_libraries(g923mac_dinput8
        dxguid
        ws2_32
    )
endif()
