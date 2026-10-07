#!/bin/bash
#* ******************************************************************************
#*  If not stated otherwise in this file or this component's LICENSE
#*  file the following copyright and licenses apply:
#*
#*  Copyright 2026 RDK Management
#*
#*  Licensed under the Apache License, Version 2.0 (the License);
#*  you may not use this file except in compliance with the License.
#*  You may obtain a copy of the License at
#*
#*  http://www.apache.org/licenses/LICENSE-2.0
#*
#*  Unless required by applicable law or agreed to in writing, software
#*  distributed under the License is distributed on an AS IS BASIS,
#*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#*  See the License for the specific language governing permissions and
#*  limitations under the License.
#*
#* ******************************************************************************

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

declare -r TOP=`dirname $(realpath $0)`
declare -r HALIF_VERSION=current
RDK_HALIF_AIDL_VERSION="${RDK_HALIF_AIDL_VERSION:-"develop"}"

VCOMPONENT="${VCOMPONENT:-compositeinput}"

VCOMPONENT_VERSION="${VCOMPONENT_VERSION:-0.2.0.0}"
VCOMPONENT_INCLUDE_VERSION="${VCOMPONENT_INCLUDE_VERSION:-0.2.0.0}"

declare -A HALIF_COMPONENTS=(
  [compositeinput]="current"
)

OUT_TAR_FILE=vcomponent-CompositeInput.tar.gz

# Define the directory where the repository will be cloned
declare -r UT_CORE_DIR="${TOP}/ut-core"
declare -r TOP_BUILD_DIR="${TOP}/build"
declare -r BUILD_INSTALL_PREFIX=${TOP_BUILD_DIR}/usr
declare -r RDK_HAL_DIR="${RDK_HAL_DIR:-${TOP}/rdk-halif-aidl}"

mkdir -p $TOP_BUILD_DIR

# Parse Target argument (e.g., Target=arm or target=linux)
# Default to 'linux' if not provided
TARGET="linux"

###############################################################################
# Function to validate TARGET option
###############################################################################
EXTRA_CMAKE_ARGS=()
HAL_DBG_LEVEL=""

for arg in "$@"; do
    if [[ $arg == [Tt]arget=* ]]; then
        TARGET="${arg#*=}"
    elif [[ $arg == HAL_DBG_LEVEL=* ]]; then
        HAL_DBG_LEVEL="${arg#*=}"
    fi
done

if [[ "$HAL_DBG_LEVEL" == "INFO" ]]; then
    EXTRA_CMAKE_ARGS+=("-DENABLE_LOG_INFO=ON")
elif [[ "$HAL_DBG_LEVEL" == "DEBUG" ]]; then
    EXTRA_CMAKE_ARGS+=("-DENABLE_LOG_INFO=ON" "-DENABLE_LOG_DEBUG=ON")
fi

# Validate TARGET
valid_targets=("linux" "arm")
if [[ ! " ${valid_targets[@]} " =~ " ${TARGET} " ]]; then
    echo -e "${BOLD}${RED}Invalid Target: $TARGET. Valid targets are: ${valid_targets[@]}${RESET}"
    help
    exit 1
fi

###############################################################################
# Function to checkout the halif aidl repository and generate the HAL interfaces
###############################################################################
generate_hal_interfaces()
{
    # Clone the repository if not already present
    if [ ! -d "$RDK_HAL_DIR" ]; then
        echo "Checking out version: " $RDK_HALIF_AIDL_VERSION
        git clone -b $RDK_HALIF_AIDL_VERSION 'https://github.com/rdkcentral/rdk-halif-aidl.git' $RDK_HAL_DIR
    fi

    pushd "$RDK_HAL_DIR" >/dev/null

    ./build_binder.sh || { echo -e "${BOLD}* ${RED}Failed to build binder tools. Exiting.${RESET}"; exit 1; }

    # CompositeInput depends on HALIF 'common' (PropertyValue.h)
    ./build_modules.sh "common" --version "${VCOMPONENT_VERSION}" || {
        echo -e "${BOLD}* ${RED}Failed to build HALIF modules (common). Exiting.${RESET}"
        exit 1
    }

    # Prepare/stage common headers for both HALIF module build and vcomponent build
    prepare_common_dir

    # Sanity check: ensure staged header exists where HALIF builds typically include from
    if [ ! -f "${RDK_HAL_DIR}/out/build/include/com/rdk/hal/PropertyValue.h" ]; then
        echo -e "${BOLD}${RED}Staged header missing: ${RDK_HAL_DIR}/out/build/include/com/rdk/hal/PropertyValue.h${RESET}"
        exit 1
    fi

    # ---- pass include path only to this command invocation ----
    COMMON_VER_INC="${RDK_HAL_DIR}/common/${VCOMPONENT_VERSION}/include"

    

    CFLAGS="-I${COMMON_VER_INC} ${CFLAGS:-}" \
    CXXFLAGS="-I${COMMON_VER_INC} ${CXXFLAGS:-}" \
    ./build_modules.sh "${VCOMPONENT}" --clean --version "${VCOMPONENT_VERSION}" || {
        echo -e "${BOLD}* ${RED}Failed to build HALIF modules. Exiting.${RESET}"
        exit 1
    }

    popd >/dev/null

    # After generating HAL interfaces/tools, path for the derived environment variables
    halif_binder_env
}

