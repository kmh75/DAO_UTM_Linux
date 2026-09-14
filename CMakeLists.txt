cmake_minimum_required(VERSION 3.16)

project(DAO_UTM_Linux)

include(CTest)

option(DAO_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(DAO_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets DBus)

add_library(dao_utm_compliance STATIC compliance/src/UtmComplianceCompensation.cpp)
target_include_directories(dao_utm_compliance PUBLIC compliance/include)

add_library(dao_protocol STATIC protocol/src/DaoProtocolV1.cpp protocol/src/DaoProtocolServer.cpp)
target_include_directories(dao_protocol PUBLIC protocol/include)
target_link_libraries(dao_protocol PUBLIC Threads::Threads)

add_library(dao_utm_protocol_service STATIC ui/src/UtmProtocolService.cpp)
target_include_directories(dao_utm_protocol_service PUBLIC ui/include utm/include compliance/include)
target_link_libraries(dao_utm_protocol_service PUBLIC dao_protocol dao_utm_compliance)

add_executable(dao_compliance_test app/compliance_test/main.cpp)
target_link_libraries(dao_compliance_test PRIVATE dao_utm_compliance)
add_test(NAME dao_compliance_compensation COMMAND dao_compliance_test)

add_executable(dao_compliance_auto_calibration_test
    app/compliance_auto_calibration_test/main.cpp
    utm/src/UtmComplianceCalibrationController.cpp)
target_include_directories(dao_compliance_auto_calibration_test PRIVATE utm/include utm/src)
add_test(NAME dao_compliance_auto_calibration_engine COMMAND dao_compliance_auto_calibration_test)

add_executable(dao_protocol_test_client app/protocol_test_client/main.cpp)
target_link_libraries(dao_protocol_test_client PRIVATE dao_protocol Threads::Threads)
add_test(NAME dao_protocol_v1_offline_integration COMMAND dao_protocol_test_client)

add_executable(dao_protocol_service_test app/protocol_service_test/main.cpp)
target_link_libraries(dao_protocol_service_test PRIVATE dao_utm_protocol_service Threads::Threads)
add_test(NAME dao_protocol_production_service COMMAND dao_protocol_service_test)

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
    VERSION 1.0.1
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

# ------------------------------------------------------------
# DAO UTM Control Engine Skeleton
# ------------------------------------------------------------
add_library(dao_utm_engine SHARED
    utm/src/DaoUtm.Engine.cpp
    utm/src/UtmEngineCore.cpp
    utm/src/UtmInputCollector.cpp
    utm/src/UtmJogController.cpp
    utm/src/UtmCoordinateController.cpp
    utm/src/UtmMotionController.cpp
    utm/src/UtmMotionOutputArbiter.cpp
    utm/src/UtmSafetyMonitor.cpp
    utm/src/UtmStateMachine.cpp
    utm/src/UtmCommandMailbox.cpp
    utm/src/UtmRuntimeStore.cpp
    utm/src/UtmStopConditionMonitor.cpp
    utm/src/UtmSequencer.cpp
    utm/src/UtmComplianceCalibrationController.cpp
)

set_target_properties(dao_utm_engine PROPERTIES
    OUTPUT_NAME dao_utm_engine
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_compile_definitions(dao_utm_engine
    PRIVATE
    DAO_UTM_ENGINE_EXPORTS
)

target_include_directories(dao_utm_engine
    PUBLIC
    utm/include
    PRIVATE
    utm/src
    engine/include
)

target_link_libraries(dao_utm_engine
    PRIVATE
    dao_engine_api
    Threads::Threads
)

target_link_options(dao_utm_engine
    PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dao_utm_engine.map"
    "LINKER:--no-undefined"
)

# ------------------------------------------------------------
# DAO UTM Control Engine Test
# ------------------------------------------------------------
add_executable(dao_utm_test
    app/utm_test/main.cpp
)

target_include_directories(dao_utm_test
    PRIVATE
    utm/include
)

target_link_libraries(dao_utm_test
    PRIVATE
    dao_utm_engine
)

add_executable(dao_utm_sequence_test
    app/utm_sequence_test/main.cpp
    utm/src/UtmSequencer.cpp
    utm/src/UtmStopConditionMonitor.cpp
)

target_include_directories(dao_utm_sequence_test
    PRIVATE
    utm/include
    utm/src
)

if (Qt6_FOUND)
    add_library(dao_ui_system STATIC ui/src/NetworkService.cpp ui/src/SystemPowerService.cpp)
    target_include_directories(dao_ui_system PUBLIC ui/include)
    target_link_libraries(dao_ui_system PUBLIC Qt6::Core Qt6::DBus)
    add_executable(dao_network_system_test app/network_system_test/main.cpp ui/src/NetworkService.cpp)
    target_include_directories(dao_network_system_test PRIVATE ui/include)
    target_link_libraries(dao_network_system_test PRIVATE Qt6::Core)
    add_test(NAME dao_network_system_policy COMMAND dao_network_system_test)
    qt_standard_project_setup()
    add_executable(dao_utm_ui
        app/dao_utm_ui/main.cpp
        ui/src/UtmUiController.cpp
        ui/src/MachineProfile.cpp
        ui/src/MainWindow.cpp
        ui/src/TrendGraphWidget.cpp
        ui/include/MachineProfile.h
        ui/include/UtmUiController.h
        ui/include/MainWindow.h
        ui/include/TrendGraphWidget.h
    )
    set_target_properties(dao_utm_ui PROPERTIES AUTOMOC ON)
    target_include_directories(dao_utm_ui PRIVATE ui/include utm/include compliance/include)
    target_link_libraries(dao_utm_ui PRIVATE dao_utm_engine dao_utm_compliance dao_utm_protocol_service dao_ui_system Qt6::Core Qt6::Gui Qt6::Widgets)
    add_executable(dao_utm_graph_test app/utm_graph_test/main.cpp ui/src/TrendGraphWidget.cpp)
    target_include_directories(dao_utm_graph_test PRIVATE ui/include)
    target_link_libraries(dao_utm_graph_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_executable(dao_force_unit_test app/force_unit_test/main.cpp ui/src/UtmUiController.cpp ui/src/MachineProfile.cpp ui/include/UtmUiController.h)
    set_target_properties(dao_force_unit_test PROPERTIES AUTOMOC ON)
    target_compile_definitions(dao_force_unit_test PRIVATE DAO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(dao_force_unit_test PRIVATE ui/include utm/include compliance/include)
    target_link_libraries(dao_force_unit_test PRIVATE dao_utm_engine dao_utm_compliance dao_utm_protocol_service Qt6::Core)
    add_test(NAME dao_force_unit_profile_lifecycle COMMAND dao_force_unit_test)
    add_executable(dao_compliance_profile_test app/compliance_profile_test/main.cpp ui/src/MachineProfile.cpp)
    target_include_directories(dao_compliance_profile_test PRIVATE ui/include utm/include compliance/include)
    target_link_libraries(dao_compliance_profile_test PRIVATE dao_utm_compliance Qt6::Core)
    add_test(NAME dao_compliance_directional_profile COMMAND dao_compliance_profile_test)
    add_executable(dao_setup_persistence_test app/setup_persistence_test/main.cpp ui/src/UtmUiController.cpp ui/src/MachineProfile.cpp ui/src/MainWindow.cpp ui/src/TrendGraphWidget.cpp ui/include/UtmUiController.h ui/include/MainWindow.h ui/include/TrendGraphWidget.h)
    set_target_properties(dao_setup_persistence_test PROPERTIES AUTOMOC ON)
    target_include_directories(dao_setup_persistence_test PRIVATE ui/include utm/include compliance/include)
    target_link_libraries(dao_setup_persistence_test PRIVATE dao_utm_engine dao_utm_compliance dao_utm_protocol_service dao_ui_system Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME dao_setup_persistence_restart COMMAND dao_setup_persistence_test)
    set_tests_properties(dao_setup_persistence_restart PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(dao_production_ui_path_test app/production_ui_path_test/main.cpp)
    target_compile_definitions(dao_production_ui_path_test PRIVATE DAO_UI_EXECUTABLE="$<TARGET_FILE:dao_utm_ui>")
    target_link_libraries(dao_production_ui_path_test PRIVATE Qt6::Core)
    add_dependencies(dao_production_ui_path_test dao_utm_ui)
    add_test(NAME dao_production_ui_startup_save_restart COMMAND dao_production_ui_path_test)
    add_executable(dao_communication_policy_test app/communication_policy_test/main.cpp utm/src/UtmSafetyMonitor.cpp utm/src/UtmStateMachine.cpp)
    target_include_directories(dao_communication_policy_test PRIVATE utm/src utm/include)
    add_test(NAME dao_communication_policy_fault_injection COMMAND dao_communication_policy_test)
else()
    message(STATUS "Qt6 Widgets not found: dao_utm_ui target disabled")
endif()
