/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file vcomponent_CompositeInputService.cpp
 * @brief Entrypoint for the CompositeInput vcomponent Binder service.
 */

#include "aidl/vcomponent_CompositeInputManager.h"
#include "service/vcomponent_CompositeInputService.h"

#include "common/logger.h"
#include "utility/vcomponent_CompositeInputHelper.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace
{
constexpr const char* kLogPrefix = "[VDEVICE_COMPOSITEINPUT]<CompositeInputService>";

/**
 * @brief Parse a TCP port in the valid non-zero uint16_t range.
 *
 * @param value String representation of the port.
 * @param[out] outPort Receives the parsed port on success.
 *
 * @return True when @p value is a decimal port in the range 1..65535.
 */
bool parsePort(const char* value, std::uint16_t* outPort)
{
    if (value == nullptr || value[0] == '\0' || outPort == nullptr)
    {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == nullptr || *end != '\0' || parsed <= 0 || parsed > 65535)
    {
        return false;
    }

    *outPort = static_cast<std::uint16_t>(parsed);
    return true;
}

/**
 * @brief Parse the supported command-line arguments.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param[out] outHfpPath Receives the HFP YAML path to use.
 * @param[out] outPort Receives an optional UT control-plane port override.
 *
 * @return True when parsing succeeded and the service may continue.
 */
bool parseArgs(
    int argc,
    char** argv,
    std::string* outHfpPath,
    std::optional<std::uint16_t>* outPort)
{
    if (outHfpPath == nullptr || outPort == nullptr || argc < 1 || argv == nullptr ||
        argv[0] == nullptr)
    {
        return false;
    }

    *outHfpPath = vcomponent::compositeinput::service::kDefaultHfpPath;
    *outPort = std::nullopt;

    for (int index = 1; index < argc; ++index)
    {
        const char* argument = argv[index];
        if (argument == nullptr)
        {
            continue;
        }

        if (std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0)
        {
            return false;
        }

        if (std::strcmp(argument, "--hfp") == 0)
        {
            if (index + 1 >= argc || argv[index + 1] == nullptr || argv[index + 1][0] == '\0')
            {
                LOGF_ERROR("%s Missing value for --hfp", kLogPrefix);
                return false;
            }

            *outHfpPath = argv[++index];
            continue;
        }

        if (std::strcmp(argument, "--port") == 0)
        {
            if (index + 1 >= argc || argv[index + 1] == nullptr || argv[index + 1][0] == '\0')
            {
                LOGF_ERROR("%s Missing value for --port", kLogPrefix);
                return false;
            }

            std::uint16_t port = 0;
            if (!parsePort(argv[index + 1], &port))
            {
                LOGF_ERROR("%s Invalid --port value: %s", kLogPrefix, argv[index + 1]);
                return false;
            }

            *outPort = port;
            ++index;
            continue;
        }

        LOGF_ERROR("%s Unknown argument: %s", kLogPrefix, argument);
        return false;
    }

    return true;
}
} // namespace

void vcomponent::compositeinput::service::printUsage(const char* argv0)
{
    const char* executable =
        (argv0 != nullptr && argv0[0] != '\0') ? argv0 : "RDKCompositeInputService";

    LOGF_INFO("Usage:");
    LOGF_INFO("  %s [--hfp <path>] [--port <port>]", executable);
    LOGF_INFO("");
    LOGF_INFO("Args:");
    LOGF_INFO("  --hfp <path> Optional CompositeInput HFP YAML path (default: %s).",
              kDefaultHfpPath);
    LOGF_INFO("  --port <port> Optional UT Control Plane port (default: manager default).");
}

/**
 * @brief Service process entrypoint.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @return 0 on clean shutdown, 1 on Binder publication failure, 2 on bad usage.
 */
int main(int argc, char** argv)
{
    using com::rdk::hal::compositeinput::CompositeInputManager;

    LOGF_INFO("%s ===============================", kLogPrefix);
    LOGF_INFO("%s CompositeInput Service 0.1.0.0.", kLogPrefix);
    LOGF_INFO("%s ===============================", kLogPrefix);

    std::string hfpPath;
    std::optional<std::uint16_t> port;
    if (!parseArgs(argc, argv, &hfpPath, &port))
    {
        vcomponent::compositeinput::service::printUsage(argc > 0 ? argv[0] : nullptr);
        return 2;
    }

    hfpPath = vcomponent::compositeinput::utility::trim(hfpPath);
    if (hfpPath.empty())
    {
        LOGF_ERROR("%s Empty HFP path after argument parsing", kLogPrefix);
        return 2;
    }

    LOGF_INFO("%s Starting CompositeInput Binder service "
              "(serviceName=%s, configPath=%s)",
              kLogPrefix,
              CompositeInputManager::getServiceName(),
              hfpPath.c_str());

    // Best-effort readability check; the manager falls back to its built-in
    // profile when the HFP cannot be read or parsed.
    const auto hfpContents = vcomponent::compositeinput::utility::readFileToString(hfpPath);
    if (!hfpContents.has_value())
    {
        LOGF_WARN("%s HFP file could not be read (continuing). path=%s",
                  kLogPrefix,
                  hfpPath.c_str());
    }
    else
    {
        LOGF_INFO("%s HFP file OK. path=%s bytes=%zu",
                  kLogPrefix,
                  hfpPath.c_str(),
                  hfpContents->size());
    }

    // The manager owns the UT control plane and its default. The service only
    // supplies an explicit command-line override when one was requested.
    if (port.has_value())
    {
        LOGF_INFO("%s Using UT control-plane port from CLI: %u",
                  kLogPrefix,
                  static_cast<unsigned>(*port));
        CompositeInputManager::setControlPlanePort(*port);
    }
    else
    {
        LOGF_INFO("%s Using UT control-plane port default (manager default)", kLogPrefix);
    }

    // Hand configuration to the manager, then publish and block on Binder.
    CompositeInputManager::setConfigurationPath(hfpPath);
    return CompositeInputManager::publishAndJoinThreadPool();
}
