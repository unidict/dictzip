add_executable(dictzip_tests main.c)

target_include_directories(dictzip_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dictzip_tests PRIVATE dictzip ZLIB::ZLIB)

add_test(NAME DictzipTest COMMAND dictzip_tests)
