find_package(Qt6 REQUIRED COMPONENTS Test)

# Certains tests ont besoin d'un bus de session D-Bus : dbus-run-session en
# démarre un privé le temps du test. Sans lui, ces tests sont ignorés (QSKIP).
find_program(DBUS_RUN_SESSION dbus-run-session)

# Tests qui ont besoin d'un bus de session D-Bus (faux UDisks2, faux systemd,
# faux serveur de notifications).
set(DBUS_TESTS tst_usbmonitor tst_onaccess tst_notifications)

foreach(test tst_clamdclient tst_clamdwatcher tst_scanjob tst_scanhistory tst_settings tst_singleinstance tst_autostart
             tst_usbmonitor tst_onaccess tst_notifications)
    qt_add_executable(${test} ${test}.cpp FakeClamd.h)
    target_link_libraries(${test} PRIVATE defender_core defender_system Qt6::Test)
    if(test IN_LIST DBUS_TESTS AND DBUS_RUN_SESSION)
        add_test(NAME ${test} COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:${test}>)
    else()
        add_test(NAME ${test} COMMAND ${test})
    endif()
endforeach()

# Dossier des fichiers de la protection en temps réel générés par CMake.
target_compile_definitions(tst_onaccess PRIVATE DEFENDER_ONACCESS_GENERATED_DIR="${DEFENDER_ONACCESS_GENERATED_DIR}")
