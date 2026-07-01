cmake_minimum_required(VERSION 3.20)
project(execution_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(exec_engine
    src/adapter/sim_adapter.cpp
    src/engine/execution_engine.cpp
    src/oms/order_store.cpp
    src/planner/basket_target_collection.cpp
    src/planner/phase_planner.cpp
    src/pricing/child_order_pricer.cpp
    src/state/execution_state_view.cpp
)

target_include_directories(exec_engine PUBLIC include)

target_compile_features(exec_engine PUBLIC cxx_std_20)

add_executable(execution_engine apps/execution_node/main.cpp)
target_link_libraries(execution_engine PRIVATE exec_engine)

enable_testing()

function(add_exec_engine_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE exec_engine)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_exec_engine_test(exec_engine_async_test tests/async_order_flow_test.cpp)
add_exec_engine_test(exec_engine_order_state_machine_test tests/order_state_machine_test.cpp)
add_exec_engine_test(exec_engine_order_store_test tests/order_store_test.cpp)
add_exec_engine_test(exec_engine_state_view_test tests/execution_state_view_test.cpp)
add_exec_engine_test(exec_engine_pricing_test tests/pricing_test.cpp)
add_exec_engine_test(exec_engine_phase_planner_test tests/phase_planner_test.cpp)
add_exec_engine_test(exec_engine_cancel_flow_test tests/cancel_flow_test.cpp)
