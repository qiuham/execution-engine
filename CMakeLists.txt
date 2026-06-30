cmake_minimum_required(VERSION 3.20)
project(execution_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(exec_engine
    src/adapter/sim_adapter.cpp
    src/engine/execution_engine.cpp
    src/planner/basket_target_collection.cpp
    src/planner/phase_planner.cpp
    src/state/execution_state_view.cpp
)

target_include_directories(exec_engine PUBLIC include)

target_compile_features(exec_engine PUBLIC cxx_std_20)

add_executable(execution_engine apps/execution_node/main.cpp)
target_link_libraries(execution_engine PRIVATE exec_engine)
