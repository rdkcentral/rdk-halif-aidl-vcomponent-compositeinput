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

#include <cstring>
#include <string>

namespace
{
constexpr const char* kLogPrefix = "[VDEVICE_COMPOSITEINPUT]<CompositeInputService>";

/**
 * @brief Parse the supported command-line arguments.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param[out] outHfpPath Receives the HFP YAML path to use.
 *
 * @return True when parsing succeeded and the service may continue.
 */
bool parseArgs(int argc, char** argv, std::string* outHfpPath)
{
    if (outHfpPath == nullptr || argc < 1 || argv == nullptr || argv[0] == nullptr)
    {
        return false;
    }

    *outHfpPath = vcomponent::compositeinput::service::kDefaultHfpPath;

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
    LOGF_INFO("  %s [--hfp <path>]", executable);
    LOGF_INFO("");
    LOGF_INFO("Args:");
    LOGF_INFO("  --hfp <path> Optional CompositeInput HFP YAML path (default: %s).",
              kDefaultHfpPath);
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
    if (!parseArgs(argc, argv, &hfpPath))
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

    // Hand configuration to the manager, then publish and block on Binder.
    CompositeInputManager::setConfigurationPath(hfpPath);
    return CompositeInputManager::publishAndJoinThreadPool();
}
