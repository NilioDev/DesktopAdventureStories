configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/das/version.hpp" @ONLY)

add_library(das_core STATIC
    src/input.cpp
)
add_library(das::core ALIAS das_core)

target_include_directories(das_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(das_core PUBLIC cxx_std_20)
set_target_properties(das_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
das_set_warnings(das_core)
