cmake_minimum_required(VERSION 3.10)
project(FlowMediaPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch SDL2 automatically
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.10
)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# Add executable
add_executable(FlowMediaPlayer src/main.cpp)

# Add nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Include libmpv headers
target_include_directories(FlowMediaPlayer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libmpv/include)

# Link SDL2, libmpv, OpenGL, comdlg32, setupapi (for mpv/SDL) and json
target_link_libraries(FlowMediaPlayer PRIVATE SDL2::SDL2main SDL2::SDL2 ${CMAKE_CURRENT_SOURCE_DIR}/libmpv/libmpv.dll.a opengl32 comdlg32 nlohmann_json::nlohmann_json)

# Subsystem windows so it doesn't open a console
if(WIN32)
    if(MSVC)
        set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS_RELEASE " /SUBSYSTEM:WINDOWS")
        set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS_RELWITHDEBINFO " /SUBSYSTEM:WINDOWS")
        set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS_MINSIZEREL " /SUBSYSTEM:WINDOWS")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE True)
    endif()
endif()
