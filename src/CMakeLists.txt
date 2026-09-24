# Bibliothèque « cœur » : la logique métier, sans aucune dépendance à l'UI
# (QtCore + QtNetwork uniquement). Utilisée par l'application et par les tests.
add_library(dedender_core STATIC
    core/ClamdClient.h
    core/ClamdClient.cpp
)
target_include_directories(dedender_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dedender_core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(linux-dedender
    main.cpp
)
target_compile_definitions(linux-dedender PRIVATE DEDENDER_VERSION="${PROJECT_VERSION}")
target_link_libraries(linux-dedender PRIVATE dedender_core)

install(TARGETS linux-dedender RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
