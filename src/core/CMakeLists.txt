configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/das/version.hpp" @ONLY)

add_library(das_core STATIC
    src/input.cpp
    src/data/binary.cpp
    src/data/container.cpp
    src/data/game_data.cpp
    src/data/game_files.cpp
    src/data/palette.cpp
    src/data/zone_records.cpp
    src/data/character.cpp
    src/data/puzzle.cpp
    src/world/generator.cpp
    src/world/yoda_generator.cpp
    src/world/zone_rules.cpp
    src/world/combat.cpp
    src/world/indy_generator.cpp
    src/world/yoda_layout.cpp
    src/world/world.cpp
    src/world/scripts.cpp
    src/script/opcodes.cpp
    src/util/json.cpp
    src/util/png.cpp
    src/util/text.cpp
    src/util/zlib.cpp
    src/pack/pack.cpp
    src/pack/import.cpp
    src/util/zip.cpp
)
add_library(das::core ALIAS das_core)

target_include_directories(das_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(das_core PUBLIC cxx_std_20)
set_target_properties(das_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
das_set_warnings(das_core)
