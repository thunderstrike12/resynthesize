cmake_minimum_required(VERSION 3.20)
project(resynthesize CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- raylib ---
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# --- raygui (header-only, just needs the include path) ---
FetchContent_Declare(
    raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG 4.0
)
FetchContent_MakeAvailable(raygui)

# --- tinyfiledialogs (header-only, just needs the include path) ---
FetchContent_Declare(
    tinyfiledialogs
    GIT_REPOSITORY https://github.com/native-toolkit/tinyfiledialogs.git
    GIT_TAG master
)
FetchContent_MakeAvailable(tinyfiledialogs)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/app/app.cpp
    src/audio/fourier.cpp
    src/audio/audio_data.cpp
    src/audio/audio_player.cpp
    src/audio/synth.cpp
    src/ui/graph.cpp
    ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c
)

target_include_directories(${PROJECT_NAME} PRIVATE ${tinyfiledialogs_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
target_include_directories(${PROJECT_NAME} PRIVATE ${raygui_SOURCE_DIR}/src)

# raylib on Windows needs these system libs
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()