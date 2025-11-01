cmake_minimum_required(VERSION 3.10)
project(bisca4_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

add_executable(bisca4
    src/card.cpp
    src/gamestate.cpp
    src/eval_nnue.cpp
    src/search.cpp
    src/selfplay.cpp
    src/rand.cpp
    src/main.cpp
)

target_include_directories(bisca4 PRIVATE src)

if(ANDROID)
    find_library(ANDROID_LOG_LIB log)
    if(ANDROID_LOG_LIB)
        target_link_libraries(bisca4 PRIVATE ${ANDROID_LOG_LIB})
    endif()
endif()

add_executable(bisca4_mcts
    src/card.cpp
    src/gamestate.cpp
    src/eval_nnue.cpp
    src/rand.cpp
    src_mcts/mcts.cpp
    src_mcts/selfplay_mcts.cpp
    src_mcts/main_mcts.cpp
)

target_include_directories(bisca4_mcts PRIVATE src src_mcts)

if(ANDROID)
    if(ANDROID_LOG_LIB)
        target_link_libraries(bisca4_mcts PRIVATE ${ANDROID_LOG_LIB})
    endif()
endif()

add_executable(bisca4_match
    src/card.cpp
    src/gamestate.cpp
    src/eval_nnue.cpp
    src/search.cpp
    src/rand.cpp
    src_mcts/mcts.cpp
    matches/match_runner.cpp
)

target_include_directories(bisca4_match PRIVATE src src_mcts matches)

if(ANDROID)
    if(ANDROID_LOG_LIB)
        target_link_libraries(bisca4_match PRIVATE ${ANDROID_LOG_LIB})
    endif()
endif()
