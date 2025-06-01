find_package(Threads     REQUIRED)
find_package(SQLite3     REQUIRED)
find_package(CURL        REQUIRED)
find_package(OpenSSL     REQUIRED)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

set(LS_UNIT_DB_SRCS
    unit/database/CloudConfigTests.cpp
    unit/database/FileTableTests.cpp
    unit/database/FileLinkTests.cpp
    unit/database/MiscDatabaseTests.cpp
)

add_executable(DatabaseUnitTests ${LS_UNIT_DB_SRCS})
target_include_directories(DatabaseUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit
)
target_link_libraries(DatabaseUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
        SQLite::SQLite3
)
include(GoogleTest)
gtest_discover_tests(DatabaseUnitTests
    PROPERTIES LABELS "unit-db"
)

set(LS_UNIT_LOCAL_SRCS
    unit/local-storage/LocalStorageSimpleTests.cpp
    unit/local-storage/LocalStoragePathTests.cpp
    unit/local-storage/LocalStorageFsEventTests.cpp
    unit/local-storage/LocalStorageCloudTests.cpp
)

add_executable(LocalStorageUnitTests ${LS_UNIT_LOCAL_SRCS})
target_include_directories(LocalStorageUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit
    ${watcher_SOURCE_DIR}/include
)
target_link_libraries(LocalStorageUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
        SQLite::SQLite3
)
include(GoogleTest)
gtest_discover_tests(LocalStorageUnitTests
    PROPERTIES LABELS "unit-local_storage"
)

add_executable(HttpServerUnitTests 
    unit/HttpServerUnitTests.cpp
)
target_include_directories(HttpServerUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(HttpServerUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
)
include(GoogleTest)
gtest_discover_tests(HttpServerUnitTests
    PROPERTIES LABELS "unit-http-server"
)


add_executable(CallbackDispatcherUnitTests 
    unit/CallbackDispatcherUnitTests.cpp
)
target_include_directories(CallbackDispatcherUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit/database
)
target_link_libraries(CallbackDispatcherUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
)
include(GoogleTest)
gtest_discover_tests(CallbackDispatcherUnitTests
    PROPERTIES LABELS "unit-callbackdispatcher"
)


add_executable(RequestHandleUnitTests 
    unit/RequestHandleUnitTests.cpp
)
target_include_directories(RequestHandleUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(RequestHandleUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
        CURL::libcurl
)
include(GoogleTest)
gtest_discover_tests(RequestHandleUnitTests
    PROPERTIES LABELS "unit-request-handle"
)


add_executable(ActiveCountUnitTests
    unit/ActiveCountUnitTests.cpp
)
target_include_directories(ActiveCountUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit
)
target_link_libraries(ActiveCountUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
)
include(GoogleTest)
gtest_discover_tests(ActiveCountUnitTests
    PROPERTIES LABELS "unit-active-count"
)


add_executable(EventRegistryUnitTests
    unit/EventRegistryUnitTests.cpp
)
target_include_directories(EventRegistryUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit
)
target_link_libraries(EventRegistryUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
)
include(GoogleTest)
gtest_discover_tests(EventRegistryUnitTests
    PROPERTIES LABELS "unit-event-registry"
)



add_executable(UtilsUnitTests
    unit/UtilsUnitTests.cpp
)
target_include_directories(UtilsUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit
)
target_link_libraries(UtilsUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
)
include(GoogleTest)
gtest_discover_tests(UtilsUnitTests
    PROPERTIES LABELS "unit-utils"
)


add_executable(SyncManagerUnitTests
    unit/SyncManagerUnitTests.cpp
)
target_include_directories(SyncManagerUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/unit
)
target_link_libraries(SyncManagerUnitTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
)
include(GoogleTest)
gtest_discover_tests(SyncManagerUnitTests
    PROPERTIES LABELS "unit-sync-manager"
)


add_executable(HttpClientIntegrationTests
    integration/HttpClientIntegrationTests.cpp
)
target_include_directories(HttpClientIntegrationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/integration
)
target_link_libraries(HttpClientIntegrationTests
    PRIVATE
        SyncHarbor_core
        GTest::gtest_main
        Threads::Threads
        CURL::libcurl
)
include(GoogleTest)
gtest_discover_tests(HttpClientIntegrationTests
    PROPERTIES LABELS "integration-http"
)

add_executable(LocalStorageIntegrationTests
    integration/LocalStorageIntegrationTests.cpp
)
target_include_directories(LocalStorageIntegrationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${watcher_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIRS}
)
target_link_libraries(LocalStorageIntegrationTests PRIVATE
    SyncHarbor_core
    GTest::gtest_main
    Threads::Threads
)
gtest_discover_tests(LocalStorageIntegrationTests)

include(GoogleTest)

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS LocalStorageIntegrationTests DatabaseTests LocalStorageUnitTests
)
