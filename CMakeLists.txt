cmake_minimum_required(VERSION 3.14)
project(libdictzip VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- Options ----------
option(DICTZIP_BUILD_TESTS "Build tests" ON)
option(BUILD_SHARED_LIBS "Build shared library" OFF)

# ---------- Dependencies ----------
find_package(ZLIB REQUIRED)

# ---------- Library ----------
file(GLOB DICTZIP_SOURCES src/*.c)
file(GLOB DICTZIP_HEADERS src/*.h)

set(DICTZIP_PUBLIC_HEADERS
    src/dictzip.h
)

add_library(dictzip ${DICTZIP_SOURCES})
target_include_directories(dictzip
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
)
target_link_libraries(dictzip PUBLIC ZLIB::ZLIB)

if(MSVC)
    target_compile_definitions(dictzip PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(dictzip PRIVATE /W4)
else()
    target_compile_options(dictzip PRIVATE -Wall -Wextra -pedantic)
endif()

# ---------- Tests ----------
if(DICTZIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------- Install ----------
include(GNUInstallDirs)

install(TARGETS dictzip
    EXPORT dictzipTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${DICTZIP_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dictzip)

install(EXPORT dictzipTargets
    FILE dictzipTargets.cmake
    NAMESPACE dictzip::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dictzip
)
