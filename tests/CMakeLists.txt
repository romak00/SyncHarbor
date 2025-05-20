find_package(Threads     REQUIRED)
find_package(SQLite3     REQUIRED)
find_package(nlohmann_json REQUIRED)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

FetchContent_MakeAvailable(googletest)

add_executable(LocalStorageTests
  LocalStorageTests.cpp
)

target_include_directories(LocalStorageTests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${watcher_SOURCE_DIR}/include
  ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(LocalStorageTests PRIVATE
  cloudsync_core
  GTest::gtest_main
  Threads::Threads
)

add_executable(DatabaseTests
  DatabaseTests.cpp
)

target_include_directories(DatabaseTests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(DatabaseTests PRIVATE
  cloudsync_core
  GTest::gtest_main
  Threads::Threads
  SQLite::SQLite3
  nlohmann_json::nlohmann_json
)

include(GoogleTest)
gtest_discover_tests(LocalStorageTests)
gtest_discover_tests(DatabaseTests)

add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS LocalStorageTests DatabaseTests
)
