cmake_minimum_required(VERSION 3.10)
project(bisca4_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BISCA4_CORE_SOURCES
    src/card.cpp
    src/gamestate.cpp
    src/eval_nnue.cpp
    src/search.cpp
    src/selfplay.cpp
    src/rand.cpp
    src_mcts/mcts.cpp
    src_mcts/selfplay_mcts.cpp
)

add_library(bisca4_core STATIC ${BISCA4_CORE_SOURCES})
target_include_directories(bisca4_core PUBLIC src src_mcts matches)

add_executable(bisca4 src/main.cpp)
target_link_libraries(bisca4 PRIVATE bisca4_core)

add_executable(bisca4_mcts src_mcts/main_mcts.cpp)
target_link_libraries(bisca4_mcts PRIVATE bisca4_core)

add_executable(bisca4_match matches/match_runner.cpp)
target_link_libraries(bisca4_match PRIVATE bisca4_core)
target_include_directories(bisca4_match PRIVATE matches)

add_library(bisca4_android SHARED src/android_api.cpp)
target_link_libraries(bisca4_android PRIVATE bisca4_core)
target_include_directories(bisca4_android PRIVATE src src_mcts matches)
