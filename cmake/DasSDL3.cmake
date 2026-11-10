# Provides the SDL3::SDL3 target.
#
# Uses an installed SDL3 when one is found (set DAS_USE_SYSTEM_SDL3=OFF to skip
# that), otherwise downloads and builds the pinned release below.
# The Android build passes FETCHCONTENT_SOURCE_DIR_SDL3 so that the Java glue
# code and the native library come from the same SDL checkout.

set(DAS_SDL3_TAG "release-3.4.20" CACHE STRING "SDL3 release to build when no installed SDL3 is used")
option(DAS_USE_SYSTEM_SDL3 "Prefer an installed SDL3" ON)

if(DAS_USE_SYSTEM_SDL3 AND NOT ANDROID)
    find_package(SDL3 3.2 CONFIG QUIET)
endif()

if(SDL3_FOUND)
    message(STATUS "Using installed SDL3 ${SDL3_VERSION}")
else()
    include(FetchContent)
    message(STATUS "Building SDL3 ${DAS_SDL3_TAG} from source")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    if(ANDROID)
        # SDLActivity loads libSDL3.so itself, so SDL must be a shared library.
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${DAS_SDL3_TAG}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()
