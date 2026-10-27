add_executable(vr_tests test_main.c)
target_link_libraries(vr_tests PRIVATE vita_runtime)
target_include_directories(vr_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME vr_tests COMMAND vr_tests ${CMAKE_CURRENT_SOURCE_DIR}/data)
