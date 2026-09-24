find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_clamdclient
    tst_clamdclient.cpp
)
target_link_libraries(tst_clamdclient PRIVATE dedender_core Qt6::Test)

add_test(NAME tst_clamdclient COMMAND tst_clamdclient)
