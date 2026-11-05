cmake_minimum_required(VERSION 3.20)
project(Zitter LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(zitter_core
  zitter/core/command_history.cc
  zitter/core/pin_tree.cc
  zitter/core/workspace_model.cc
  zitter/application/workspace_controller.cc)
target_include_directories(zitter_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(zitter_core PRIVATE /W4 /WX)
else()
  target_compile_options(zitter_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
enable_testing()
add_executable(workspace_tests tests/workspace_tests.cc)
target_link_libraries(workspace_tests PRIVATE zitter_core)
add_test(NAME workspace_contract COMMAND workspace_tests)

add_executable(product_logic_tests tests/product_logic_tests.cc)
target_link_libraries(product_logic_tests PRIVATE zitter_core)
add_test(NAME product_logic_contract COMMAND product_logic_tests)
