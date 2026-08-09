cmake_minimum_required(VERSION 3.24)

project(DK2VRPlayer VERSION 0.1.0 LANGUAGES C CXX RC)

# CMake 4 removed implicit compatibility below 3.5. Some pinned, otherwise
# unchanged upstream dependencies still declare an older policy baseline.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy for legacy dependencies" FORCE)

if(NOT WIN32)
    message(FATAL_ERROR "DK2 VR Player currently targets Windows 11 only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DK2VR_BUILD_TESTS "Build unit tests" ON)
option(DK2VR_PACKAGE_VLC_PLUGINS "Copy VLC plugins beside the build output" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
find_package(OpenGL REQUIRED)

# SDL2: window creation, display selection, input and the OpenGL context.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.11
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

# Header-only math library.
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# JSON parser used for yt-dlp's machine-readable response.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# OpenGL function loader.
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
    GIT_TAG glew-cmake-2.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glew)

# Dear ImGui is intentionally built here so its SDL2/OpenGL backends use the
# exact same SDL target as the application.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_sdl_opengl STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_sdl_opengl PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_sdl_opengl PUBLIC SDL2::SDL2-static OpenGL::GL)

# hidapi is OpenHMD's cross-platform USB HID transport.
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG hidapi-0.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(hidapi)

# OpenHMD 0.3's upstream CMake expects a system-installed hidapi. Building the
# focused DK1/DK2 driver directly keeps the application reproducible and avoids
# enabling unrelated headset drivers.
FetchContent_Declare(
    openhmd_source
    GIT_REPOSITORY https://github.com/OpenHMD/OpenHMD.git
    GIT_TAG 0.3.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake-do-not-add-upstream-project
)
FetchContent_MakeAvailable(openhmd_source)

add_library(openhmd_dk2 STATIC
    ${openhmd_source_SOURCE_DIR}/src/openhmd.c
    ${openhmd_source_SOURCE_DIR}/src/platform-win32.c
    ${openhmd_source_SOURCE_DIR}/src/platform-posix.c
    ${openhmd_source_SOURCE_DIR}/src/omath.c
    ${openhmd_source_SOURCE_DIR}/src/fusion.c
    ${openhmd_source_SOURCE_DIR}/src/shaders.c
    ${openhmd_source_SOURCE_DIR}/src/drv_dummy/dummy.c
    ${openhmd_source_SOURCE_DIR}/src/drv_oculus_rift/rift.c
    ${openhmd_source_SOURCE_DIR}/src/drv_oculus_rift/packet.c
)
target_include_directories(openhmd_dk2 PUBLIC ${openhmd_source_SOURCE_DIR}/include)
target_compile_definitions(openhmd_dk2 PUBLIC OHMD_STATIC PRIVATE DRIVER_OCULUS_RIFT)
if(TARGET hidapi::hidapi)
    target_link_libraries(openhmd_dk2 PRIVATE hidapi::hidapi)
elseif(TARGET hidapi_winapi)
    target_link_libraries(openhmd_dk2 PRIVATE hidapi_winapi)
elseif(TARGET hidapi)
    target_link_libraries(openhmd_dk2 PRIVATE hidapi)
else()
    message(FATAL_ERROR "A suitable hidapi target was not created")
endif()

# VLC's official portable Windows runtime is installed by scripts/bootstrap.ps1.
# Its stable C API is loaded dynamically, so no proprietary/binary SDK is needed.
set(DK2VR_VLC_ROOT "${CMAKE_SOURCE_DIR}/third_party/vlc" CACHE PATH "Extracted VLC root")
if(NOT EXISTS "${DK2VR_VLC_ROOT}/libvlc.dll"
    OR NOT EXISTS "${DK2VR_VLC_ROOT}/libvlccore.dll"
    OR NOT IS_DIRECTORY "${DK2VR_VLC_ROOT}/plugins")
    message(FATAL_ERROR
        "VLC runtime was not found. Run: powershell -ExecutionPolicy Bypass -File scripts/bootstrap.ps1")
endif()

set(DK2VR_SOURCES
    src/main.cpp
    src/Application.cpp
    src/HmdManager.cpp
    src/Renderer.cpp
    src/VideoPlayer.cpp
    src/VlcApi.cpp
    src/YouTubeResolver.cpp
    src/Process.cpp
    src/FileDialog.cpp
    src/Logger.cpp
    src/Projection.cpp
    resources/app.rc
)

add_executable(DK2VRPlayer WIN32 ${DK2VR_SOURCES})
# Bundled manifest enables Per-Monitor DPI awareness and Windows 11 compatibility.
set_target_properties(DK2VRPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    )
target_sources(DK2VRPlayer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest)
target_compile_definitions(DK2VRPlayer PRIVATE DK2VR_APP_VERSION="${PROJECT_VERSION}")
target_include_directories(DK2VRPlayer PRIVATE src)
target_link_libraries(DK2VRPlayer PRIVATE
    SDL2::SDL2-static
    OpenGL::GL
    libglew_static
    glm::glm
    nlohmann_json::nlohmann_json
    imgui_sdl_opengl
    openhmd_dk2
    comdlg32
    shell32
)
target_compile_definitions(DK2VRPlayer PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SDL_MAIN_HANDLED
    DK2VR_APP_VERSION="${PROJECT_VERSION}"
    DK2VR_DEV_VLC_PLUGIN_DIR="${DK2VR_VLC_ROOT}/plugins"
)

if(MSVC)
    target_compile_options(DK2VRPlayer PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(DK2VRPlayer PRIVATE /SUBSYSTEM:WINDOWS)
else()
    target_compile_options(DK2VRPlayer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(DK2VRPlayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:DK2VRPlayer>"
)

# The loader DLLs must be present before Windows can enter main(). Plugins are
# kept in third_party during development and copied only for a portable package.
add_custom_command(TARGET DK2VRPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DK2VR_VLC_ROOT}/libvlc.dll" "$<TARGET_FILE_DIR:DK2VRPlayer>/libvlc.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DK2VR_VLC_ROOT}/libvlccore.dll" "$<TARGET_FILE_DIR:DK2VRPlayer>/libvlccore.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/yt-dlp.exe" "$<TARGET_FILE_DIR:DK2VRPlayer>/yt-dlp.exe"
    VERBATIM
)
if(DK2VR_PACKAGE_VLC_PLUGINS)
    add_custom_command(TARGET DK2VRPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DK2VR_VLC_ROOT}/plugins" "$<TARGET_FILE_DIR:DK2VRPlayer>/plugins"
        VERBATIM
    )
endif()

if(DK2VR_BUILD_TESTS)
    enable_testing()
    add_executable(DK2VRCoreTests tests/CoreTests.cpp src/Projection.cpp)
    target_include_directories(DK2VRCoreTests PRIVATE src)
    target_link_libraries(DK2VRCoreTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(DK2VRCoreTests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME DK2VRCoreTests COMMAND DK2VRCoreTests)
endif()