###############################################################################
# Function to set path for HALIF + Binder SDK environment variables
###############################################################################
halif_binder_env()
{
    AIDL_SRC_VERSION="${AIDL_SRC_VERSION:-${VCOMPONENT_VERSION}}"
    VCOMPONENT_HALIF_INCLUDE_DIR="${VCOMPONENT_HALIF_INCLUDE_DIR:-${RDK_HAL_DIR}/${VCOMPONENT}/${VCOMPONENT_VERSION}/include}"
    

    AIDL_BIN="${AIDL_BIN:-${RDK_HAL_DIR}/out/target/bin/aidl}"
    AIDL_CPP_BIN="${AIDL_CPP_BIN:-${RDK_HAL_DIR}/out/target/bin/aidl-cpp}"

    BINDER_SDK_DIR="${BINDER_SDK_DIR:-${RDK_HAL_DIR}/out/target}"
    BINDER_SDK_INCLUDE_DIR="${BINDER_SDK_INCLUDE_DIR:-${RDK_HAL_DIR}/out/build/include/binder_sdk}"

    HALIF_LIB_DIR="${HALIF_LIB_DIR:-${RDK_HAL_DIR}/out/target/lib/rdk-halif-aidl}"
    HALIF_INCLUDE_DIR="${HALIF_INCLUDE_DIR:-${RDK_HAL_DIR}/out/build/include}"

    case ":${PATH}:" in
        *":${RDK_HAL_DIR}/out/target/bin:"*) : ;;
        *) PATH="${RDK_HAL_DIR}/out/target/bin:${PATH}" ;;
    esac
    case ":${PATH}:" in
        *":${BINDER_SDK_DIR}/bin:"*) : ;;
        *) PATH="${BINDER_SDK_DIR}/bin:${PATH}" ;;
    esac

    LD_LIBRARY_PATH="${BINDER_SDK_DIR}/lib:${HALIF_LIB_DIR}:${LD_LIBRARY_PATH:-}"

    echo -e "${BOLD}${CYAN}Derived HALIF/Binder environment:${RESET}"
    echo -e "${BOLD}${GREEN}  RDK_HAL_DIR=${RESET}${YELLOW}${RDK_HAL_DIR}${RESET}"
    echo -e "${BOLD}${GREEN}  VCOMPONENT=${RESET}${YELLOW}${VCOMPONENT}${RESET}"
    echo -e "${BOLD}${GREEN}  VCOMPONENT_VERSION=${RESET}${YELLOW}${VCOMPONENT_VERSION}${RESET}"
    echo -e "${BOLD}${GREEN}  AIDL_BIN=${RESET}${YELLOW}${AIDL_BIN}${RESET}"
}

###############################################################################
# Function to clone and build the ut-core repository
###############################################################################
build_ut_core()
{
    if [ ! -d ${UT_CORE_DIR} ]; then
        git clone https://github.com/rdkcentral/ut-core.git ${UT_CORE_DIR}
        if [ $? -ne 0 ]; then
            echo "Failed to clone the repository. Exiting."
            exit 1
        fi
    fi

    pushd ${UT_CORE_DIR}

    git fetch -q -t origin
    if [ -z "$UT_CORE_VERSION" ]; then
        UT_CORE_VERSION=`git tag | sort -Vr | head -n1`
    fi

    git checkout $UT_CORE_VERSION

    ./build.sh TARGET=$TARGET VARIANT=CPP
    if [ $? -ne 0 ]; then
        echo -e "${BOLD}* ${RED}Failed to build the UT Core. Exiting.${RESET}"
        exit 1
    fi

    make -C ${UT_CORE_DIR} TARGET=$TARGET VARIANT=CPP BIN_DIR=$TOP_BUILD_DIR/lib
    if [ $? -ne 0 ]; then
        echo -e "${BOLD}* ${RED}Failed to build the UT Control. Exiting.${RESET}"
        exit 1
    fi
    popd
}

