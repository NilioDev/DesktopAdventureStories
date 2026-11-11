# Desktop packages made with CPack: a zip on Windows, a disk image on macOS
# and a tar.gz on Linux. Each holds the program (SDL is linked in), the
# licenses and the readme. The game files are never part of a package.

install(TARGETS das_app
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md" DESTINATION .)
# The fonts built into the program come with their own licenses.
install(FILES "${PROJECT_SOURCE_DIR}/assets/fonts/OFL.txt" DESTINATION . RENAME "LICENSE-FiraSans.txt")
install(FILES "${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE-PxPlus_IBM_CGA.txt" DESTINATION .)

if(WIN32)
    set(DAS_PACKAGE_SYSTEM "windows")
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(DAS_PACKAGE_SYSTEM "macos")
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(DAS_PACKAGE_SYSTEM "linux")
    set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_PACKAGE_NAME "DesktopAdventureStories")
set(CPACK_PACKAGE_VENDOR "DesktopAdventureStories")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "DesktopAdventureStories-${PROJECT_VERSION}-${DAS_PACKAGE_SYSTEM}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
include(CPack)
