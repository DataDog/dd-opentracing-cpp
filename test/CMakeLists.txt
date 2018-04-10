macro(_datadog_test TEST_NAME)
  add_executable(${TEST_NAME} ${ARGN})
  target_link_libraries(${TEST_NAME} ${DATADOG_LINK_LIBRARIES}
                                     dd_opentracing)
  add_test(${TEST_NAME} ${TEST_NAME})
endmacro()

_datadog_test(opentracing_test opentracing_test.cpp)