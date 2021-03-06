cmake_minimum_required(VERSION 3.19)
project(chord_and_dhash)
set(CMAKE_CXX_STANDARD 20)

option(BUILD_TESTING "Build unit tests." OFF)

add_subdirectory(src)

# Run tests if option is specified from command line
if(BUILD_TESTING)
    add_executable(
        chord_and_dhash
        test/main.cpp
        test/chord_test.cpp
        test/dhash_test.cpp
        test/information_dispersal_test.cc
        test/json_reader.cpp
        test/json_reader.h
        test/key_test.cc
        test/merkle_tree_test.cc
        test/server_test.cpp
    )
endif()


target_link_libraries(chord_and_dhash dhts)