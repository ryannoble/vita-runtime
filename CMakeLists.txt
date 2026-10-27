cmake_minimum_required(VERSION 3.16)
project(vita_runtime C)

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

add_library(vita_runtime
  src/path.c
  src/${VR_BACKEND}/platform.c
  src/${VR_BACKEND}/fs.c
  src/${VR_BACKEND}/input.c
)
target_include_directories(vita_runtime PUBLIC include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vita_runtime PRIVATE -Wall -Wextra -Werror)
endif()

if(PROJECT_IS_TOP_LEVEL AND NOT VITA)
  enable_testing()
  add_subdirectory(tests)
endif()
