# Install script for directory: /home/dao/Projects/DAO_UTM_Linux/third_party/soem

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/libsoem.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/CMakeFiles/soem.dir/install-cxx-module-bmi-noconfig.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/cmake/soemConfig.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/cmake/soemConfig.cmake"
         "/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/CMakeFiles/Export/272ceadb8458515b2ae4b5630a6029cc/soemConfig.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/cmake/soemConfig-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/cmake/soemConfig.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/cmake" TYPE FILE FILES "/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/CMakeFiles/Export/272ceadb8458515b2ae4b5630a6029cc/soemConfig.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^()$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/cmake" TYPE FILE FILES "/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/CMakeFiles/Export/272ceadb8458515b2ae4b5630a6029cc/soemConfig-noconfig.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soem" TYPE FILE FILES
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_base.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_coe.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_config.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_dc.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_eoe.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_foe.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_main.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_print.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_soe.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/ec_type.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/include/soem/soem.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/osal/osal.h"
    "/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/include/soem/ec_options.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/scripts" TYPE FILE FILES "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/scripts/eniconv.py")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/cmake" TYPE FILE FILES "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/cmake/AddENI.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE FILE FILES
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/README.md"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/LICENSE.md"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soem" TYPE FILE FILES
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/osal/linux/osal_defs.h"
    "/home/dao/Projects/DAO_UTM_Linux/third_party/soem/oshw/linux/nicdrv.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/dao/Projects/DAO_UTM_Linux/build/third_party/soem/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
