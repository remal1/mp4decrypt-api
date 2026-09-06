cmake_minimum_required(VERSION 3.20)
project(mp4decrypt-api VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Dependency: Bento4 (via Git Submodule in third_party/bento4)
# ---------------------------------------------------------------------------
set(BENTO4_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bento4")

if(NOT EXISTS "${BENTO4_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "Bento4 submodule not found at ${BENTO4_DIR}!\n"
    "Please run:\n"
    "  git submodule update --init --recursive\n"
  )
endif()

# Configure Bento4 build options
set(BUILD_APPS OFF CACHE BOOL "Disable Bento4 CLI apps" FORCE)
set(BUILD_SHARED_LIBRARY OFF CACHE BOOL "Build Bento4 as static library" FORCE)

# Self-contained runtime for Windows MSVC (avoid dependency on external VC++ redistributable)
if(MSVC)
  set(MSVC_FORCE_STATIC_C_RUNTIME ON CACHE BOOL "Use static CRT on MSVC" FORCE)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(third_party/bento4)

# ---------------------------------------------------------------------------
# Target: mp4decrypt_api (Shared Library / DLL)
# ---------------------------------------------------------------------------
add_library(mp4decrypt_api SHARED
  src/mp4decrypt_api.cpp
  src/mp4decrypt_internal.h
  src/mp4decrypt_callback_stream.h
  include/mp4decrypt_api.h
)

target_include_directories(mp4decrypt_api
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BENTO4_DIR}/Source/C++/Core
    ${BENTO4_DIR}/Source/C++/Codecs
    ${BENTO4_DIR}/Source/C++/Crypto
    ${BENTO4_DIR}/Source/C++/MetaData
    ${BENTO4_DIR}/Source/C++/System
)

target_compile_definitions(mp4decrypt_api
  PRIVATE
    MP4DECRYPT_BUILD_SHARED=1
)

target_link_libraries(mp4decrypt_api
  PRIVATE
    ap4
)

if(MSVC AND MSVC_FORCE_STATIC_C_RUNTIME)
  set_property(TARGET mp4decrypt_api PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(mp4decrypt_api PROPERTIES
  OUTPUT_NAME "mp4decrypt_api"
  VERSION 1.0.0
  SOVERSION 1
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN 1
)
