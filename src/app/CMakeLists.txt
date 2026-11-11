set(DAS_APP_SOURCES
    main.cpp
    input_bindings.cpp
    viewer.cpp
    viewer_draw.cpp
    gui.cpp
    editor.cpp
    editor_zones.cpp
    editor_scripts.cpp
    editor_characters.cpp
    editor_puzzles.cpp
    editor_sounds.cpp
    editor_check.cpp
    editor_tiles.cpp
    ui.cpp
    settings.cpp
    touch_controls.cpp
    audio.cpp
    problem_log.cpp
)

if(ANDROID)
    # SDLActivity loads the game as libmain.so.
    add_library(das_app SHARED ${DAS_APP_SOURCES})
    set_target_properties(das_app PROPERTIES OUTPUT_NAME main)
else()
    add_executable(das_app WIN32 MACOSX_BUNDLE ${DAS_APP_SOURCES})
    set_target_properties(das_app PROPERTIES
        OUTPUT_NAME DesktopAdventureStories
        MACOSX_BUNDLE_GUI_IDENTIFIER org.desktopadventurestories.app
        MACOSX_BUNDLE_BUNDLE_NAME DesktopAdventureStories
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()

# The fonts are built into the program: Fira Sans for the interface (SIL Open
# Font License, assets/fonts/OFL.txt) and PxPlus IBM CGA for speech bubbles
# (CC BY-SA 4.0, assets/fonts/LICENSE-PxPlus_IBM_CGA.txt).
include(DasEmbed)
das_embed_file(das_app "${PROJECT_SOURCE_DIR}/assets/fonts/FiraSans-Medium.ttf" kInterfaceFont)
das_embed_file(das_app "${PROJECT_SOURCE_DIR}/assets/fonts/PxPlus_IBM_CGA.ttf" kStoryFont)

target_link_libraries(das_app PRIVATE das::core SDL3_ttf::SDL3_ttf SDL3::SDL3)
das_set_warnings(das_app)
