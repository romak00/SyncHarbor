cmake_minimum_required(VERSION 3.20)
project(cloudsync VERSION 1.0 LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(${PROJECT_NAME}
    test.cpp
    database.cpp
    google.cpp
    dropbox.cpp
    CallbackDispatcher.cpp
    Networking.cpp
    LocalStorage.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CURL::libcurl
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
)

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)