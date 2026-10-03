function(yac_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE yac_core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${name} PRIVATE YAC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_test(NAME ${name} COMMAND ${name})
endfunction()

yac_add_test(test_util)
