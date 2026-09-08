# Composite Input vcomponent

This repository folder contains Composite Input component service that is expected to consume the RDK HALIF Composite Input AIDL interfaces.

## Table of Contents

- [Composite Input (vDevice) README](#composite-input-vcomponent)
  - [Acronyms, Terms and Abbreviations](#acronyms-terms-and-abbreviations)
  - [Build linux binder for Target Linux](#build-linux-binder-for-target-linux)
  - [Build RDKCompositeInputService](#build-rdkcompositeinputservice)
  - [Run Composite Input](#run-rdkcompositeinputservice)

## Acronyms, Terms and Abbreviations

| Acronym / Term | Description |
|----------------|-------------|
| **AIDL** | Android Interface Definition Language |
| **HAL** | Hardware Abstraction Layer |
| **HFP** | HAL Feature Profile (YAML profile used to configure the vComponent) |
| **RDK** | Reference Design Kit |
| **UT-Core** | RDK Unified Test Core Framework |
| **UT ControlPlane** | UT-Core / UT-Control control plane for receiving YAML/KVP control messages |
| **VTS** | Vendor Test Suite |
| **YAML** | Yet Another Markup Language (configuration format) |

## Build RDKCompositeInputService

### Prerequisites for UT-Core

This module relies on UT-Core / UT-Control headers and libraries. Please ensure all required packages for UT-Core are installed. See:  
[Packages for ut-core](https://github.com/rdkcentral/ut-core/wiki/UT-Core-Building-using-Docker-or-Vagrant#script-for-installing-basic-packages-for-ut-core)

### Clone the Repository

```bash
git clone git@github.com:rdkcentral/rdk-halif-aidl-vcomponent-compositeinput.git

cd rdk-halif-aidl-vcomponent-compositeinput
```

### Environment variables

The build is driven by `./build.sh` in this repository. It uses (or defaults) the following environment variables:

- `UT_CORE_VERSION`: Specific version of UT-Core to build. If not set, the script checks out the latest tag.
- `RDK_HALIF_AIDL_VERSION`: Git ref used if the script must clone `rdk-halif-aidl`. The script defaults to `main`.

Example:

```bash
export UT_CORE_VERSION=5.1.0
export RDK_HALIF_AIDL_VERSION=0.22.0
```

### Build command (Target Linux)

From the repository root:

```bash
./build.sh Target=linux
```

At a high level, the `build.sh` script:

1. Stages Linux binder service-manager binaries, headers, and libraries into `build/usr`.
2. Generates AIDL C++ headers for the `RDKCompositeInput` interface (AIDL “current”).
3. Builds the AIDL support library via `aidl_lib/Makefile`.
4. Clones and builds `ut-core` (checked out to `UT_CORE_VERSION`) and stages required headers.
5. Builds the RDKCompositeInput service using CMake. The service executable is named `RDKCompositeInputService`.

## Run RDKComposite InputService

### Run the service on a target device

To run the RDKCompositeInput Binder service:

1. Copy the repository’s `build/` folder onto the target device (for example using `scp`), or otherwise ensure the built binary and `vcomponent_configurations/` directory are present on the target filesystem.
2. Run the RDKCompositeInput service binary.

The service executable is named:

- `RDKCompositeInputService`

### Command line interface

The RDKCompositeInput service supports the following command line flags:

- `--hfp <path>`: Optional HFP YAML path.  
  Default: `vcomponent_configurations/hfp-compositeinput.yaml`
- `--port <port>`: Optional UT ControlPlane port to listen on.  
  Default: `8086`

Example:

```bash
./RDKCompositeInputService --hfp vcomponent_configurations/hfp-compositeinput.yaml --port 8086
```

If `--help` (or `-h`) is provided, or if an unknown argument is provided, the service prints usage and exits with failure.
