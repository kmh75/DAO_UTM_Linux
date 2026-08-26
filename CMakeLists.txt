cmake_minimum_required(VERSION 3.16)

project(DAO_UTM_Linux)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# ------------------------------------------------------------
# SOEM
# ------------------------------------------------------------
add_subdirectory(third_party/soem)

# ------------------------------------------------------------
# DAO EtherCAT Master
# ------------------------------------------------------------
add_library(dao_master STATIC
    master/src/DaoEtherCATMaster.cpp
)

target_include_directories(dao_master
    PUBLIC
    master/include
)

target_link_libraries(dao_master
    PUBLIC
    soem
    Threads::Threads
)

# ------------------------------------------------------------
# DAO Engine Core
# ------------------------------------------------------------
add_library(dao_engine_core STATIC
    engine/src/DaoEngineCore.cpp
)

target_include_directories(dao_engine_core
    PUBLIC
    engine/include
    master/include
)

target_link_libraries(dao_engine_core
    PUBLIC
    dao_master
)
# ------------------------------------------------------------
# Existing SOEM validation console
# ------------------------------------------------------------
add_executable(dao_console
    app/console/main.cpp
)

target_link_libraries(dao_console
    PRIVATE
    soem
)
# ------------------------------------------------------------
# DAO Engine API
# ------------------------------------------------------------
add_library(dao_engine_api STATIC
    engine/src/DaoEtherCAT.Engine.cpp
)

target_include_directories(dao_engine_api
    PUBLIC
    engine/include
    master/include
)

target_link_libraries(dao_engine_api
    PUBLIC
    dao_engine_core
)
# ------------------------------------------------------------
# DAO Engine Test
# ------------------------------------------------------------
add_executable(dao_engine_test
    app/engine_test/main.cpp
)

target_include_directories(dao_engine_test
    PRIVATE
    engine/include
    master/include
)

target_link_libraries(dao_engine_test
    PRIVATE
    dao_engine_api
)