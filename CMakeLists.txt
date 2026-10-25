cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
project(FaustsDeck LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FD_BUILD_APP   "Build the raylib game (needs raylib; downloaded automatically if not installed)" ON)
option(FD_BUILD_TESTS "Build the headless engine test-suite (dev_checks)" ON)
option(FD_WINDOWS_GUI "On Windows, build without a console window" ON)

# ----------------------------------------------------------------------------
# Engine: all game rules, no graphics, no raylib. Port of the Python engine/ package.
# ----------------------------------------------------------------------------
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/engine/*.cpp)
add_library(fd_engine STATIC ${ENGINE_SOURCES})
target_include_directories(fd_engine PUBLIC ${CMAKE_SOURCE_DIR}/src/engine ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
if(MSVC)
  target_compile_options(fd_engine PRIVATE /W3 /utf-8)
  target_compile_definitions(fd_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(fd_engine PRIVATE -Wall -Wextra)
endif()

# ----------------------------------------------------------------------------
# Tests: port of dev_checks.py
# ----------------------------------------------------------------------------
if(FD_BUILD_TESTS)
  enable_testing()
  add_executable(dev_checks tests/dev_checks.cpp)
  target_link_libraries(dev_checks PRIVATE fd_engine)
  if(MSVC)
    target_compile_options(dev_checks PRIVATE /W3 /utf-8)
  else()
    target_compile_options(dev_checks PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME dev_checks COMMAND dev_checks ${CMAKE_SOURCE_DIR}/data)
  add_executable(stress_test tests/stress_test.cpp)
  target_link_libraries(stress_test PRIVATE fd_engine)
  if(NOT MSVC)
    target_compile_options(stress_test PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME stress_test COMMAND stress_test ${CMAKE_SOURCE_DIR}/data 300)
endif()

# ----------------------------------------------------------------------------
# The game itself (raylib UI)
# ----------------------------------------------------------------------------
if(FD_BUILD_APP)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
      URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
    FetchContent_MakeAvailable(raylib)
  endif()

  file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/ui/*.cpp)
  add_executable(FaustsDeck src/main.cpp ${APP_SOURCES})
  target_link_libraries(FaustsDeck PRIVATE fd_engine raylib)
  target_include_directories(FaustsDeck PRIVATE ${CMAKE_SOURCE_DIR}/src/ui)
  if(MSVC)
    target_compile_options(FaustsDeck PRIVATE /W3 /utf-8)
    if(FD_WINDOWS_GUI)
      target_link_options(FaustsDeck PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
  else()
    target_compile_options(FaustsDeck PRIVATE -Wall -Wextra)
    if(MINGW)
      target_link_options(FaustsDeck PRIVATE -static -static-libgcc -static-libstdc++)
      if(FD_WINDOWS_GUI)
        target_link_options(FaustsDeck PRIVATE -mwindows)
      endif()
    endif()
  endif()

  # The game looks for data/, assets/, saves/ and reports/ next to the executable.
  add_custom_command(TARGET FaustsDeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data   $<TARGET_FILE_DIR:FaustsDeck>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:FaustsDeck>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:FaustsDeck>/saves
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:FaustsDeck>/reports)
endif()

