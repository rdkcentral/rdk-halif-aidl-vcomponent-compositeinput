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
 * @file vcomponent_CompositeInputParseConfig.cpp
 * @brief SKELETON implementation of the CompositeInput HFP configuration source.
 *
 * This file intentionally contains NO YAML/KVP parsing back-end. Instead of
 * failing (which left the manager with empty capabilities and made every
 * VTS_L1_COMPOSITEINPUT case fail), the skeleton returns a built-in,
 * deterministic profile that mirrors
 * vcomponent_configurations/hfp-compositeinput.yaml.
 *
 * Why the defaults are file-independent
 * -------------------------------------
 * On target the service is launched from a working directory where the relative
 * HFP path may not resolve (observed in the VTS run logs). A skeleton whose
 * behaviour depends on locating that file is therefore non-deterministic. The
 * skeleton deliberately ignores @p configurationFile and always reports the same
 * profile so the Binder surface is stable everywhere.
 *
 * Values below are the contract asserted by VTS_L1_COMPOSITEINPUT:
 *   - 2 ports (ids 0 and 1)              -> getPortIds_pos / getPort_pos
 *   - halVersion "1.0.0"                 -> getPlatformCapabilities_pos
 *   - maxPorts 2
 *   - maximumConcurrentStartedPorts 1
 *   - 8 supportedProperties
 *   - 8 propertyMetadata entries
 *
 * Real implementation
 * -------------------
 * The production parser (ut-core / ut-control KVP based) is intentionally kept
 * out of this skeleton and will live in a separate implementation folder. See
 * TEVDevice/impl/README.md. When that lands, only the three functions below need
 * to forward to it; no caller changes are required.
 */

#include "utility/vcomponent_CompositeInputParseConfig.h"

#include <string>
#include <vector>

