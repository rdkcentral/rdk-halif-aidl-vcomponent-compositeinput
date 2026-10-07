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

#pragma once

/**
 * @file vcomponent_CompositeInputHfpConfigUtils.h
 * @brief HFP configuration structures aligned with CompositeInput AIDL data.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace vcomponent::compositeinput::utility
{

/**
 * @brief HFP representation of PropertyMetadata for one PortProperty.
 */
struct CompositeInputPropertyMetadataConfig
{
    std::string key;
    std::string type;
    bool readOnly = true;
    bool isMetric = false;
    std::string description;
};

/**
 * @brief HFP representation of one CompositeInput port.
 */
struct CompositeInputPortConfig
{
    int32_t id = -1;
    std::string name;
    std::string description;
    std::vector<std::string> supportedProperties;
    std::vector<CompositeInputPropertyMetadataConfig> propertyMetadata;
};

/**
 * @brief HFP representation of PlatformCapabilities.FeatureFlags.
 */
struct CompositeInputFeatureFlagsConfig
{
    bool macrovisionDetectionSupported = false;
};

/**
 * @brief HFP representation of CompositeInput platform configuration.
 */
struct CompositeInputHfpConfig
{
    std::string interfaceVersion;
    std::vector<CompositeInputPortConfig> ports;
    std::string halVersion;
    int32_t maxPorts = 0;
    int32_t maximumConcurrentStartedPorts = 1;
    std::vector<std::string> supportedProperties;
    std::vector<CompositeInputPropertyMetadataConfig> propertyMetadata;
    CompositeInputFeatureFlagsConfig features;
};

} // namespace vcomponent::compositeinput::utility
