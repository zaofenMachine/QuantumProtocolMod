cmake_minimum_required(VERSION 3.18)

set(TARGET QuantumCheckpoint)

add_library(
    ${TARGET}
    SHARED
    src/QuantumCheckpointMod.cpp
    src/CheckpointPersistence.cpp
)

target_include_directories(${TARGET} PRIVATE include)
target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PUBLIC UE4SS bcrypt)

set_target_properties(
    ${TARGET}
    PROPERTIES
    OUTPUT_NAME "QuantumCheckpoint"
)

add_executable(
    QuantumCheckpointPersistenceTests
    tests/CheckpointPersistenceTests.cpp
    src/CheckpointPersistence.cpp
)
target_include_directories(QuantumCheckpointPersistenceTests PRIVATE include)
target_compile_features(QuantumCheckpointPersistenceTests PRIVATE cxx_std_23)

enable_testing()
add_test(NAME QuantumCheckpointPersistence COMMAND QuantumCheckpointPersistenceTests)
