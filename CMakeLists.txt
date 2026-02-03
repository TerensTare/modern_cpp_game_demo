
cmake_minimum_required(VERSION 3.25)

project(modern_cpp_game_demo)

find_package(entt CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

add_executable(modern_cpp_game_demo main.cpp)
target_compile_features(modern_cpp_game_demo PRIVATE cxx_std_20)
target_include_directories(modern_cpp_game_demo PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(
    modern_cpp_game_demo
    PRIVATE
    EnTT::EnTT
    imgui::imgui
    SDL3::SDL3
)
