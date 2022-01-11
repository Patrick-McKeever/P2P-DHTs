project(dhts)
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

add_library(
        ${PROJECT_NAME}
        chord/abstract_chord_peer.h chord/abstract_chord_peer.cpp
        chord/chord_peer.h chord/chord_peer.cpp
        chord/remote_peer_list.h chord/remote_peer_list.cpp
        chord/remote_peer.h chord/remote_peer.cpp
        data_structures/database.h
        data_structures/finger_table.h
        data_structures/key.h
        data_structures/merkle_node.h
        data_structures/merkle_tree.h
        data_structures/thread_safe_queue.h
        data_structures/thread_safe.h
        dhash/dhash_peer.cpp dhash/dhash_peer.h
        ida/data_block.h ida/data_block.cpp
        ida/data_fragment.h ida/data_fragment.cpp
        ida/ida.h ida/ida.cpp
        ida/matrix_math.h ida/matrix_math.cpp
        networking/client.cpp networking/client.h
        networking/server.h
)

find_package( Boost 1.40 COMPONENTS program_options thread REQUIRED )
find_package(Threads)
include_directories( ${Boost_INCLUDE_DIR} )

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
target_link_libraries( ${PROJECT_NAME} ${Boost_LIBRARIES} jsoncpp_lib gtest_main )