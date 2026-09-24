find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(test tst_clamdclient tst_clamdwatcher)
    qt_add_executable(${test} ${test}.cpp FakeClamd.h)
    target_link_libraries(${test} PRIVATE defender_core Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
