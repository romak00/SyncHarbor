cmake_minimum_required(VERSION 3.20)
project(cloudsync VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Debug)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

set(CMAKE_C_FLAGS_DEBUG   "-g -O0 ${COMMON_C_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 ${COMMON_CXX_FLAGS}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-g -O2 -DNDEBUG ${COMMON_C_FLAGS}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -O2 -DNDEBUG ${COMMON_CXX_FLAGS}")
cmake_policy(SET CMP0077 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

find_package(CURL       REQUIRED)
find_package(SQLite3    REQUIRED)
find_package(Threads    REQUIRED)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG        v0.8.3
)
FetchContent_Declare(
  cpp-httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.20.0
)
FetchContent_Declare(
  watcher
  GIT_REPOSITORY https://github.com/e-dant/watcher.git
  GIT_TAG        release
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_LIB OFF         CACHE BOOL "" FORCE)
set(BUILD_BIN OFF         CACHE BOOL "" FORCE)
set(xxhash_BUILD_XXHSUM OFF CACHE BOOL "Disable building xxhsum binary" FORCE)
set(xxhash_BUILD_SHARED OFF CACHE BOOL "Disable building shared lib" FORCE)

FetchContent_MakeAvailable(
  nlohmann_json
  xxhash
  cpp-httplib
  watcher
)

add_library(cloudsync_core STATIC
  src/database.cpp
  src/google.cpp
  src/dropbox.cpp
  src/CallbackDispatcher.cpp
  src/Networking.cpp
  src/LocalStorage.cpp
  src/logger.cpp
  src/change.cpp
  src/http-server.cpp
  src/sync-manager.cpp
)

target_include_directories(cloudsync_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${watcher_SOURCE_DIR}/include
    ${xxhash_SOURCE_DIR}
)

target_link_libraries(cloudsync_core
  PUBLIC
    CURL::libcurl
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    Threads::Threads
    httplib::httplib
    wtr.hdr_watcher
)

add_executable(${PROJECT_NAME}
  src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE cloudsync_core
)

add_subdirectory(tests)
