cmake_minimum_required(VERSION 3.20)

project(hoptrace
    VERSION 1.0.0
    DESCRIPTION "A traceroute implementation for Linux and Windows"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HOPTRACE_BUILD_TESTS "Build the unit tests" ON)
option(HOPTRACE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(hoptrace_warnings INTERFACE)
if(MSVC)
    target_compile_options(hoptrace_warnings INTERFACE /W4 /permissive- /utf-8)
    if(HOPTRACE_WARNINGS_AS_ERRORS)
        target_compile_options(hoptrace_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(hoptrace_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(HOPTRACE_WARNINGS_AS_ERRORS)
        target_compile_options(hoptrace_warnings INTERFACE -Werror)
    endif()
endif()

add_library(hoptrace_core STATIC
    src/cli.cpp
    src/icmp.cpp
    src/ip_address.cpp
    src/report.cpp
    src/resolver.cpp
    src/tracer.cpp
    src/platform/socket_address.cpp)
add_library(hoptrace::core ALIAS hoptrace_core)
target_include_directories(hoptrace_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hoptrace_core PRIVATE hoptrace_warnings)

if(WIN32)
    target_sources(hoptrace_core PRIVATE src/windows/icmp_client_windows.cpp)
    target_compile_definitions(hoptrace_core PUBLIC
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(hoptrace_core PUBLIC iphlpapi ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(hoptrace_core PRIVATE src/linux/icmp_client_linux.cpp)
else()
    message(FATAL_ERROR "hoptrace supports Linux and Windows; ${CMAKE_SYSTEM_NAME} has no ICMP backend yet")
endif()

add_executable(hoptrace app/main.cpp)
target_compile_definitions(hoptrace PRIVATE HOPTRACE_VERSION="${PROJECT_VERSION}")
target_link_libraries(hoptrace PRIVATE hoptrace::core hoptrace_warnings)
if(MINGW)
    target_link_options(hoptrace PRIVATE -static)
endif()

if(HOPTRACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS hoptrace RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" HOPTRACE_PLATFORM)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${HOPTRACE_PLATFORM}")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
