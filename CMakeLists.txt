# 
# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2026 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
cmake_minimum_required(VERSION 3.8)

project(CompositeInput LANGUAGES CXX)

# ----------------------
# C++ standard
# ----------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ----------------------
# Base paths
# ----------------------
set(PROJECT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set(UTCONTROL_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ut-core/include")
set(UTFRAMEWORK_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ut-core/framework/ut-control/include")
set(GTEST_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ut-core/framework/gtest/linux/googletest-1.15.2/googletest/include")

# ----------------------
# Common include validation
# ----------------------
# CompositeInput generated headers may include:
#   #include <com/rdk/hal/PropertyValue.h>
# Accept either:
#   - COMMON_INCLUDE_DIR (typically staged by build.sh into build/common/<ver>/include), or
#   - HALIF_COMMON_INCLUDE_DIR (typically <RDK_HAL_DIR>/common/<ver>/include)
if(NOT DEFINED COMMON_INCLUDE_DIR)
    if(CMAKE_SYSROOT)
        message(STATUS "COMMON_INCLUDE_DIR not set; relying on sysroot include paths")
    elseif(DEFINED HALIF_COMMON_INCLUDE_DIR AND NOT "${HALIF_COMMON_INCLUDE_DIR}" STREQUAL "")
        message(STATUS "COMMON_INCLUDE_DIR not set; using HALIF_COMMON_INCLUDE_DIR")
        set(COMMON_INCLUDE_DIR "${HALIF_COMMON_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR "COMMON_INCLUDE_DIR not set")
    endif()
else()
    message(STATUS "COMMON_INCLUDE_DIR = ${COMMON_INCLUDE_DIR}")
endif()

if(DEFINED HALIF_COMMON_INCLUDE_DIR AND NOT "${HALIF_COMMON_INCLUDE_DIR}" STREQUAL "")
    message(STATUS "HALIF_COMMON_INCLUDE_DIR = ${HALIF_COMMON_INCLUDE_DIR}")
endif()

# ----------------------
# Optional version (fallback)
# ----------------------
if(NOT DEFINED VCOMPONENT_VERSION)
  set(VCOMPONENT_VERSION "0.2.0.0")
  message(WARNING "VCOMPONENT_VERSION not set. Using default: ${VCOMPONENT_VERSION}")
endif()

# ----------------------
# Detect environment
# ----------------------
if(CMAKE_SYSROOT)
    message(STATUS "Using Yocto sysroot: ${CMAKE_SYSROOT}")
    set(USE_SYSROOT TRUE)
else()
    message(STATUS "Using build.sh environment")
    set(USE_SYSROOT FALSE)
endif()

# ----------------------
# Include paths
# ----------------------
if(USE_SYSROOT)
    set(AIDL_INCLUDE_DIR "${CMAKE_SYSROOT}/usr/include/hal/h")
    set(EXTRA_INCLUDE    "${CMAKE_SYSROOT}/usr/include")
    set(EXTRA_INCLUDE    
    "${CMAKE_SYSROOT}/usr/include/vendor/compositeinput/${VCOMPONENT_VERSION}/include" 
    "${CMAKE_SYSROOT}/usr/include/vendor/common/${VCOMPONENT_INCLUDE_VERSION}/include")
else()
    if(NOT DEFINED HALIF_INCLUDE_DIR OR
       NOT DEFINED BINDER_SDK_DIR OR
       NOT DEFINED BINDER_SDK_INCLUDE_DIR OR
       NOT DEFINED COMPOSITEINPUT_HALIF_INCLUDE_DIR)
        message(FATAL_ERROR "HALIF/BINDER env not set. Use build.sh or sysroot")
    endif()

    set(AIDL_INCLUDE_DIR ${COMPOSITEINPUT_HALIF_INCLUDE_DIR})
    set(EXTRA_INCLUDE
        ${HALIF_INCLUDE_DIR}
        ${BINDER_SDK_INCLUDE_DIR}
    )
endif()

# ----------------------
# Debug
# ----------------------
message(STATUS "==== BUILD CONFIG ====")
message(STATUS "MODE = ${USE_SYSROOT}")
message(STATUS "VCOMPONENT_VERSION = ${VCOMPONENT_VERSION}")
message(STATUS "======================")

# ----------------------
# Sources
# ----------------------
set(COMPOSITEINPUT_CORE_SOURCES
    src/aidl/vcomponent_CompositeInputManager.cpp
    src/aidl/vcomponent_CompositeInputPort.cpp
    src/aidl/vcomponent_CompositeInputController.cpp
    src/aidl/vcomponent_CompositeInputControllerListener.cpp
    src/aidl/vcomponent_CompositeInputEventListener.cpp

    src/utility/vcomponent_CompositeInputParseConfig.cpp
    src/utility/vcomponent_CompositeInputHelper.cpp
    src/controller/vcomponent_CompositeInputUtController.cpp
)

set(COMPOSITEINPUT_SERVICE_SOURCES
    src/service/vcomponent_CompositeInputService.cpp
)

# ----------------------------------------
# Copy vcomponent_configurations directory
# ----------------------------------------
set(VCOMP_CONFIG_SRC "${PROJECT_SOURCE_DIR}/vcomponent_configurations")
set(VCOMP_CONFIG_DST "${CMAKE_BINARY_DIR}/vcomponent_configurations")

if(NOT EXISTS "${VCOMP_CONFIG_SRC}")
    message(FATAL_ERROR "Missing directory: ${VCOMP_CONFIG_SRC}")
endif()

add_custom_target(copy_vcomponent_configs ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VCOMP_CONFIG_DST}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VCOMP_CONFIG_SRC}"
            "${VCOMP_CONFIG_DST}"
    COMMENT "Copying vcomponent_configurations to build directory"
)

# ----------------------
# HAL library
# ----------------------
if(USE_SYSROOT)
    find_library(COMPOSITEINPUT_HAL_LIB
    NAMES 
        compositeinput-v${VCOMPONENT_VERSION}-cpp
        compositeinput-vcurrent-cpp
        compositeinput
        hal_aidl
    HINTS
        "${CMAKE_SYSROOT}/usr/lib/vendor/rdk-halif-aidl"
        
    )
    if(NOT COMPOSITEINPUT_HAL_LIB)
        message(FATAL_ERROR "CompositeInput HALIF lib not found in sysroot")
    endif()
else()
    if(NOT DEFINED HALIF_LIB_DIR)
        message(FATAL_ERROR "HALIF_LIB_DIR not set")
    endif()

    file(GLOB COMPOSITEINPUT_HAL_LIB
        "${HALIF_LIB_DIR}/libcompositeinput*-cpp.so"
        "${HALIF_LIB_DIR}/libcompositeinput*.so"
        "${HALIF_LIB_DIR}/*compositeinput*-cpp.so"
        "${HALIF_LIB_DIR}/*compositeinput*.so"
    )
    list(LENGTH COMPOSITEINPUT_HAL_LIB _HAL_LIB_COUNT)
    if(_HAL_LIB_COUNT EQUAL 0)
        message(FATAL_ERROR "CompositeInput HALIF lib not found in ${HALIF_LIB_DIR}")
    endif()

    list(SORT COMPOSITEINPUT_HAL_LIB)
    math(EXPR _HAL_LIB_LAST "${_HAL_LIB_COUNT} - 1")
    list(GET COMPOSITEINPUT_HAL_LIB ${_HAL_LIB_LAST} COMPOSITEINPUT_HAL_LIB)

    if(NOT EXISTS "${COMPOSITEINPUT_HAL_LIB}")
        message(FATAL_ERROR "CompositeInput HALIF lib not found in ${HALIF_LIB_DIR}")
    endif()
endif()

add_library(compositeinput_hal SHARED IMPORTED)
set_target_properties(compositeinput_hal PROPERTIES IMPORTED_LOCATION "${COMPOSITEINPUT_HAL_LIB}")

# ----------------------
# UT Control
# ----------------------
find_library(UTCONTROL_LIB
    NAMES ut_control libut_control.so
    HINTS
        "${PROJECT_SOURCE_DIR}/build/lib"
        "${CMAKE_SYSROOT}/usr/lib"
)

if(NOT UTCONTROL_LIB)
    message(FATAL_ERROR "ut_control library not found. Run build.sh so ut-core is cloned/built first.")
endif()

add_library(ut_control SHARED IMPORTED)
set_target_properties(ut_control PROPERTIES IMPORTED_LOCATION "${UTCONTROL_LIB}")

# ----------------------
# Binder / utils / log
# ----------------------
if(USE_SYSROOT)

    find_library(BINDER_LIB NAMES binder HINTS "${CMAKE_SYSROOT}/usr/lib")
    find_library(UTILS_LIB  NAMES utils  HINTS "${CMAKE_SYSROOT}/usr/lib")
    find_library(LOG_LIB    NAMES log    HINTS "${CMAKE_SYSROOT}/usr/lib")

else()

    set(BINDER_LIB "${BINDER_SDK_DIR}/lib/binder/libbinder.so")
    set(UTILS_LIB  "${BINDER_SDK_DIR}/lib/binder/libutils.so")
    set(LOG_LIB    "${BINDER_SDK_DIR}/lib/binder/liblog.so")

endif()

foreach(lib BINDER_LIB UTILS_LIB LOG_LIB)
    if(NOT ${lib})
        message(FATAL_ERROR "Missing required library: ${lib}")
    endif()
endforeach()

add_library(binder SHARED IMPORTED)
set_target_properties(binder PROPERTIES IMPORTED_LOCATION "${BINDER_LIB}")

add_library(utils SHARED IMPORTED)
set_target_properties(utils PROPERTIES IMPORTED_LOCATION "${UTILS_LIB}")

add_library(log SHARED IMPORTED)
set_target_properties(log PROPERTIES IMPORTED_LOCATION "${LOG_LIB}")

# ----------------------
# Core library
# ----------------------
add_library(compositeinput_core SHARED ${COMPOSITEINPUT_CORE_SOURCES})

target_include_directories(compositeinput_core PUBLIC
    ${PROJECT_INCLUDE_DIR}
    ${AIDL_INCLUDE_DIR}
    ${UTCONTROL_INCLUDE_DIR}
    ${UTFRAMEWORK_INCLUDE_DIR}
    ${GTEST_INCLUDE_DIR}
    ${EXTRA_INCLUDE}
    ${COMMON_INCLUDE_DIR}
)

target_include_directories(compositeinput_core PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(compositeinput_core PUBLIC
    compositeinput_hal
    ut_control
)

set_target_properties(compositeinput_core PROPERTIES VERSION 1.0.0 SOVERSION 1)

if(USE_SYSROOT)
    set_target_properties(compositeinput_core PROPERTIES
        INSTALL_RPATH "/usr/lib/vendor/rdk-halif-aidl"
    )
endif()

# ----------------------
# Executable
# ----------------------
add_executable(RDKCompositeInputService ${COMPOSITEINPUT_SERVICE_SOURCES})

add_dependencies(RDKCompositeInputService copy_vcomponent_configs)

target_include_directories(RDKCompositeInputService PRIVATE
    ${PROJECT_INCLUDE_DIR}
    ${AIDL_INCLUDE_DIR}
    ${UTCONTROL_INCLUDE_DIR}
    ${UTFRAMEWORK_INCLUDE_DIR}
    ${GTEST_INCLUDE_DIR}
    ${EXTRA_INCLUDE}
    ${COMMON_INCLUDE_DIR}
)

target_link_libraries(RDKCompositeInputService PRIVATE
    compositeinput_core
    compositeinput_hal
    binder
    utils
    log
    ut_control
)

if(USE_SYSROOT)
    set_target_properties(RDKCompositeInputService PROPERTIES
        INSTALL_RPATH "/usr/lib/vendor/rdk-halif-aidl"
    )
endif()

# ----------------------
# Compiler warnings
# ----------------------
foreach(tgt compositeinput_core RDKCompositeInputService)
    target_compile_options(${tgt} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-attributes
        -Wno-unknown-pragmas
        -Wno-unused-result
        -Wno-sign-compare
    )
endforeach()

# ----------------------
# Install
# ----------------------
include(GNUInstallDirs)

install(TARGETS compositeinput_core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(TARGETS RDKCompositeInputService
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${VCOMP_CONFIG_SRC}/"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/vcomponent_configurations
)

install(DIRECTORY ${PROJECT_INCLUDE_DIR}/controller/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/controller
        FILES_MATCHING PATTERN "*.h")

install(DIRECTORY ${PROJECT_INCLUDE_DIR}/utility/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/utility
        FILES_MATCHING PATTERN "*.h")

install(DIRECTORY ${PROJECT_INCLUDE_DIR}/aidl/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aidl
        FILES_MATCHING PATTERN "*.h")

install(DIRECTORY ${PROJECT_INCLUDE_DIR}/common/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/common
        FILES_MATCHING PATTERN "*.h")
