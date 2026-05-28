cmake_minimum_required(VERSION 3.21)
project(VkP2Template VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(sdl3_src
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.4
)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_Declare(VkP2
    GIT_REPOSITORY https://github.com/AsperTheDog/VkP2.git
    GIT_TAG main
)

FetchContent_MakeAvailable(sdl3_src)
FetchContent_MakeAvailable(spdlog)
FetchContent_MakeAvailable(VkP2)

add_subdirectory(libs/SDL3)
add_subdirectory(libs/ImGui)
add_subdirectory(app)
