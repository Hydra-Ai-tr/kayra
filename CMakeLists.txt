cmake_minimum_required(VERSION 3.20)
project(Kayra VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Kayra 0.1 targets Windows 10/11 (SAPI voices, WinHTTP, Core Audio).")
endif()

option(KAYRA_BUILD_GUI "Build the Kayra desktop HUD" ON)
option(KAYRA_WHISPER "Speech recognition with whisper.cpp" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(MSVC)
  add_compile_options(/utf-8 /W3)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
include(FetchContent)
# nlohmann/json 3.11.3 (MIT), vendored as its single header.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

# ------------------------------------------------------------ speech to text
if(KAYRA_WHISPER)
  set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
  # Portable binaries: AVX2/FMA (every x86-64 CPU since ~2013), not -march=native.
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  set(GGML_AVX ON CACHE BOOL "" FORCE)
  set(GGML_AVX2 ON CACHE BOOL "" FORCE)
  set(GGML_FMA ON CACHE BOOL "" FORCE)
  set(GGML_F16C ON CACHE BOOL "" FORCE)
  FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.7.6
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(whisper)
endif()

# ---------------------------------------------------------------- core
add_library(kayracore STATIC
  src/core/util.cpp
  src/core/config.cpp
  src/core/http.cpp
  src/core/calc.cpp
  src/core/i18n.cpp
  src/core/system.cpp
  src/core/apps.cpp
  src/core/memory.cpp
  src/core/tools.cpp
  src/core/intent.cpp
  src/core/llm.cpp
  src/core/assistant.cpp
  src/voice/tts.cpp
  src/voice/mic.cpp
  src/voice/stt.cpp
  src/voice/listener.cpp)
target_include_directories(kayracore PUBLIC src)
target_compile_definitions(kayracore PUBLIC KAYRA_VERSION="${PROJECT_VERSION}"
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(kayracore PUBLIC nlohmann_json::nlohmann_json
  winhttp ole32 oleaut32 uuid sapi winmm gdiplus gdi32 user32 shell32 shlwapi advapi32 powrprof)
if(NOT MSVC)
  target_compile_options(kayracore PRIVATE -Wall -Wextra -Wno-unknown-pragmas -Wno-missing-field-initializers)
endif()
if(KAYRA_WHISPER)
  target_link_libraries(kayracore PUBLIC whisper)
  target_compile_definitions(kayracore PUBLIC KAYRA_HAVE_WHISPER)
endif()

add_executable(kayra-cli src/cli/main.cpp)
target_link_libraries(kayra-cli PRIVATE kayracore)
if(MINGW)
  target_link_options(kayra-cli PRIVATE -static)
endif()

enable_testing()
add_test(NAME selftest COMMAND kayra-cli selftest)

# ----------------------------------------------------------------- HUD
if(KAYRA_BUILD_GUI)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw imgui)
  find_package(OpenGL REQUIRED)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
  if(NOT MSVC)
    target_compile_options(imgui PRIVATE -w)
  endif()

  add_executable(kayra WIN32 src/app/main.cpp src/app/hud.cpp src/app/record.cpp assets/kayra.rc)
  target_link_libraries(kayra PRIVATE kayracore imgui dwmapi)
  if(NOT MSVC)
    target_compile_options(kayra PRIVATE -Wall -Wextra)
  endif()
  if(MSVC)
    target_link_options(kayra PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    target_link_options(kayra PRIVATE -static)
  endif()
  add_dependencies(kayra kayra-cli)
endif()
