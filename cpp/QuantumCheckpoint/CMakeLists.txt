cmake_minimum_required(VERSION 3.18)

set(TARGET QuantumCheckpoint)

add_library(
    ${TARGET}
    SHARED
    src/QuantumCheckpointMod.cpp
    src/CheckpointPersistence.cpp
    src/PlayerRestorePlan.cpp
    src/PlayerAttackState.cpp
)

target_include_directories(${TARGET} PRIVATE include)
target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PUBLIC UE4SS bcrypt)

option(QUANTUM_CHECKPOINT_RUNTIME_TEST_FIXTURES
    "Enable destructive disposable-battle test fixtures (never for normal deployment)" OFF)
if(QUANTUM_CHECKPOINT_RUNTIME_TEST_FIXTURES)
    target_compile_definitions(${TARGET} PRIVATE QUANTUM_CHECKPOINT_RUNTIME_TEST_FIXTURES=1)
endif()

set_target_properties(
    ${TARGET}
    PROPERTIES
    OUTPUT_NAME "QuantumCheckpoint"
)

add_executable(
    QuantumCheckpointPersistenceTests
    tests/CheckpointPersistenceTests.cpp
    src/CheckpointPersistence.cpp
    src/PlayerRestorePlan.cpp
    src/PlayerAttackState.cpp
)
target_include_directories(QuantumCheckpointPersistenceTests PRIVATE include)
target_compile_features(QuantumCheckpointPersistenceTests PRIVATE cxx_std_23)

enable_testing()
add_test(NAME QuantumCheckpointPersistence COMMAND QuantumCheckpointPersistenceTests)

add_executable(QuantumCheckpointAttackStateTests tests/PlayerAttackStateTests.cpp src/PlayerAttackState.cpp)
target_include_directories(QuantumCheckpointAttackStateTests PRIVATE include)
target_compile_features(QuantumCheckpointAttackStateTests PRIVATE cxx_std_23)
add_test(NAME QuantumCheckpointAttackState COMMAND QuantumCheckpointAttackStateTests)
