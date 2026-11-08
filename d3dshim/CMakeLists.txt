enable_language(CXX)

add_library(vr_d3d8 src/d3d8.cpp)
set_target_properties(vr_d3d8 PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(vr_d3d8 PUBLIC include)
target_link_libraries(vr_d3d8 PUBLIC vita_runtime)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Anonymous structs in D3DMATRIX are a near-universal extension.
  target_compile_options(vr_d3d8 PRIVATE -Wall -Wextra -Werror -Wno-pedantic)
endif()

if(PROJECT_IS_TOP_LEVEL AND NOT VITA)
  add_executable(vr_d3d8_tests tests/test_d3d8.cpp)
  set_target_properties(vr_d3d8_tests PROPERTIES CXX_STANDARD 11)
  target_link_libraries(vr_d3d8_tests PRIVATE vr_d3d8)
  add_test(NAME vr_d3d8_tests COMMAND vr_d3d8_tests)
  set_tests_properties(vr_d3d8_tests PROPERTIES ENVIRONMENT VR_HEADLESS=1)
endif()
