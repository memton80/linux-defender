# Bibliothèque « cœur » : la logique métier, sans aucune dépendance à l'UI
# (QtCore + QtNetwork uniquement). Utilisée par l'application et par les tests.
add_library(defender_core STATIC
    core/ClamdClient.h
    core/ClamdClient.cpp
)
target_include_directories(defender_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(defender_core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(linux-defender
    main.cpp
)
target_compile_definitions(linux-defender PRIVATE DEFENDER_VERSION="${PROJECT_VERSION}")
target_link_libraries(linux-defender PRIVATE defender_core)

install(TARGETS linux-defender RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
