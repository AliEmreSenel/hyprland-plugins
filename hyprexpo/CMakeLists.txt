cmake_minimum_required(VERSION 3.27)

project(hyprexpo DESCRIPTION "hyprexpo plugin for Hyprland" VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(hyprexpo SHARED main.cpp overview.cpp ExpoGesture.cpp OverviewPassElement.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland libdrm libinput libudev pangocairo pixman-1 wayland-server xkbcommon
)
target_link_libraries(hyprexpo PRIVATE PkgConfig::deps)
install(TARGETS hyprexpo)

include(CTest)
if(BUILD_TESTING)
    add_executable(grid-test tests/grid.cpp)
    add_test(NAME grid COMMAND grid-test)
    add_executable(preview-test tests/preview.cpp)
    add_test(NAME workspace-preview COMMAND preview-test)
endif()
