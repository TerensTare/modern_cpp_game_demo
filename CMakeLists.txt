
cmake_minimum_required(VERSION 3.25)

project(modern_cpp_game_demo)

find_package(entt CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

add_executable(modern_cpp_game_demo main.cpp)
target_compile_features(modern_cpp_game_demo PRIVATE cxx_std_20)
target_include_directories(modern_cpp_game_demo PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(
    modern_cpp_game_demo
    PRIVATE
    EnTT::EnTT
    imgui::imgui
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
)

add_custom_command(
    TARGET modern_cpp_game_demo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:modern_cpp_game_demo>/assets
)
