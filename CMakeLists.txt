cmake_minimum_required(VERSION 3.21)

project(linux-defender
    VERSION 0.1.0
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
# Étape 4 (clé USB) : ajouter DBus.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Widgets)
qt_standard_project_setup()  # active AUTOMOC et charge GNUInstallDirs

add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(src)

if(DEFENDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
