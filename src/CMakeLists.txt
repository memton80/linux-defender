# Bibliothèque « cœur » : la logique métier, sans aucune dépendance à l'UI
# (QtCore + QtNetwork uniquement). Utilisée par l'application et par les tests.
add_library(defender_core STATIC
    core/ClamdClient.h
    core/ClamdClient.cpp
    core/ClamdConfig.h
    core/ClamdConfig.cpp
    core/ClamdWatcher.h
    core/ClamdWatcher.cpp
    core/ScanHistory.h
    core/ScanHistory.cpp
    core/ScanJob.h
    core/ScanJob.cpp
    core/ScanManager.h
    core/ScanManager.cpp
    core/Settings.h
    core/Settings.cpp
    core/ThreatText.h
    core/ThreatText.cpp
)
target_include_directories(defender_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(defender_core PUBLIC Qt6::Core Qt6::Network)

# Intégration au système : clés USB (UDisks2 via D-Bus), instance unique,
# démarrage automatique, notifications du bureau. Pas d'UI non plus.
add_library(defender_system STATIC
    system/Autostart.h
    system/Autostart.cpp
    system/DesktopNotifier.h
    system/DesktopNotifier.cpp
    system/OnAccessController.h
    system/OnAccessController.cpp
    system/OnAccessLog.h
    system/OnAccessLog.cpp
    system/PrivilegedHelper.h
    system/PrivilegedHelper.cpp
    system/SingleInstance.h
    system/SingleInstance.cpp
    system/SystemDiagnostics.h
    system/SystemDiagnostics.cpp
    system/UsbMonitor.h
    system/UsbMonitor.cpp
)
target_include_directories(defender_system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(defender_system PUBLIC defender_core Qt6::Core Qt6::Network Qt6::DBus)

# Protection en temps réel : nom du service systemd et chemins de ses fichiers.
# Définis ici UNE seule fois : ils servent à générer les fichiers installés
# (plus bas) et sont transmis au code (OnAccessController), qui les recherche.
set(DEFENDER_ONACCESS_SERVICE "linux-defender-onaccess.service")
set(DEFENDER_ONACCESS_CONFIG "${CMAKE_INSTALL_FULL_SYSCONFDIR}/linux-defender/clamonacc.conf")
set(DEFENDER_ONACCESS_LOG "/var/log/linux-defender/clamonacc.log") # LogsDirectory=linux-defender
target_compile_definitions(defender_system PUBLIC
    DEFENDER_ONACCESS_SERVICE="${DEFENDER_ONACCESS_SERVICE}"
    DEFENDER_ONACCESS_CONFIG="${DEFENDER_ONACCESS_CONFIG}"
    DEFENDER_ONACCESS_LOG="${DEFENDER_ONACCESS_LOG}"
)

# Application graphique (QtWidgets).
qt_add_executable(linux-defender
    main.cpp
    ui/DashboardPage.h
    ui/DashboardPage.cpp
    ui/DiagnosticsPanel.h
    ui/DiagnosticsPanel.cpp
    ui/FileActions.h
    ui/FileActions.cpp
    ui/HistoryModel.h
    ui/HistoryModel.cpp
    ui/HistoryPanel.h
    ui/HistoryPanel.cpp
    ui/MainWindow.h
    ui/MainWindow.cpp
    ui/OnAccessModel.h
    ui/OnAccessModel.cpp
    ui/OnAccessPanel.h
    ui/OnAccessPanel.cpp
    ui/ScanPanel.h
    ui/ScanPanel.cpp
    ui/ScanResultsModel.h
    ui/ScanResultsModel.cpp
    ui/SettingsDialog.h
    ui/SettingsDialog.cpp
    ui/StatusDisplay.h
    ui/StatusDisplay.cpp
    ui/TrayIcon.h
    ui/TrayIcon.cpp
    ui/Widgets.h
    ui/Widgets.cpp
)
target_compile_definitions(linux-defender PRIVATE DEFENDER_VERSION="${DEFENDER_VERSION}")
target_link_libraries(linux-defender PRIVATE defender_core defender_system Qt6::Widgets)

# Icônes SVG intégrées au binaire, accessibles via ":/icons/<fichier>.svg".
set(DATA_DIR ${PROJECT_SOURCE_DIR}/data)
qt_add_resources(linux-defender "icons"
    PREFIX "/icons"
    BASE ${DATA_DIR}/icons
    FILES
        ${DATA_DIR}/icons/linux-defender.svg
        ${DATA_DIR}/icons/result-suspicious.svg
        ${DATA_DIR}/icons/result-threat.svg
        ${DATA_DIR}/icons/result-unscanned.svg
        ${DATA_DIR}/icons/result-warning.svg
        ${DATA_DIR}/icons/status-error.svg
        ${DATA_DIR}/icons/status-ok.svg
        ${DATA_DIR}/icons/status-scanning.svg
        ${DATA_DIR}/icons/status-unknown.svg
)

# Installation, en trois composants :
# - application : binaire, fichier .desktop, icône, page de manuel ;
# - onaccess : service systemd de la protection en temps réel, configuration
#   de clamonacc et rotation de son journal ;
# - admin : programme d'aide lancé par pkexec, et son action polkit.
# onaccess et admin sont des fichiers système : paquets .deb et .rpm
# seulement, pas l'archive .tar.gz.
install(TARGETS linux-defender RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT application)
install(FILES ${DATA_DIR}/linux-defender.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT application)
install(FILES ${DATA_DIR}/icons/linux-defender.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT application)
# Page de manuel : la version y est inscrite (en-tête de « man linux-defender »).
configure_file(${DATA_DIR}/linux-defender.1.in ${CMAKE_CURRENT_BINARY_DIR}/linux-defender.1 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/linux-defender.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT application)

# Valeurs propres à chaque distribution, fixées par les paquets
# (packaging/debian/rules, packaging/rpm/linux-defender.spec). Par défaut :
# Debian et Ubuntu.
set(DEFENDER_CLAMONACC "/usr/sbin/clamonacc" CACHE FILEPATH "Chemin de clamonacc")
set(DEFENDER_CLAMD_SOCKET "/run/clamav/clamd.ctl" CACHE FILEPATH "Socket de clamd, pour clamonacc")
set(DEFENDER_CLAMD_USER "clamav" CACHE STRING "Utilisateur de clamd, exclu de l'analyse en temps réel")
set(DEFENDER_SYSTEMD_UNIT_DIR "lib/systemd/system" CACHE PATH "Dossier des services systemd (relatif au préfixe)")

set(ONACCESS_DIR ${CMAKE_CURRENT_BINARY_DIR}/onaccess)
configure_file(${DATA_DIR}/onaccess/linux-defender-onaccess.service.in ${ONACCESS_DIR}/${DEFENDER_ONACCESS_SERVICE} @ONLY)
configure_file(${DATA_DIR}/onaccess/clamonacc.conf.in ${ONACCESS_DIR}/clamonacc.conf @ONLY)
configure_file(${DATA_DIR}/onaccess/linux-defender.logrotate.in ${ONACCESS_DIR}/linux-defender.logrotate @ONLY)
# Chemins exposés aux tests, qui vérifient la cohérence fichiers générés / code.
set(DEFENDER_ONACCESS_GENERATED_DIR ${ONACCESS_DIR} PARENT_SCOPE)

install(FILES ${ONACCESS_DIR}/${DEFENDER_ONACCESS_SERVICE} DESTINATION ${DEFENDER_SYSTEMD_UNIT_DIR} COMPONENT onaccess)
install(FILES ${ONACCESS_DIR}/clamonacc.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/linux-defender COMPONENT onaccess)
install(FILES ${ONACCESS_DIR}/linux-defender.logrotate DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/logrotate.d
        RENAME linux-defender COMPONENT onaccess)

# Programme d'aide : les corrections qui demandent les droits root (page
# « Diagnostic », case de la protection en temps réel), lancées avec pkexec
# sous une action polkit dédiée. Son chemin est défini ici une seule fois : il
# est inscrit dans l'action polkit (pkexec l'exige) et transmis au code
# (PrivilegedHelper). Composant « admin » : paquets .deb et .rpm seulement.
set(DEFENDER_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/linux-defender-helper")
target_compile_definitions(defender_system PUBLIC DEFENDER_HELPER_PATH="${DEFENDER_HELPER_PATH}")
set(HELPER_DIR ${CMAKE_CURRENT_BINARY_DIR}/helper)
configure_file(${DATA_DIR}/helper/linux-defender-helper.in ${HELPER_DIR}/linux-defender-helper @ONLY)
configure_file(${DATA_DIR}/helper/io.github.memton80.linux-defender.policy.in
               ${HELPER_DIR}/io.github.memton80.linux-defender.policy @ONLY)
# Programme d'aide généré et nom du service, pour le test tests/tst_helper.sh.
set(DEFENDER_HELPER_GENERATED ${HELPER_DIR}/linux-defender-helper PARENT_SCOPE)
set(DEFENDER_ONACCESS_SERVICE ${DEFENDER_ONACCESS_SERVICE} PARENT_SCOPE)
install(PROGRAMS ${HELPER_DIR}/linux-defender-helper DESTINATION ${CMAKE_INSTALL_LIBEXECDIR} COMPONENT admin)
install(FILES ${HELPER_DIR}/io.github.memton80.linux-defender.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions COMPONENT admin)
