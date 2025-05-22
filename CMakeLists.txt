cmake_minimum_required(VERSION 3.20...3.31)
project(SyncHarbor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_LOGGING     "Enable custom logging"        OFF)
option(ENABLE_DEBUG_TOOLS "Enable extra debug checks"   OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ENABLE_LOGGING     ON  CACHE BOOL "" FORCE)
    set(ENABLE_DEBUG_TOOLS ON  CACHE BOOL "" FORCE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(ENABLE_LOGGING     OFF CACHE BOOL "" FORCE)
    set(ENABLE_DEBUG_TOOLS OFF CACHE BOOL "" FORCE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(ENABLE_LOGGING     ON CACHE BOOL "" FORCE)
    set(ENABLE_DEBUG_TOOLS ON  CACHE BOOL "" FORCE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_compile_definitions(
    $<$<BOOL:${ENABLE_LOGGING}>:CUSTOM_LOGGING_ENABLED=1>
    $<$<NOT:$<BOOL:${ENABLE_LOGGING}>>:CUSTOM_LOGGING_ENABLED=0>
    $<$<BOOL:${ENABLE_DEBUG_TOOLS}>:ENABLE_DEBUG_TOOLS=1>
    $<$<NOT:$<BOOL:${ENABLE_DEBUG_TOOLS}>>:ENABLE_DEBUG_TOOLS=0>
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

cmake_policy(SET CMP0077 NEW)

find_package(CURL       REQUIRED)
find_package(SQLite3    REQUIRED)
find_package(Threads    REQUIRED)


include(FetchContent)
FetchContent_Declare(nlohmann_json    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_Declare(xxhash           GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git GIT_TAG v0.8.3)
FetchContent_Declare(cpp-httplib      GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.20.0)
FetchContent_Declare(watcher          GIT_REPOSITORY https://github.com/e-dant/watcher.git GIT_TAG release)

set(BUILD_SHARED_LIBS                 OFF CACHE BOOL "" FORCE)
set(BUILD_LIB                         OFF CACHE BOOL "" FORCE)
set(BUILD_BIN                         OFF CACHE BOOL "" FORCE)
set(xxhash_BUILD_XXHSUM               OFF CACHE BOOL "Disable building xxhsum binary" FORCE)
set(xxhash_BUILD_SHARED               OFF CACHE BOOL "Disable building shared lib" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE     OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE     OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE   OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE  OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(
    nlohmann_json
    xxhash
    cpp-httplib
    watcher
)

add_library(SyncHarbor_core STATIC
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
    src/commands.cpp
)

target_include_directories(SyncHarbor_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${watcher_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${xxhash_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(SyncHarbor_core
  PUBLIC
    CURL::libcurl
    SQLite::SQLite3
    Threads::Threads
    nlohmann_json::nlohmann_json
    httplib::httplib
    wtr.hdr_watcher
)

if(APPLE)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  find_library(CORESERVICES_FRAMEWORK   CoreServices)
  target_link_libraries(SyncHarbor_core
    PUBLIC
      ${COREFOUNDATION_FRAMEWORK}
      ${CORESERVICES_FRAMEWORK}
  )
endif()

target_compile_options(SyncHarbor_core PRIVATE
    # Debug
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-Wall -Wextra -Wpedantic -fsanitize=undefined>
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:Clang>>:-Weverything -fsanitize=undefined>
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/W4 /fsanitize=undefined>
)

target_compile_options(SyncHarbor_core PRIVATE
    # RelWithDebInfo
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU>>:-Wall -Wextra -O2>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -O2>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:MSVC>>:/O2>

    # Release
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-O3 -march=native -DNDEBUG>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang>>:-O3 -march=native -DNDEBUG>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2 /DNDEBUG>
)

target_link_options(SyncHarbor_core PUBLIC
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined>
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:Clang>>:-fsanitize=undefined>
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/fsanitize=undefined>
)

add_executable(SyncHarbor src/main.cpp)
target_link_libraries(SyncHarbor PRIVATE SyncHarbor_core)

target_link_options(SyncHarbor PUBLIC
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined>
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:Clang>>:-fsanitize=undefined>
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/fsanitize=undefined>
)

enable_testing()
add_subdirectory(tests)



#include(CPack)
#set(CPACK_PACKAGE_NAME        "SyncHarbor")
#set(CPACK_PACKAGE_VENDOR      "Kolesnik Roman")
#set(CPACK_PACKAGE_DESCRIPTION "Cross-platform cloud sync tool")
#set(CPACK_PACKAGE_VERSION     ${PROJECT_VERSION})
#set(CPACK_GENERATOR           "ZIP;TGZ;NSIS;DragNDrop;DEB;RPM")

#install(
#    DIRECTORY ${CMAKE_SOURCE_DIR}/include/ 
#    DESTINATION include
#)

#install(
#    TARGETS SyncHarbor_core SyncHarbor
#    EXPORT  SyncHarborTargets
#    RUNTIME  DESTINATION bin
#    ARCHIVE  DESTINATION lib
#    INCLUDES DESTINATION include
#)

#install(
#    EXPORT   SyncHarborTargets
#    FILE     SyncHarborTargets.cmake
#    NAMESPACE SyncHarbor::
#    DESTINATION lib/cmake/SyncHarbor
#)

#include(CMakePackageConfigHelpers)
#write_basic_package_version_file(
#    "${CMAKE_CURRENT_BINARY_DIR}/SyncHarborConfigVersion.cmake"
#    VERSION ${PROJECT_VERSION}
#    COMPATIBILITY AnyNewerVersion
#)
#install(
#   FILES
#        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncHarborConfig.cmake"
#        "${CMAKE_CURRENT_BINARY_DIR}/SyncHarborConfigVersion.cmake"
#    DESTINATION lib/cmake/SyncHarbor
#)