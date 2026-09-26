add_executable(Mafia1OnlineLauncher src/main.cpp)
target_include_directories(Mafia1OnlineLauncher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineLauncher PRIVATE Framework FrameworkLoader Mafia1OnlineClient Mafia1OnlineSDK)
# Game.exe has absolute 0x400000 references and no usable relocation table.
target_link_options(Mafia1OnlineLauncher PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
