set(DAS_APP_SOURCES
    main.cpp
    input_bindings.cpp
    viewer.cpp
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

target_link_libraries(das_app PRIVATE das::core SDL3::SDL3)
das_set_warnings(das_app)
