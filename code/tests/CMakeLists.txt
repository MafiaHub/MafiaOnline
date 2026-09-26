add_executable(Mafia1OnlineNativeRegistryTests
    native_object_registry_tests.cpp
    ../client/src/game/entities/native_object_registry.cpp
)
target_include_directories(Mafia1OnlineNativeRegistryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")

add_executable(Mafia1OnlineMeshCheckpointTests mesh_checkpoint_tests.cpp)
target_include_directories(Mafia1OnlineMeshCheckpointTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(Mafia1OnlineModAssetTests mod_asset_tests.cpp ../client/src/features/mod/mod_service.cpp)
target_include_directories(Mafia1OnlineModAssetTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")
target_link_libraries(Mafia1OnlineModAssetTests PRIVATE Framework)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(Mafia1OnlineWineFpuTests
        wine_fpu_tests.cpp
        ../client/src/features/compat/x87_nearbyint.cpp
    )
    target_include_directories(Mafia1OnlineWineFpuTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../client/src")
    target_compile_options(Mafia1OnlineWineFpuTests PRIVATE /fp:strict)
endif()
