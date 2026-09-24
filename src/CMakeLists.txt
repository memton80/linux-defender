# Bibliothèque « cœur » : la logique métier, sans aucune dépendance à l'UI
# (QtCore + QtNetwork uniquement). Utilisée par l'application et par les tests.
add_library(defender_core STATIC
    core/ClamdClient.h
    core/ClamdClient.cpp
    core/ClamdWatcher.h
    core/ClamdWatcher.cpp
)
target_include_directories(defender_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(defender_core PUBLIC Qt6::Core Qt6::Network)

# Application graphique (QtWidgets).
qt_add_executable(linux-defender
    main.cpp
    ui/MainWindow.h
    ui/MainWindow.cpp
    ui/StatusDisplay.h
    ui/StatusDisplay.cpp
    ui/TrayIcon.h
    ui/TrayIcon.cpp
)
target_compile_definitions(linux-defender PRIVATE DEFENDER_VERSION="${PROJECT_VERSION}")
target_link_libraries(linux-defender PRIVATE defender_core Qt6::Widgets)

# Icônes SVG intégrées au binaire, accessibles via ":/icons/<fichier>.svg".
set(DATA_DIR ${PROJECT_SOURCE_DIR}/data)
qt_add_resources(linux-defender "icons"
    PREFIX "/icons"
    BASE ${DATA_DIR}/icons
    FILES
        ${DATA_DIR}/icons/linux-defender.svg
        ${DATA_DIR}/icons/status-ok.svg
        ${DATA_DIR}/icons/status-error.svg
        ${DATA_DIR}/icons/status-unknown.svg
)

install(TARGETS linux-defender RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${DATA_DIR}/linux-defender.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${DATA_DIR}/icons/linux-defender.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