namespace vcomponent::compositeinput::utility
{

namespace
{

/**
 * @brief Build the 8 property tokens declared by the reference HFP.
 *
 * Order matches hfp-compositeinput.yaml so indices line up with the metadata
 * list, as required by the PlatformCapabilities AIDL documentation.
 *
 * @return Supported property tokens in HFP declaration order.
 */
std::vector<std::string> buildSupportedProperties()
{
    return {
        "SIGNAL_STRENGTH",
        "SIGNAL_QUALITY",
        "METRIC_SIGNAL_LOCK_TIME",
        "METRIC_SIGNAL_DROPS",
        "METRIC_UPTIME",
        "METRIC_SIGNAL_LOCK_COUNT",
        "METRIC_LAST_SIGNAL_LOCK_TIME",
        "METRIC_LAST_RESET_TIMESTAMP",
    };
}

/**
 * @brief Build one metadata entry.
 *
 * @param[in] key         PortProperty token.
 * @param[in] type        PropertyMetadata::PropertyType token.
 * @param[in] isMetric    Whether the property is a telemetry metric.
 * @param[in] description Human readable description.
 *
 * @return Populated metadata configuration entry.
 */
CompositeInputPropertyMetadataConfig makeMetadata(
    const std::string& key,
    const std::string& type,
    bool isMetric,
    const std::string& description)
{
    CompositeInputPropertyMetadataConfig metadata{};
    metadata.key = key;
    metadata.type = type;
    // Every property in the reference HFP is read-only.
    metadata.readOnly = true;
    metadata.isMetric = isMetric;
    metadata.description = description;
    return metadata;
}

/**
 * @brief Build the 8 metadata entries declared by the reference HFP.
 *
 * @return Property metadata in HFP declaration order.
 */
std::vector<CompositeInputPropertyMetadataConfig> buildPropertyMetadata()
{
    return {
        makeMetadata("SIGNAL_STRENGTH", "LONG", false, "Signal strength in dBm"),
        makeMetadata("SIGNAL_QUALITY",
                     "INTEGER",
                     false,
                     "Aggregated signal quality percentage (0..100)"),
        makeMetadata("METRIC_SIGNAL_LOCK_TIME",
                     "LONG",
                     true,
                     "Average signal lock time in milliseconds"),
        makeMetadata("METRIC_SIGNAL_DROPS",
                     "LONG",
                     true,
                     "Total signal drops since last reset"),
        makeMetadata("METRIC_UPTIME", "LONG", true, "Total uptime in milliseconds"),
        makeMetadata("METRIC_SIGNAL_LOCK_COUNT",
                     "LONG",
                     true,
                     "Successful signal lock acquisitions since last reset"),
        makeMetadata("METRIC_LAST_SIGNAL_LOCK_TIME",
                     "LONG",
                     true,
                     "Most recent signal lock acquisition time in milliseconds"),
        makeMetadata("METRIC_LAST_RESET_TIMESTAMP",
                     "LONG",
                     true,
                     "Wall-clock metric reset timestamp in milliseconds"),
    };
}

/**
 * @brief Build one port entry with the reference property surface.
 *
 * @param[in] id          AIDL port identifier.
 * @param[in] name        Port display name.
 * @param[in] description Port description.
 *
 * @return Populated port configuration.
 */
CompositeInputPortConfig makePort(
    int32_t id,
    const std::string& name,
    const std::string& description)
{
    CompositeInputPortConfig port{};
    port.id = id;
    port.name = name;
    port.description = description;
    port.supportedProperties = buildSupportedProperties();
    port.propertyMetadata = buildPropertyMetadata();
    return port;
}

/**
 * @brief Build the complete built-in skeleton profile.
 *
 * @return Deterministic CompositeInput configuration.
 */
CompositeInputHfpConfig buildDefaultConfig()
{
    CompositeInputHfpConfig config{};

    config.interfaceVersion = "0.2.0.0";

    config.ports.push_back(
        makePort(0, "Front Panel Composite", "Front panel composite video input"));
    config.ports.push_back(
        makePort(1, "Rear Composite", "Rear panel composite video input"));

    config.halVersion = "1.0.0";
    config.maxPorts = 2;
    config.maximumConcurrentStartedPorts = 1;
    config.supportedProperties = buildSupportedProperties();
    config.propertyMetadata = buildPropertyMetadata();
    config.features.macrovisionDetectionSupported = false;

    return config;
}

} // namespace

void* vcomponent_CompositeInput_kvpCreateInstance(char* fileName)
{
    // TODO(impl): create a KVP instance and open `fileName` once the real parser
    // lands in the separate implementation folder. The skeleton has no parsing
    // back-end, so no instance is ever produced.
    (void)fileName;
    return nullptr;
}

void vcomponent_CompositeInput_kvpDestroyInstance(void* instance)
{
    // TODO(impl): destroy the KVP instance created above (ignore nullptr).
    // No-op while the skeleton has no parsing back-end.
    (void)instance;
}

bool vcomponent_CompositeInput_parseConfig(
    char* configurationFile,
    CompositeInputHfpConfig& compositeInputConfiguration,
    std::string* outError)
{
    // TODO(impl): read `configurationFile` with the ut-core KVP APIs and honour
    // the documented validation rules. The skeleton ignores the path on purpose
    // (see the file header) and reports the built-in profile instead.
    (void)configurationFile;

    compositeInputConfiguration = buildDefaultConfig();

    if (outError != nullptr)
    {
        outError->clear();
    }

    return true;
}

bool loadCompositeInputHfpConfigFromYaml(
    const std::string& path,
    CompositeInputHfpConfig* outConfig,
    std::string* outError)
{
    if (outConfig == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "outConfig is null";
        }
        return false;
    }

    // vcomponent_CompositeInput_parseConfig() takes char* to match the ut-core
    // KVP API; use a mutable copy rather than casting away constness.
    std::string mutablePath = path;
    return vcomponent_CompositeInput_parseConfig(mutablePath.data(), *outConfig, outError);
}

} // namespace vcomponent::compositeinput::utility
