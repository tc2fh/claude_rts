cmake_minimum_required(VERSION 3.20)
project(claude_rts_sim CXX)

# CMake 4.x removed compatibility with cmake_minimum_required < 3.5;
# pass this policy so FetchContent'd deps (e.g. doctest 2.4.11) still configure.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.13.2)
FetchContent_MakeAvailable(entt)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

file(GLOB SIM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(sim STATIC ${SIM_SOURCES})
target_include_directories(sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sim PUBLIC EnTT::EnTT)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(sim_tests ${TEST_SOURCES})
target_link_libraries(sim_tests PRIVATE sim doctest::doctest)

enable_testing()
add_test(NAME sim_tests COMMAND sim_tests)
