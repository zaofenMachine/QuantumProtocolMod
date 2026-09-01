cmake_minimum_required(VERSION 3.18)

project(QuantumProtocolMods LANGUAGES CXX)

include(CTest)

# Keep the standalone mod and nested dependencies under one predictable output
# tree. The build script also supplies UE4SS's custom configuration names on the
# first configure so its bundled Corrosion can resolve Rust artifact paths.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Output/$<CONFIG>/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Output/$<CONFIG>/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Output/$<CONFIG>/lib")

# Pin third-party FetchContent sources to revisions verified with UE4SS v3.0.1
# so upstream branch or tag movement cannot break this historical build (the
# first declaration of a FetchContent name wins).
include(FetchContent)
FetchContent_Declare(ImGuiTextEdit
    GIT_REPOSITORY git@github.com:UE4SS-RE/ImGuiColorTextEdit.git
    GIT_TAG af7821926251feca84e35f8fa83eee84dae90424
    GIT_PROGRESS ON
)
FetchContent_Declare(IconFontCppHeaders
    GIT_REPOSITORY git@github.com:juliettef/IconFontCppHeaders.git
    GIT_TAG 8886c5657bac22b8fee34354871e3ade2a596433
    GIT_PROGRESS ON
)
FetchContent_Declare(Tracy
    GIT_REPOSITORY git@github.com:wolfpld/tracy.git
    GIT_TAG 37aff70dfa50cf6307b3fee6074d627dc2929143
    GIT_PROGRESS ON
)

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

if(NOT EXISTS "${UE4SS_ROOT}/deps/first/patternsleuth/Cargo.toml"
   OR NOT EXISTS "${UE4SS_ROOT}/deps/first/patternsleuth_bind/Cargo.toml")
    message(FATAL_ERROR
        "The patternsleuth Rust sources are missing from UE4SS_ROOT. Initialize UE4SS submodules."
    )
endif()

add_subdirectory("${UE4SS_ROOT}" "${CMAKE_BINARY_DIR}/RE-UE4SS")
add_subdirectory(cpp/QuantumCheckpoint)
