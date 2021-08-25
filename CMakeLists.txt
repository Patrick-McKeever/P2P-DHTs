cmake_minimum_required(VERSION 3.19)
project(chord_and_dhash)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
FetchContent_Declare(
        JsonCpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
)
FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(JsonCpp)

add_executable(chord_and_dhash main.cpp src/networking/server.h src/networking/client.cpp src/networking/client.h)

find_package( Boost 1.40 COMPONENTS program_options thread REQUIRED )
find_package(Threads)
include_directories( ${Boost_INCLUDE_DIR} )

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

target_link_libraries( chord_and_dhash ${Boost_LIBRARIES} jsoncpp_lib gtest_main )