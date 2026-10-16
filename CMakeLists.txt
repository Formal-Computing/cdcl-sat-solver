cmake_minimum_required(VERSION 3.16)
project(cdcl_sat_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sat_core
    src/DimacsParser.cpp
    src/Solver.cpp
    src/Cdcl.cpp
    src/Watched.cpp
    src/Moms.cpp
    src/Trace.cpp
)
target_include_directories(sat_core PUBLIC include)
target_compile_options(sat_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(sat_solver src/main.cpp)
target_link_libraries(sat_solver PRIVATE sat_core)

enable_testing()
add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE sat_core)
add_test(NAME dimacs_parser COMMAND test_parser)

add_executable(test_solver tests/test_solver.cpp)
target_link_libraries(test_solver PRIVATE sat_core)
add_test(NAME boolean_constraint_propagation COMMAND test_solver)

add_executable(test_dpll tests/test_dpll.cpp)
target_link_libraries(test_dpll PRIVATE sat_core)
add_test(NAME dpll_sat_search COMMAND test_dpll)

add_executable(test_cdcl tests/test_cdcl.cpp)
target_link_libraries(test_cdcl PRIVATE sat_core)
add_test(NAME cdcl_first_uip COMMAND test_cdcl)

add_executable(test_watched tests/test_watched.cpp)
target_link_libraries(test_watched PRIVATE sat_core)
add_test(NAME two_watched_literals COMMAND test_watched)

add_executable(test_moms_trace tests/test_moms_trace.cpp)
target_link_libraries(test_moms_trace PRIVATE sat_core)
add_test(NAME moms_and_solver_events COMMAND test_moms_trace)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME python_gui_bridge
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s gui -p test_app.py)
    set_tests_properties(python_gui_bridge PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
