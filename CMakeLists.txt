cmake_minimum_required(VERSION 3.16)
project(vita_runtime C)

option(VR_HOST_SDL2 "Use SDL2 for host input" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(VITA)
  set(VR_BACKEND vita)
else()
  set(VR_BACKEND host)
endif()

if(VR_BACKEND STREQUAL "vita")
  message(FATAL_ERROR "The Vita backend is not implemented yet (see src/vita/README.md)")
endif()

set(VR_INPUT_SOURCE src/${VR_BACKEND}/input.c)
if(VR_BACKEND STREQUAL "host" AND VR_HOST_SDL2)
  find_package(SDL2 REQUIRED)
  set(VR_INPUT_SOURCE src/host/input_sdl2.c)
endif()

add_library(vita_runtime
  src/path.c
  src/input_common.c
  src/${VR_BACKEND}/platform.c
  src/${VR_BACKEND}/fs.c
  ${VR_INPUT_SOURCE}
)
target_include_directories(vita_runtime PUBLIC include)
if(UNIX AND NOT APPLE)
  target_link_libraries(vita_runtime PRIVATE m)
endif()
if(VR_BACKEND STREQUAL "host" AND VR_HOST_SDL2)
  target_link_libraries(vita_runtime PUBLIC SDL2::SDL2)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vita_runtime PRIVATE -Wall -Wextra -Werror)
endif()

if(PROJECT_IS_TOP_LEVEL AND NOT VITA)
  enable_testing()
  add_subdirectory(tests)
endif()
