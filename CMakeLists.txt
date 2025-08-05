cmake_minimum_required(VERSION 3.21)

project(g923mac VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_OSX_ARCHITECTURES "x86_64")

# Compiler flags
add_compile_options(-Wall -Wextra -pedantic -Werror -fno-exceptions -fno-rtti -O3 -DUTI_RELEASE)

# Source files
set(SOURCES
    src/plugin.cpp
    src/plugin_manager.cpp
    src/types.cpp
    src/utilities.cpp
    src/device.cpp
    src/command.cpp
    src/wheel.cpp
    src/telemetry.cpp
    src/force_feedback.cpp
)

# Create the shared library
add_library(g923mac SHARED ${SOURCES})

# Include directories
target_include_directories(g923mac PUBLIC
    include
    include/scs/include
    include/scs/include/common
    include/scs/include/amtrucks
    include/scs/include/eurotrucks2
)

# Link libraries
target_link_libraries(g923mac 
    "-framework CoreFoundation"
    "-framework IOKit"
)
