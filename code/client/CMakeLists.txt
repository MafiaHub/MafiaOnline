file(GLOB_RECURSE MAFIA1ONLINE_CLIENT_FEATURES CONFIGURE_DEPENDS "src/features/*/*.cpp")

add_library(Mafia1OnlineClient SHARED
    src/main.cpp
    src/core/application.cpp
    src/core/application_module.cpp
    src/core/boot/lifecycle_hooks.cpp
    src/game/entities/native_object_registry.cpp
    ../shared/register_entities.cpp
    ${MAFIA1ONLINE_CLIENT_FEATURES}
    ${CMAKE_BINARY_DIR}/mafia1online_version.cpp
)
target_include_directories(Mafia1OnlineClient PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineClient PRIVATE Framework FrameworkClient Mafia1OnlineSDK imm32)
target_link_options(Mafia1OnlineClient PRIVATE /SAFESEH:NO /MANIFEST:NO)

set(MAFIA1ONLINE_CLIENT_CONFIG_EXAMPLE_OUTPUT "${CMAKE_BINARY_DIR}/bin/config/client.example.json")
add_custom_command(OUTPUT "${MAFIA1ONLINE_CLIENT_CONFIG_EXAMPLE_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../config/client.example.json"
        "${MAFIA1ONLINE_CLIENT_CONFIG_EXAMPLE_OUTPUT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../config/client.example.json")
add_custom_target(Mafia1OnlineClientConfigExample DEPENDS "${MAFIA1ONLINE_CLIENT_CONFIG_EXAMPLE_OUTPUT}")
add_dependencies(Mafia1OnlineClient Mafia1OnlineClientConfigExample)

# The CEF front end (ui/, Preact + StyleX) is prebuilt into resources/ui by
# tools/build_ui.sh; the canonical Windows build runs that script first when
# Node is available. The client serves it from bin/ui as fw://mafia1online/.
# Without it the client relies on quick join and the native chat.
set(MAFIA1ONLINE_UI_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/ui")
add_custom_target(Mafia1OnlineUi
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${MAFIA1ONLINE_UI_OUTPUT} -DDESTINATION=${CMAKE_BINARY_DIR}/bin/ui
        -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_ui.cmake
    COMMENT "Copying the Mafia1Online web UI"
    VERBATIM)
add_dependencies(Mafia1OnlineClient Mafia1OnlineUi)
