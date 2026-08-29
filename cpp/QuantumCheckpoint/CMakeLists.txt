cmake_minimum_required(VERSION 3.18)

set(TARGET QuantumCheckpoint)

add_library(
    ${TARGET}
    SHARED
    src/QuantumCheckpointMod.cpp
)

target_include_directories(${TARGET} PRIVATE include)
target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PUBLIC UE4SS)

set_target_properties(
    ${TARGET}
    PROPERTIES
    OUTPUT_NAME "QuantumCheckpoint"
)
