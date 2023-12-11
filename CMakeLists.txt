cmake_minimum_required(VERSION 3.26)
project(hcnet VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_LIST_DIR}/share)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

add_library(hcnet INTERFACE)

target_include_directories(hcnet INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/hcnet>)

find_library(miniupnpc_library
        NAMES miniupnpc)

find_package(asio REQUIRED CONFIG)

add_dependencies(hcnet asio::asio miniupnpc)

target_link_libraries(hcnet
        INTERFACE miniupnpc_library
        INTERFACE asio::asio)

install(TARGETS hcnet
        EXPORT hcnetTargets
        INCLUDES DESTINATION include/hcnet)

install(EXPORT hcnetTargets NAMESPACE net:: DESTINATION hcnet)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ClimbingStats/ClimbingStatsConfigVersion.cmake"
        VERSION ${Upstream_VERSION}
        COMPATIBILITY AnyNewerVersion)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/hcnetConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/hcnetConfig.cmake
        INSTALL_DESTINATION hcnet)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/hcnetConfig.cmake
        DESTINATION hcnet)