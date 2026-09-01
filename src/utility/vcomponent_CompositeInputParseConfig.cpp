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
 * @brief Validated KVP-backed parser for the CompositeInput HFP profile.
 */

#include "utility/vcomponent_CompositeInputParseConfig.h"

#include "common/logger.h"
#include "utility/vcomponent_CompositeInputHelper.h"

#include <ut_kvp_profile.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace vcomponent::compositeinput::utility
{

namespace
{

constexpr const char* kCompositeInputRoot = "compositeinput";
constexpr const char* kInterfaceVersionKey = "compositeinput.interfaceVersion";
constexpr const char* kPortsKey = "compositeinput.ports";
constexpr const char* kPlatformCapabilitiesKey = "compositeinput.platformCapabilities";
constexpr size_t kKvpBufferSize = UT_KVP_MAX_ELEMENT_SIZE;

/**
 * @brief Set an optional parser error message.
 *
 * @param[out] outError Optional caller-owned error output.
 * @param[in] message Error detail identifying the failing HFP key.
 */
void setError(std::string* outError, const std::string& message)
{
    if (outError != nullptr)
    {
        *outError = message;
    }
}

/**
 * @brief Report whether a KVP key is present.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] key Fully-qualified HFP key.
 *
 * @return True when the key exists.
 */
bool fieldPresent(ut_kvp_instance_t* instance, const std::string& key)
{
    return instance != nullptr && ut_kvp_fieldPresent(instance, key.c_str());
}

/**
 * @brief Parse a decimal or base-prefixed integer into int32_t.
 *
 * @param[in] value Serialized KVP value.
 * @param[out] outValue Parsed integer.
 *
 * @return True when the complete value is representable as int32_t.
 */
bool parseInt32(const std::string& value, int32_t* outValue)
{
    if (outValue == nullptr || value.empty())
    {
        return false;
    }

    errno = 0;
    char* endPtr = nullptr;
    const long parsed = std::strtol(value.c_str(), &endPtr, 0);
    if (errno != 0 || endPtr == value.c_str() || (endPtr != nullptr && *endPtr != '\0') ||
        parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max())
    {
        return false;
    }

    *outValue = static_cast<int32_t>(parsed);
    return true;
}

/**
 * @brief Parse a serialized KVP boolean.
 *
 * @param[in] value Serialized KVP value.
 * @param[out] outValue Parsed boolean.
 *
 * @return True when the value is an accepted boolean representation.
 */
bool parseBool(const std::string& value, bool* outValue)
{
    if (outValue == nullptr)
    {
        return false;
    }

    if (value == "true" || value == "True" || value == "TRUE" || value == "1")
    {
        *outValue = true;
        return true;
    }

    if (value == "false" || value == "False" || value == "FALSE" || value == "0")
    {
        *outValue = false;
        return true;
    }

    return false;
}

/**
 * @brief Read a string field, enforcing presence when requested.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] key Fully-qualified HFP key.
 * @param[out] outValue Parsed output.
 * @param[out] outError Optional parser error output.
 * @param[in] required Whether a missing field is an error.
 *
 * @return True on a successful read or an allowed absent optional field.
 */
bool readStringField(
    ut_kvp_instance_t* instance,
    const std::string& key,
    std::string* outValue,
    std::string* outError,
    bool required)
{
    if (outValue == nullptr)
    {
        setError(outError, "internal parser error: null string output for key " + key);
        return false;
    }

    if (!fieldPresent(instance, key))
    {
        if (required)
        {
            setError(outError, "required CompositeInput HFP field is missing: " + key);
            return false;
        }
        return true;
    }

    char buffer[kKvpBufferSize] = {0};
    const ut_kvp_status_t status =
        ut_kvp_getStringField(instance, key.c_str(), buffer, sizeof(buffer));
    if (status != UT_KVP_STATUS_SUCCESS)
    {
        setError(outError, "failed to read CompositeInput HFP string field: " + key);
        return false;
    }

    *outValue = buffer;
    if (required && outValue->empty())
    {
        setError(outError, "required CompositeInput HFP field is empty: " + key);
        return false;
    }

    return true;
}

/**
 * @brief Read a required or optional integer HFP field.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] key Fully-qualified HFP key.
 * @param[out] outValue Parsed output.
 * @param[out] outError Optional parser error output.
 * @param[in] required Whether a missing field is an error.
 *
 * @return True on a valid integer or an allowed absent optional field.
 */
bool readInt32Field(
    ut_kvp_instance_t* instance,
    const std::string& key,
    int32_t* outValue,
    std::string* outError,
    bool required)
{
    std::string value;
    if (!readStringField(instance, key, &value, outError, required))
    {
        return false;
    }

    if (value.empty() && !required)
    {
        return true;
    }

    if (!parseInt32(value, outValue))
    {
        setError(outError,
                 "failed to parse CompositeInput HFP integer field: " + key + " value=" + value);
        return false;
    }

    return true;
}

/**
 * @brief Read a required or optional boolean HFP field.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] key Fully-qualified HFP key.
 * @param[out] outValue Parsed output.
 * @param[out] outError Optional parser error output.
 * @param[in] required Whether a missing field is an error.
 *
 * @return True on a valid boolean or an allowed absent optional field.
 */
bool readBoolField(
    ut_kvp_instance_t* instance,
    const std::string& key,
    bool* outValue,
    std::string* outError,
    bool required)
{
    std::string value;
    if (!readStringField(instance, key, &value, outError, required))
    {
        return false;
    }

    if (value.empty() && !required)
    {
        return true;
    }

    if (!parseBool(value, outValue))
    {
        setError(outError,
                 "failed to parse CompositeInput HFP boolean field: " + key + " value=" + value);
        return false;
    }

    return true;
}

/**
 * @brief Read a string list from a KVP profile.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] key Fully-qualified HFP list key.
 * @param[out] outValues Parsed list.
 * @param[out] outError Optional parser error output.
 * @param[in] required Whether the list must exist.
 *
 * @return True on success.
 */
bool readStringList(
    ut_kvp_instance_t* instance,
    const std::string& key,
    std::vector<std::string>* outValues,
    std::string* outError,
    bool required)
{
    if (outValues == nullptr)
    {
        setError(outError, "internal parser error: null string-list output for key " + key);
        return false;
    }

    outValues->clear();
    if (!fieldPresent(instance, key))
    {
        if (required)
        {
            setError(outError, "required CompositeInput HFP list is missing: " + key);
            return false;
        }
        return true;
    }

    const uint32_t count = ut_kvp_getListCount(instance, key.c_str());
    outValues->reserve(count);

    for (uint32_t index = 0; index < count; ++index)
    {
        std::string value;
        const std::string itemKey = key + "." + std::to_string(index);
        if (!readStringField(instance, itemKey, &value, outError, true))
        {
            return false;
        }

        outValues->push_back(value);
    }

    return true;
}

/**
 * @brief Validate tokens and enforce property-metadata declaration order.
 *
 * @param[in] properties Supported-property token list.
 * @param[in] metadata Property metadata entries.
 * @param[in] keyPrefix HFP key prefix used in validation errors.
 * @param[out] outError Optional parser error output.
 *
 * @return True when all tokens map to supported AIDL enums and metadata aligns.
 */
bool validateProperties(
    const std::vector<std::string>& properties,
    const std::vector<CompositeInputPropertyMetadataConfig>& metadata,
    const std::string& keyPrefix,
    std::string* outError)
{
    std::unordered_set<std::string> propertyTokens;
    propertyTokens.reserve(properties.size());

    for (size_t index = 0; index < properties.size(); ++index)
    {
        ::com::rdk::hal::compositeinput::PortProperty property{};
        if (!portPropertyFromString(properties[index], &property))
        {
            setError(outError,
                     "unsupported CompositeInput PortProperty token at " + keyPrefix +
                         ".supportedProperties." + std::to_string(index) + ": " + properties[index]);
            return false;
        }

        if (!propertyTokens.insert(properties[index]).second)
        {
            setError(outError,
                     "duplicate CompositeInput PortProperty token at " + keyPrefix +
                         ".supportedProperties." + std::to_string(index) + ": " + properties[index]);
            return false;
        }
    }

    if (metadata.size() != properties.size())
    {
        setError(outError,
                 "CompositeInput propertyMetadata count must match supportedProperties at " +
                     keyPrefix);
        return false;
    }

    for (size_t index = 0; index < metadata.size(); ++index)
    {
        const auto& entry = metadata[index];
        ::com::rdk::hal::compositeinput::PortProperty property{};
        if (!portPropertyFromString(entry.key, &property))
        {
            setError(outError,
                     "unsupported CompositeInput propertyMetadata key at " + keyPrefix +
                         ".propertyMetadata." + std::to_string(index) + ".key: " + entry.key);
            return false;
        }

        ::com::rdk::hal::compositeinput::PropertyMetadata::PropertyType type{};
        if (!propertyTypeFromString(entry.type, &type))
        {
            setError(outError,
                     "unsupported CompositeInput propertyMetadata type at " + keyPrefix +
                         ".propertyMetadata." + std::to_string(index) + ".type: " + entry.type);
            return false;
        }

        if (entry.key != properties[index])
        {
            setError(outError,
                     "CompositeInput propertyMetadata ordering does not match supportedProperties at " +
                         keyPrefix + ".propertyMetadata." + std::to_string(index));
            return false;
        }

        if (entry.description.empty())
        {
            setError(outError,
                     "required CompositeInput HFP field is empty: " + keyPrefix +
                         ".propertyMetadata." + std::to_string(index) + ".description");
            return false;
        }
    }

    return true;
}

/**
 * @brief Read a property-metadata list.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] key Fully-qualified HFP list key.
 * @param[out] outValues Parsed metadata entries.
 * @param[out] outError Optional parser error output.
 *
 * @return True on success.
 */
bool readPropertyMetadata(
    ut_kvp_instance_t* instance,
    const std::string& key,
    std::vector<CompositeInputPropertyMetadataConfig>* outValues,
    std::string* outError)
{
    if (outValues == nullptr)
    {
        setError(outError, "internal parser error: null metadata output for key " + key);
        return false;
    }

    outValues->clear();
    if (!fieldPresent(instance, key))
    {
        setError(outError, "required CompositeInput HFP list is missing: " + key);
        return false;
    }

    const uint32_t count = ut_kvp_getListCount(instance, key.c_str());
    outValues->reserve(count);

    for (uint32_t index = 0; index < count; ++index)
    {
        const std::string prefix = key + "." + std::to_string(index) + ".";
        CompositeInputPropertyMetadataConfig entry{};

        if (!readStringField(instance, prefix + "key", &entry.key, outError, true) ||
            !readStringField(instance, prefix + "type", &entry.type, outError, true) ||
            !readBoolField(instance, prefix + "readOnly", &entry.readOnly, outError, true) ||
            !readBoolField(instance, prefix + "isMetric", &entry.isMetric, outError, true) ||
            !readStringField(instance, prefix + "description", &entry.description, outError, true))
        {
            return false;
        }

        outValues->push_back(std::move(entry));
    }

    return true;
}

/**
 * @brief Read and validate one port configuration.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[in] index Port list index.
 * @param[out] outPort Parsed port configuration.
 * @param[out] outError Optional parser error output.
 *
 * @return True when the port is complete and valid.
 */
bool readPort(
    ut_kvp_instance_t* instance,
    uint32_t index,
    CompositeInputPortConfig* outPort,
    std::string* outError)
{
    if (outPort == nullptr)
    {
        setError(outError, "internal parser error: null port output");
        return false;
    }

    *outPort = CompositeInputPortConfig{};
    const std::string prefix = std::string(kPortsKey) + "." + std::to_string(index) + ".";

    if (!readInt32Field(instance, prefix + "id", &outPort->id, outError, true) ||
        !readStringField(instance, prefix + "name", &outPort->name, outError, true) ||
        !readStringField(instance, prefix + "description", &outPort->description, outError, true) ||
        !readStringList(
            instance, prefix + "supportedProperties", &outPort->supportedProperties, outError, true) ||
        !readPropertyMetadata(
            instance, prefix + "propertyMetadata", &outPort->propertyMetadata, outError))
    {
        return false;
    }

    if (outPort->id < 0)
    {
        setError(outError, "CompositeInput port ID must be nonnegative: " + prefix + "id");
        return false;
    }

    return validateProperties(
        outPort->supportedProperties, outPort->propertyMetadata, prefix.substr(0, prefix.size() - 1), outError);
}

/**
 * @brief Read and validate platform-wide capabilities.
 *
 * @param[in] instance Open KVP profile instance.
 * @param[out] outConfig Parsed profile configuration.
 * @param[out] outError Optional parser error output.
 *
 * @return True when platform capabilities are complete and valid.
 */
bool readPlatformCapabilities(
    ut_kvp_instance_t* instance,
    CompositeInputHfpConfig* outConfig,
    std::string* outError)
{
    if (outConfig == nullptr)
    {
        setError(outError, "internal parser error: null configuration output");
        return false;
    }

    if (!fieldPresent(instance, kPlatformCapabilitiesKey))
    {
        setError(outError,
                 "required CompositeInput HFP profile is missing: compositeinput.platformCapabilities");
        return false;
    }

    const std::string prefix = std::string(kPlatformCapabilitiesKey) + ".";
    if (!readStringField(instance, prefix + "halVersion", &outConfig->halVersion, outError, true) ||
        !readInt32Field(instance, prefix + "maxPorts", &outConfig->maxPorts, outError, true) ||
        !readInt32Field(instance,
                            prefix + "maximumConcurrentStartedPorts",
                            &outConfig->maximumConcurrentStartedPorts,
                            outError,
                            true) ||
        !readStringList(
            instance, prefix + "supportedProperties", &outConfig->supportedProperties, outError, true) ||
        !readPropertyMetadata(
            instance, prefix + "propertyMetadata", &outConfig->propertyMetadata, outError) ||
        !readBoolField(instance,
                           prefix + "features.macrovisionDetectionSupported",
                           &outConfig->features.macrovisionDetectionSupported,
                           outError,
                           true))
    {
        return false;
    }

    if (outConfig->maxPorts < 1)
    {
        setError(outError, "CompositeInput maxPorts must be at least one: " + prefix + "maxPorts");
        return false;
    }

    if (outConfig->maximumConcurrentStartedPorts < 1)
    {
        setError(outError,
                 "CompositeInput maximumConcurrentStartedPorts must be at least one: " +
                     prefix + "maximumConcurrentStartedPorts");
        return false;
    }

    return validateProperties(
        outConfig->supportedProperties,
        outConfig->propertyMetadata,
        prefix.substr(0, prefix.size() - 1),
        outError);
}

} // namespace

void* vcomponent_CompositeInput_kvpCreateInstance(char* fileName)
{
    if (fileName == nullptr || std::strlen(fileName) == 0)
    {
        return nullptr;
    }

    ut_kvp_instance_t* instance = ut_kvp_createInstance();
    if (instance == nullptr)
    {
        return nullptr;
    }

    if (ut_kvp_open(instance, fileName) != UT_KVP_STATUS_SUCCESS)
    {
        ut_kvp_destroyInstance(instance);
        return nullptr;
    }

    return static_cast<void*>(instance);
}

void vcomponent_CompositeInput_kvpDestroyInstance(void* instance)
{
    if (instance != nullptr)
    {
        ut_kvp_destroyInstance(static_cast<ut_kvp_instance_t*>(instance));
    }
}

bool vcomponent_CompositeInput_parseConfig(
    char* configurationFile,
    CompositeInputHfpConfig& compositeInputConfiguration,
    std::string* outError)
{
    compositeInputConfiguration = CompositeInputHfpConfig{};
    if (outError != nullptr)
    {
        outError->clear();
    }

    if (configurationFile == nullptr || std::strlen(configurationFile) == 0)
    {
        setError(outError, "CompositeInput HFP YAML path is empty");
        return false;
    }

    auto* instance = static_cast<ut_kvp_instance_t*>(
        vcomponent_CompositeInput_kvpCreateInstance(configurationFile));
    if (instance == nullptr)
    {
        setError(outError,
                 std::string("failed to open CompositeInput HFP YAML: ") + configurationFile);
        return false;
    }

    CompositeInputHfpConfig parsedConfig{};
    bool success = fieldPresent(instance, kCompositeInputRoot);
    if (!success)
    {
        setError(outError, "missing top-level CompositeInput HFP profile: compositeinput");
    }

    if (success)
    {
        success = readStringField(
            instance, kInterfaceVersionKey, &parsedConfig.interfaceVersion, outError, true);
    }

    if (success && !fieldPresent(instance, kPortsKey))
    {
        setError(outError, "required CompositeInput HFP list is missing: compositeinput.ports");
        success = false;
    }

    if (success)
    {
        const uint32_t portCount = ut_kvp_getListCount(instance, kPortsKey);
        if (portCount == 0)
        {
            setError(outError, "CompositeInput HFP must declare at least one port: compositeinput.ports");
            success = false;
        }

        std::unordered_set<int32_t> portIds;
        parsedConfig.ports.reserve(portCount);
        for (uint32_t index = 0; success && index < portCount; ++index)
        {
            CompositeInputPortConfig port{};
            success = readPort(instance, index, &port, outError);
            if (success && !portIds.insert(port.id).second)
            {
                setError(outError,
                         "duplicate CompositeInput port ID at compositeinput.ports." +
                             std::to_string(index) + ".id: " + std::to_string(port.id));
                success = false;
            }

            if (success)
            {
                parsedConfig.ports.push_back(std::move(port));
            }
        }
    }

    if (success)
    {
        success = readPlatformCapabilities(instance, &parsedConfig, outError);
    }

    vcomponent_CompositeInput_kvpDestroyInstance(instance);

    if (!success)
    {
        compositeInputConfiguration = CompositeInputHfpConfig{};
        return false;
    }

    compositeInputConfiguration = std::move(parsedConfig);
    LOGF_INFO("CompositeInput HFP parsing completed successfully. path=%s",
              configurationFile);
    return true;
}

bool loadCompositeInputHfpConfigFromYaml(
    const std::string& path,
    CompositeInputHfpConfig* outConfig,
    std::string* outError)
{
    if (outConfig == nullptr)
    {
        setError(outError, "outConfig is null");
        return false;
    }

    std::string mutablePath = path;
    return vcomponent_CompositeInput_parseConfig(mutablePath.data(), *outConfig, outError);
}

} // namespace vcomponent::compositeinput::utility
