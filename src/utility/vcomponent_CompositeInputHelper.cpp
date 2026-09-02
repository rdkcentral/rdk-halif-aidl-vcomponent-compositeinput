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
 * @file vcomponent_CompositeInputHelper.cpp
 * @brief Helper utilities and HFP token to AIDL enum conversions.
 */

#include "utility/vcomponent_CompositeInputHelper.h"

#include "common/logger.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace vcomponent::compositeinput::utility
{

namespace
{
using ::com::rdk::hal::compositeinput::PortProperty;
using ::com::rdk::hal::compositeinput::PropertyMetadata;
using ::com::rdk::hal::compositeinput::SignalStatus;
using ::com::rdk::hal::compositeinput::VideoResolution;

constexpr const char* kLogPrefix = "[VDEVICE_COMPOSITEINPUT]<CompositeInputHelper>";

/**
 * @brief Table entry pairing an HFP token with a PortProperty enum value.
 */
struct PortPropertyToken
{
    const char* token;
    PortProperty value;
};

/**
 * @brief Table entry pairing an HFP token with a PropertyType enum value.
 */
struct PropertyTypeToken
{
    const char* token;
    PropertyMetadata::PropertyType value;
};

/**
 * @brief Table entry pairing a UT token with a SignalStatus enum value.
 */
struct SignalStatusToken
{
    const char* token;
    SignalStatus value;
};

/**
 * @brief Table entry pairing a UT command token with its dispatch value.
 */
struct UtCommandToken
{
    const char* token;
    UtCommand value;
};

// Keys and slot semantics follow com.rdk.hal.compositeinput.PortProperty.
constexpr PortPropertyToken kPortPropertyTokens[] = {
    {"SIGNAL_STRENGTH", PortProperty::SIGNAL_STRENGTH},
    {"SIGNAL_QUALITY", PortProperty::SIGNAL_QUALITY},
    {"METRIC_SIGNAL_LOCK_TIME", PortProperty::METRIC_SIGNAL_LOCK_TIME},
    {"METRIC_SIGNAL_DROPS", PortProperty::METRIC_SIGNAL_DROPS},
    {"METRIC_UPTIME", PortProperty::METRIC_UPTIME},
    {"METRIC_SIGNAL_LOCK_COUNT", PortProperty::METRIC_SIGNAL_LOCK_COUNT},
    {"METRIC_LAST_SIGNAL_LOCK_TIME", PortProperty::METRIC_LAST_SIGNAL_LOCK_TIME},
    {"METRIC_LAST_RESET_TIMESTAMP", PortProperty::METRIC_LAST_RESET_TIMESTAMP},
};

constexpr SignalStatusToken kSignalStatusTokens[] = {
    {"NO_SIGNAL", SignalStatus::NO_SIGNAL},
    {"UNSTABLE", SignalStatus::UNSTABLE},
    {"NOT_SUPPORTED", SignalStatus::NOT_SUPPORTED},
    {"STABLE", SignalStatus::STABLE},
};

constexpr UtCommandToken kUtCommandTokens[] = {
    {"setConnection", UtCommand::SET_CONNECTION},
    {"setSignalStatus", UtCommand::SET_SIGNAL_STATUS},
    {"setProperty", UtCommand::SET_PROPERTY},
    {"setVideoMode", UtCommand::SET_VIDEO_MODE},
    {"clearVideoMode", UtCommand::CLEAR_VIDEO_MODE},
    // The host control plane emits snake_case command tokens; both spellings
    // must resolve to the same dispatch value.
    {"connection_status", UtCommand::SET_CONNECTION},
    {"signal_status", UtCommand::SET_SIGNAL_STATUS},
    {"set_property", UtCommand::SET_PROPERTY},
    {"property_changed", UtCommand::SET_PROPERTY},
    {"video_mode", UtCommand::SET_VIDEO_MODE},
    {"clear_video_mode", UtCommand::CLEAR_VIDEO_MODE},
};

constexpr PropertyTypeToken kPropertyTypeTokens[] = {
    {"BOOLEAN", PropertyMetadata::PropertyType::BOOLEAN},
    {"INTEGER", PropertyMetadata::PropertyType::INTEGER},
    {"LONG", PropertyMetadata::PropertyType::LONG},
    {"FLOAT", PropertyMetadata::PropertyType::FLOAT},
    {"DOUBLE", PropertyMetadata::PropertyType::DOUBLE},
    {"STRING", PropertyMetadata::PropertyType::STRING},
};
} // namespace

std::optional<std::string> readFileToString(const std::string& path)
{
    std::ifstream inputFile(path);
    if (!inputFile.is_open())
    {
        return std::nullopt;
    }

    std::ostringstream contentStream;
    contentStream << inputFile.rdbuf();
    return contentStream.str();
}

std::string trim(const std::string& input)
{
    size_t startIndex = 0;
    while (startIndex < input.size() &&
           std::isspace(static_cast<unsigned char>(input[startIndex])))
    {
        ++startIndex;
    }

    size_t endIndex = input.size();
    while (endIndex > startIndex &&
           std::isspace(static_cast<unsigned char>(input[endIndex - 1])))
    {
        --endIndex;
    }

    return input.substr(startIndex, endIndex - startIndex);
}

bool portPropertyFromString(
    const std::string& token,
    ::com::rdk::hal::compositeinput::PortProperty* outValue)
{
    if (outValue == nullptr)
    {
        return false;
    }

    const std::string normalized = trim(token);
    for (const auto& entry : kPortPropertyTokens)
    {
        if (normalized == entry.token)
        {
            *outValue = entry.value;
            return true;
        }
    }

    return false;
}

bool signalStatusFromString(
    const std::string& token,
    ::com::rdk::hal::compositeinput::SignalStatus* outValue)
{
    if (outValue == nullptr)
    {
        return false;
    }

    const std::string normalized = trim(token);
    for (const auto& entry : kSignalStatusTokens)
    {
        if (normalized == entry.token)
        {
            *outValue = entry.value;
            return true;
        }
    }

    return false;
}

bool utCommandFromString(const std::string& token, UtCommand* outValue)
{
    if (outValue == nullptr)
    {
        return false;
    }

    const std::string normalized = trim(token);
    for (const auto& entry : kUtCommandTokens)
    {
        if (normalized == entry.token)
        {
            *outValue = entry.value;
            return true;
        }
    }

    return false;
}

bool makeVideoResolution(
    int32_t pixelWidth,
    int32_t pixelHeight,
    bool interlaced,
    float frameRateInHz,
    ::com::rdk::hal::compositeinput::VideoResolution* outValue)
{
    if (outValue == nullptr)
    {
        return false;
    }

    if (pixelWidth <= 0 || pixelHeight <= 0 || frameRateInHz <= 0.0f)
    {
        LOGF_WARN(
            "%s invalid video resolution %dx%d%s at %.3f Hz",
            kLogPrefix,
            pixelWidth,
            pixelHeight,
            interlaced ? "i" : "p",
            frameRateInHz);
        return false;
    }

    outValue->pixelWidth = pixelWidth;
    outValue->pixelHeight = pixelHeight;
    outValue->interlaced = interlaced;
    outValue->frameRateInHz = frameRateInHz;
    return true;
}

bool propertyTypeFromString(
    const std::string& token,
    ::com::rdk::hal::compositeinput::PropertyMetadata::PropertyType* outValue)
{
    if (outValue == nullptr)
    {
        return false;
    }

    const std::string normalized = trim(token);
    for (const auto& entry : kPropertyTypeTokens)
    {
        if (normalized == entry.token)
        {
            *outValue = entry.value;
            return true;
        }
    }

    return false;
}

std::vector<::com::rdk::hal::compositeinput::PortProperty> toPortProperties(
    const std::vector<std::string>& tokens)
{
    std::vector<PortProperty> properties;
    properties.reserve(tokens.size());

    for (const auto& token : tokens)
    {
        PortProperty value{};
        if (portPropertyFromString(token, &value))
        {
            properties.push_back(value);
        }
        else
        {
            LOGF_ERROR("%s Unknown PortProperty token in profile: '%s'",
                       kLogPrefix,
                       token.c_str());
        }
    }

    return properties;
}

std::vector<::com::rdk::hal::compositeinput::PropertyMetadata> toPropertyMetadata(
    const std::vector<CompositeInputPropertyMetadataConfig>& entries)
{
    std::vector<PropertyMetadata> metadata;
    metadata.reserve(entries.size());

    for (const auto& entry : entries)
    {
        PortProperty key{};
        if (!portPropertyFromString(entry.key, &key))
        {
            LOGF_ERROR("%s Unknown PropertyMetadata.key token in profile: '%s'",
                       kLogPrefix,
                       entry.key.c_str());
            continue;
        }

        PropertyMetadata::PropertyType type{};
        if (!propertyTypeFromString(entry.type, &type))
        {
            LOGF_ERROR("%s Unknown PropertyMetadata.type token in profile: '%s' (key=%s)",
                       kLogPrefix,
                       entry.type.c_str(),
                       entry.key.c_str());
            continue;
        }

        PropertyMetadata item{};
        item.key = key;
        item.type = type;
        item.readOnly = entry.readOnly;
        item.isMetric = entry.isMetric;

        // Conformance clients compare the parcelable for full equality, so the
        // description must match the YAML exactly.
        item.description = entry.description;

        metadata.push_back(std::move(item));
    }

    if (!entries.empty() && metadata.size() != entries.size())
    {
        LOGF_ERROR("%s propertyMetadata mapping mismatch: profile=%zu mapped=%zu",
                   kLogPrefix,
                   entries.size(),
                   metadata.size());
    }

    return metadata;
}

std::optional<std::vector<std::optional<::com::rdk::hal::compositeinput::PropertyMetadata>>>
toNullablePropertyMetadata(const std::vector<CompositeInputPropertyMetadataConfig>& entries)
{
    // The AIDL field is @nullable: engaged only when the profile declares entries.
    if (entries.empty())
    {
        return std::nullopt;
    }

    const std::vector<PropertyMetadata> metadata = toPropertyMetadata(entries);

    std::vector<std::optional<PropertyMetadata>> nullableMetadata;
    nullableMetadata.reserve(metadata.size());

    for (const auto& item : metadata)
    {
        // Always engaged optionals (no nullopt elements).
        nullableMetadata.emplace_back(item);
    }

    return nullableMetadata;
}

} // namespace vcomponent::compositeinput::utility
