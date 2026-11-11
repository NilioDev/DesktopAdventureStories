# Provides the SDL3_ttf::SDL3_ttf target, used to draw the interface text
# with our font (assets/fonts).
#
# Uses an installed SDL3_ttf when the installed SDL3 is used and it is found,
# otherwise downloads and builds the pinned release below, with its own copy
# of FreeType (no HarfBuzz or SVG support: the interface does not need them).

set(DAS_SDL3_TTF_TAG "release-3.2.2" CACHE STRING "SDL3_ttf release to build when no installed SDL3_ttf is used")

if(SDL3_FOUND)
    find_package(SDL3_ttf 3.2 CONFIG QUIET)
endif()

if(SDL3_ttf_FOUND)
    message(STATUS "Using installed SDL3_ttf ${SDL3_ttf_VERSION}")
else()
    include(FetchContent)
    message(STATUS "Building SDL3_ttf ${DAS_SDL3_TTF_TAG} from source")
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLTTF_STRICT_INSTALL OFF CACHE BOOL "" FORCE)
    # Linked into the program (also on Android, next to the shared SDL).
    set(BUILD_SHARED_LIBS OFF)
    set(SDLTTF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG ${DAS_SDL3_TTF_TAG}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "external/freetype"
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(SDL3_ttf)
endif()
