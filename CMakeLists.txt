cmake_minimum_required(VERSION 3.16)

project(DAO_UTM_Linux)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# ------------------------------------------------------------
# SOEM
# ------------------------------------------------------------
add_subdirectory(third_party/soem)

set_target_properties(soem PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)

# ------------------------------------------------------------
# DAO EtherCAT Master
# ------------------------------------------------------------
add_library(dao_master STATIC
    master/src/DaoEtherCATMaster.cpp
)

set_target_properties(dao_master PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
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

set_target_properties(dao_engine_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
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
add_library(dao_engine_api SHARED
    engine/src/DaoEtherCAT.Engine.cpp
)

set_target_properties(dao_engine_api PROPERTIES
    OUTPUT_NAME dao_ethercat
    VERSION 1.0.0
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_compile_definitions(dao_engine_api
    PRIVATE
    DAOETHERCATENGINE_EXPORTS
)

target_link_options(dao_engine_api
    PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dao_ethercat.map"
    "LINKER:--no-undefined"
)

target_include_directories(dao_engine_api
    PUBLIC
    engine/include
    PRIVATE
    master/include
)

target_link_libraries(dao_engine_api
    PRIVATE
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
)

target_link_libraries(dao_engine_test
    PRIVATE
    dao_engine_api
)
