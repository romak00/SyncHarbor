cmake_minimum_required(VERSION 3.20)
project(cloudsync VERSION 1.0 LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
#set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG        v0.8.3
    SOURCE_SUBDIR  cmake_unofficial
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

set(BUILD_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_BIN OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(
    nlohmann_json
    xxhash
    cpp-httplib
    watcher
)

add_executable(${PROJECT_NAME}
    main.cpp
    database.cpp
    google.cpp
    dropbox.cpp
    CallbackDispatcher.cpp
    Networking.cpp
    LocalStorage.cpp
    logger.cpp
    utils.cpp
    sync-manager.cpp
    http-server.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${watcher_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CURL::libcurl
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    Threads::Threads
    xxhash
    httplib::httplib
    wtr.hdr_watcher
)
