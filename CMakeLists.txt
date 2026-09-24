cmake_minimum_required(VERSION 3.21)

# Version de l'application (--version) et des paquets. La CI la déduit du tag
# git : v1.2.3 donne 1.2.3 ; hors tag, c'est 0.0.0-dev (voir packaging/version.sh).
set(DEFENDER_VERSION "0.0.0-dev" CACHE STRING "Version de l'application")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" DEFENDER_VERSION_NUMBER "${DEFENDER_VERSION}")
if(NOT DEFENDER_VERSION_NUMBER)
    message(FATAL_ERROR "DEFENDER_VERSION doit commencer par X.Y.Z (reçu : « ${DEFENDER_VERSION} »)")
endif()

project(linux-defender
    VERSION ${DEFENDER_VERSION_NUMBER}
    DESCRIPTION "Interface graphique légère pour l'antivirus ClamAV (clamd)"
    HOMEPAGE_URL "https://github.com/memton80/linux-defender"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Release par défaut : binaire optimisé, le plus léger possible.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build" FORCE)
endif()

# Tous les exécutables (application + tests) dans build/bin/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(DEFENDER_BUILD_TESTS "Compiler les tests unitaires" ON)

# Qt 6.4 minimum : c'est la version d'Ubuntu 24.04 (cible du futur .deb).
# Fedora 44 fournit une version plus récente, compatible.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network DBus Widgets)
qt_standard_project_setup()  # active AUTOMOC et charge GNUInstallDirs

# Pas de RPATH dans le binaire installé : il n'utilise que des bibliothèques du
# système (c'est aussi ce qu'exigent les règles des paquets Debian et Fedora).
set(CMAKE_SKIP_INSTALL_RPATH ON)

add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(src)

if(DEFENDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
