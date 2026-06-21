cmake_minimum_required(VERSION 3.25)
project(bumpy_port VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(SDL
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG 8e37db5e797b6167f3a00d697d816a684bd259c7)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG b8acae26b0453dad6250447ad32f6b0015b162ae)
FetchContent_MakeAvailable(SDL Catch2)

add_library(bumpy_core
  src/core/indexed_framebuffer.cpp
  src/core/asset_manifest.cpp
  src/game/menu.cpp
  src/game/app.cpp
  src/resources/binary_reader.cpp
  src/resources/vec.cpp
  src/resources/menu_resources.cpp
  src/resources/sprite_frame.cpp
  src/resources/level_resources.cpp
  src/video/menu_renderer.cpp
  src/video/board_renderer.cpp)
target_include_directories(bumpy_core PUBLIC src)
target_link_libraries(bumpy_core PUBLIC bcrypt)

add_library(bumpy_platform_sdl3
  src/platform_sdl3/sdl_app.cpp)
target_include_directories(bumpy_platform_sdl3 PUBLIC src)
target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3)

add_executable(bumpy_port src/app/main.cpp)
target_link_libraries(bumpy_port PRIVATE bumpy_platform_sdl3)
add_custom_command(TARGET bumpy_port POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:bumpy_port>)

enable_testing()
add_executable(bumpy_tests
  tests/cpp/indexed_framebuffer_test.cpp
  tests/cpp/asset_manifest_test.cpp
  tests/cpp/binary_reader_test.cpp
  tests/cpp/vec_test.cpp
  tests/cpp/sprite_frame_test.cpp
  tests/cpp/menu_resources_test.cpp
  tests/cpp/menu_renderer_test.cpp
  tests/cpp/level_resources_test.cpp
  tests/cpp/board_renderer_test.cpp
  tests/cpp/menu_test.cpp
  tests/cpp/app_test.cpp)
target_link_libraries(bumpy_tests PRIVATE bumpy_core Catch2::Catch2WithMain)
add_test(NAME bumpy_tests COMMAND bumpy_tests)
set_tests_properties(bumpy_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
