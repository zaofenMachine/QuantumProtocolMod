cmake_minimum_required(VERSION 3.18)

project(QuantumProtocolMods LANGUAGES CXX)

set(
    UE4SS_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/RE-UE4SS-v3.0.1"
    CACHE PATH
    "Path to a complete RE-UE4SS v3.0.1 source checkout with submodules"
)

if(NOT EXISTS "${UE4SS_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "UE4SS_ROOT does not contain RE-UE4SS. Run scripts/Check-CppPrerequisites.ps1 "
        "and provide a complete v3.0.1 checkout with submodules."
    )
endif()

if(NOT EXISTS "${UE4SS_ROOT}/deps/first/Unreal/CMakeLists.txt")
    message(FATAL_ERROR
        "The private UEPseudo submodule is missing from UE4SS_ROOT. Link GitHub to Epic, "
        "accept the EpicGames organization invitation, then initialize UE4SS submodules."
    )
endif()

if(NOT EXISTS "${UE4SS_ROOT}/deps/first/patternsleuth/CMakeLists.txt")
    message(FATAL_ERROR
        "The patternsleuth submodule is missing from UE4SS_ROOT. Initialize UE4SS submodules."
    )
endif()

add_subdirectory("${UE4SS_ROOT}" "${CMAKE_BINARY_DIR}/RE-UE4SS")
add_subdirectory(cpp/QuantumCheckpoint)