###############################################################################
# Function to install linux-binder service manager, headers, and libraries
###############################################################################
install_linux_binder()
{
     echo "LINUX_BINDER_OUT_DIR=${LINUX_BINDER_OUT_DIR}"

    popd >/dev/null

    install -d ${BUILD_INSTALL_PREFIX}/bin \
               ${BUILD_INSTALL_PREFIX}/lib/ \
               ${BUILD_INSTALL_PREFIX}/include

    install -t ${BUILD_INSTALL_PREFIX}/bin ${LINUX_BINDER_OUT_DIR}/bin/*
    install -t ${BUILD_INSTALL_PREFIX}/lib ${LINUX_BINDER_OUT_DIR}/lib/*.so
    install -t ${BUILD_INSTALL_PREFIX}/lib ${BINDER_SDK_DIR}/lib/rdk-halif-aidl/*
    cp -apr ${BINDER_SDK_INCLUDE_DIR}/* ${BUILD_INSTALL_PREFIX}/include/
}

###############################################################################
# Function to copy common directory from HALIF repo (versioned include retained)
###############################################################################
prepare_common_dir()
{
    SRC_COMMON_DIR="${RDK_HAL_DIR}/common"
    DEST_COMMON_DIR="${TOP_BUILD_DIR}/common"

    echo -e "${BOLD}${CYAN}Preparing common directory...${RESET}"

    if [ ! -d "${SRC_COMMON_DIR}" ]; then
        echo -e "${RED}common directory not found in HALIF repo!${RESET}"
        exit 1
    fi

    rm -rf "${DEST_COMMON_DIR}"
    cp -r "${SRC_COMMON_DIR}" "${DEST_COMMON_DIR}" || { echo -e "${RED}Failed to copy common directory!${RESET}"; exit 1; }

    # Versioned include root (needed for PropertyValue.h)
    COMMON_INCLUDE_DIR="${DEST_COMMON_DIR}/${VCOMPONENT_VERSION}/include"
    echo -e "${GREEN}COMMON_INCLUDE_DIR=${COMMON_INCLUDE_DIR}${RESET}"

    if [ ! -f "${COMMON_INCLUDE_DIR}/com/rdk/hal/PropertyValue.h" ]; then
        echo -e "${RED}PropertyValue.h not found at: ${COMMON_INCLUDE_DIR}/com/rdk/hal/PropertyValue.h${RESET}"
        exit 1
    fi

    # Stage into HALIF global include root so HALIF module build can include <com/...>
    HALIF_GLOBAL_INCLUDE_DIR="${RDK_HAL_DIR}/out/build/include"
    mkdir -p "${HALIF_GLOBAL_INCLUDE_DIR}" || exit 1
    cp -a "${SRC_COMMON_DIR}/${VCOMPONENT_VERSION}/include/com" "${HALIF_GLOBAL_INCLUDE_DIR}/" || exit 1
}

###############################################################################
# Function to build CompositeInput component
###############################################################################
build_CompositeInput()
{
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DRDK_HAL_DIR="${RDK_HAL_DIR}" \
        -DBINDER_SDK_DIR="${BINDER_SDK_DIR}" \
        -DBINDER_SDK_INCLUDE_DIR="${BINDER_SDK_INCLUDE_DIR}" \
        -DHALIF_LIB_DIR="${HALIF_LIB_DIR}" \
        -DHALIF_INCLUDE_DIR="${HALIF_INCLUDE_DIR}" \
        -DCOMPOSITEINPUT_HALIF_INCLUDE_DIR="${VCOMPONENT_HALIF_INCLUDE_DIR}" \
        -DHALIF_COMMON_INCLUDE_DIR="${HALIF_COMMON_INCLUDE_DIR}" \
        -DCOMMON_INCLUDE_DIR="${COMMON_INCLUDE_DIR}" \
        -DVCOMPONENT_VERSION="${VCOMPONENT_VERSION}" \
        "${EXTRA_CMAKE_ARGS[@]}"

    local cfg_rc=$?
    if [ ${cfg_rc} -ne 0 ]; then
        echo "❌ Configuration failed (exit ${cfg_rc}). Aborting."
        return ${cfg_rc}
    fi

    cmake --build build -j || return $?
    cmake --install build --prefix ./build/out/ || return $?

    cp -f "${HALIF_LIB_DIR}"/libcompositeinput*-cpp.so "${TOP_BUILD_DIR}/" 2>/dev/null || \
    cp -f "${HALIF_LIB_DIR}"/libcompositeinput*.so "${TOP_BUILD_DIR}/" 2>/dev/null || \
    echo -e "${YELLOW}Warning: compositeinput shared library not found under ${HALIF_LIB_DIR}${RESET}"

    # libcompositeinput-v<ver>-cpp.so has a direct NEEDED dependency on
    # libcommon-v<ver>-cpp.so (HALIF 'common' 0.2.0.0), which hdmioutput does
    # not have. Copy it beside the service binary as well, otherwise the loader
    # reports "libcommon-v0.2.0.0-cpp.so: cannot open shared object file" even
    # after exporting LD_LIBRARY_PATH.
    cp -f "${HALIF_LIB_DIR}"/libcommon*-cpp.so "${TOP_BUILD_DIR}/" 2>/dev/null || \
    cp -f "${HALIF_LIB_DIR}"/libcommon*.so "${TOP_BUILD_DIR}/" 2>/dev/null || \
    echo -e "${YELLOW}Warning: common shared library not found under ${HALIF_LIB_DIR}${RESET}"
}

###############################################################################
# Function to clean the test build directory
###############################################################################
clean_test()
{
    echo -e "${BOLD}removing *.o${RESET}"
    rm -rf build
    [ -d "${AIDL_LIB_DIR}" ] && make -C ${AIDL_LIB_DIR} clean
    rm -rf ${TOP_BUILD_DIR}/bin/${OUT_TAR_FILE}
}

###############################################################################
# Function to clean the test build directory
###############################################################################
dist_clean_test()
{
    clean_test
    rm -rf $TOP_BUILD_DIR
    rm -rf $RDK_HAL_DIR
    rm -rf ${UT_CORE_DIR}
}

###############################################################################
# help function to display usage information
###############################################################################
help()
{
    echo -e "${BOLD}${CYAN}Usage:${RESET}"

    echo -e "${BOLD}╔══════════════════════════════════════════════════════════════════════════════╗${RESET}"
    echo -e "${BOLD}* ${GREEN}$0 TARGET=<target>${RESET} ${YELLOW}- Builds the component for specified target (arm, linux)${RESET} ${BOLD}*${RESET}"
    echo -e "${BOLD}* ${GREEN}$0 clean${RESET}           ${YELLOW}- Cleans the build folder${RESET}                           ${BOLD}*${RESET}"
    echo -e "${BOLD}* ${GREEN}$0 dist_clean${RESET}      ${YELLOW}- Deletes all build and checkout folders${RESET}            ${BOLD}*${RESET}"
    echo -e "${BOLD}* ${GREEN}$0 help/-h/--help${RESET}  ${YELLOW}- Prints this message${RESET}                               ${BOLD}*${RESET}"
    echo -e "${BOLD}╚══════════════════════════════════════════════════════════════════════════════╝${RESET}"
}

echo -e "${BOLD}${GREEN}Repo Branch            : ${RESET}${YELLOW}$(git rev-parse --abbrev-ref HEAD)${RESET}"
echo -e "${BOLD}${GREEN}UT_CORE_VERSION        : ${RESET}${YELLOW}$UT_CORE_VERSION${RESET}"
echo -e "${BOLD}${GREEN}RDK_HALIF_AIDL_VERSION : ${RESET}${YELLOW}$RDK_HALIF_AIDL_VERSION${RESET}"
echo -e "${BOLD}${GREEN}AIDL_BIN               : ${RESET}${YELLOW}$AIDL_BIN${RESET}"
echo -e "${BOLD}${GREEN}TARGET                 : ${RESET}${YELLOW}$TARGET${RESET}"

###############################################################################
# Main script execution starts here
###############################################################################
if [ "$1" == "clean" ]; then
    clean_test
elif [ "$1" == "dist_clean" ]; then
    dist_clean_test
elif [ "$1" == "help" ] || [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
    help
elif [[ "$1" == [Tt]arget=* ]]; then
    generate_hal_interfaces
    # install_linux_binder() begins with popd; recreate the expected directory stack
    # entry here so popd has a matching pushd to consume.
    pushd "${RDK_HAL_DIR}" >/dev/null
    install_linux_binder
    build_ut_core
    build_CompositeInput
    echo -e "${BOLD}╔══════════════════════════════╗${RESET}"
    echo -e "${BOLD}* ${GREEN}Build Successful${RESET}        ${BOLD}*${RESET}"
    echo -e "${BOLD}╚══════════════════════════════╝${RESET}"
else
    echo -e "${BOLD}${RED}Invalid Arguments${RESET}"
    help
fi
