add_executable(Mafia1OnlineNativeRegistryTests
    native_object_registry_tests.cpp
    ../client/src/game/entities/native_object_registry.cpp
)
target_include_directories(Mafia1OnlineNativeRegistryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")

add_executable(Mafia1OnlineMeshCheckpointTests mesh_checkpoint_tests.cpp)
target_include_directories(Mafia1OnlineMeshCheckpointTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(Mafia1OnlineModAssetTests mod_asset_tests.cpp ../client/src/features/mod/mod_service.cpp)
target_include_directories(Mafia1OnlineModAssetTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")
target_link_libraries(Mafia1OnlineModAssetTests PRIVATE Framework::Framework)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(Mafia1OnlineWineFpuTests
        wine_fpu_tests.cpp
        ../client/src/features/compat/x87_nearbyint.cpp
    )
    target_include_directories(Mafia1OnlineWineFpuTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")
    target_compile_options(Mafia1OnlineWineFpuTests PRIVATE /fp:strict)
endif()

add_executable(Mafia1OnlineCarSnapshotTests car_snapshot_tests.cpp)
target_include_directories(Mafia1OnlineCarSnapshotTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineCarSnapshotTests PRIVATE Framework::Framework)

add_executable(Mafia1OnlineMissionActorTests mission_actor_tests.cpp)
target_include_directories(Mafia1OnlineMissionActorTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(Mafia1OnlineMapObjectTests
    map_object_tests.cpp
    ../client/src/features/map_object/collision_template.cpp
    ../client/src/features/map_object/static_collision.cpp
)
target_include_directories(Mafia1OnlineMapObjectTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")
target_link_libraries(Mafia1OnlineMapObjectTests PRIVATE Framework::Framework Mafia1OnlineSDK)

add_executable(Mafia1OnlinePumperReportTests pumper_report_tests.cpp)
target_include_directories(Mafia1OnlinePumperReportTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlinePumperReportTests PRIVATE Framework::Framework)

add_executable(Mafia1OnlineTaxiProtocolTests taxi_protocol_tests.cpp)
target_include_directories(Mafia1OnlineTaxiProtocolTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineTaxiProtocolTests PRIVATE Framework::Framework)

add_executable(Mafia1OnlineNetworkWireTests network_wire_tests.cpp)
target_include_directories(Mafia1OnlineNetworkWireTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineNetworkWireTests PRIVATE Framework::Framework)

# The Framework's own snapshot buffer module, when the Framework is built from
# a checkout alongside: its interpolation is what every replica here runs on.
if(FW_SOURCE_DIR)
    add_executable(Mafia1OnlineSnapshotBufferTests snapshot_buffer_tests.cpp)
    target_include_directories(Mafia1OnlineSnapshotBufferTests PRIVATE "${FW_SOURCE_DIR}/code/tests")
    target_link_libraries(Mafia1OnlineSnapshotBufferTests PRIVATE Framework::Framework)
endif()

add_executable(Mafia1OnlineContactSyncTests contact_sync_tests.cpp)
target_include_directories(Mafia1OnlineContactSyncTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineContactSyncTests PRIVATE Framework::Framework)

add_executable(Mafia1OnlineMeleeTests melee_tests.cpp)
target_include_directories(Mafia1OnlineMeleeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineMeleeTests PRIVATE Framework::Framework)

add_executable(Mafia1OnlineSettingsTests
    settings_tests.cpp
    ../client/src/features/settings/key_catalog.cpp
    ../client/src/features/settings/settings_model.cpp
)
target_include_directories(Mafia1OnlineSettingsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")
target_link_libraries(Mafia1OnlineSettingsTests PRIVATE Framework::Framework)

add_executable(Mafia1OnlineSeatRulesTests seat_rules_tests.cpp)
target_include_directories(Mafia1OnlineSeatRulesTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")

add_executable(Mafia1OnlineProcessLogTests process_log_tests.cpp)
target_include_directories(Mafia1OnlineProcessLogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineProcessLogTests PRIVATE Framework::Framework)
