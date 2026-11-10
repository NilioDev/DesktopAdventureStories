cmake_minimum_required(VERSION 3.22)

project(DesktopAdventureStories
    VERSION 0.1.0
    DESCRIPTION "Clean-room reimplementation of the Desktop Adventures engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DAS_BUILD_APP "Build the game executable (needs SDL3)" ON)
option(DAS_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(DAS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DasWarnings)

# Engine core: platform independent, no SDL. Everything that can be tested
# without a screen lives here.
add_subdirectory(src/core)

if(DAS_BUILD_APP)
    include(DasSDL3)
    add_subdirectory(src/app)
endif()

# Desktop packages for the releases: `cpack -C Release` after a build.
if(DAS_BUILD_APP AND NOT ANDROID)
    include(DasPackage)
endif()

if(DAS_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()
