find_package(Qt6 REQUIRED COMPONENTS Test)

# Le test de UsbMonitor a besoin d'un bus de session D-Bus : dbus-run-session en
# démarre un privé le temps du test. Sans lui, le test est ignoré (QSKIP).
find_program(DBUS_RUN_SESSION dbus-run-session)

foreach(test tst_clamdclient tst_clamdwatcher tst_scanjob tst_singleinstance tst_autostart tst_usbmonitor)
    qt_add_executable(${test} ${test}.cpp FakeClamd.h)
    target_link_libraries(${test} PRIVATE defender_core defender_system Qt6::Test)
    if(test STREQUAL "tst_usbmonitor" AND DBUS_RUN_SESSION)
        add_test(NAME ${test} COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:${test}>)
    else()
        add_test(NAME ${test} COMMAND ${test})
    endif()
endforeach()
