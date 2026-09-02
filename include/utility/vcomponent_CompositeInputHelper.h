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
 * @file vcomponent_CompositeInputHelper.h
 * @brief Helper utilities for the CompositeInput component.
 *
 * Besides generic string/file helpers, this header hosts the HFP token to AIDL
 * enum conversions. The HFP profile stores `PortProperty` and
 * `PropertyMetadata.PropertyType` values as unquoted enum identifiers, and the
 * parsed configuration model keeps them as strings so the parser stays free of
 * AIDL dependencies. These helpers are the single place where the conversion to
 * generated AIDL enums happens, at the Binder boundary.
 */

#include "utility/vcomponent_CompositeInputHfpConfigUtils.h"

#include <com/rdk/hal/compositeinput/PortProperty.h>
#include <com/rdk/hal/compositeinput/PropertyMetadata.h>
#include <com/rdk/hal/compositeinput/SignalStatus.h>
#include <com/rdk/hal/compositeinput/VideoResolution.h>

#include <optional>
#include <string>
#include <vector>

namespace vcomponent::compositeinput::utility
{

/**
 * @brief Recognised CompositeInput UT control-plane commands.
 */
enum class UtCommand
{
    SET_CONNECTION,
    SET_SIGNAL_STATUS,
    SET_PROPERTY,
    SET_VIDEO_MODE,
    CLEAR_VIDEO_MODE,
};

// PUBLIC_INTERFACE
/**
 * @brief Read an entire file into a string.
 *
 * @param[in] path  Path to the file.
 *
 * @return File contents, or std::nullopt on error.
 */
std::optional<std::string> readFileToString(const std::string& path);

// PUBLIC_INTERFACE
/**
 * @brief Trim leading/trailing whitespace from a string.
 *
 * @param[in] input  Input string.
 *
 * @return Trimmed copy.
 */
std::string trim(const std::string& input);

// PUBLIC_INTERFACE
/**
 * @brief Convert an HFP `PortProperty` token to its AIDL enum value.
 *
 * @param[in]  token     Enum identifier from the HFP (for example `SIGNAL_QUALITY`).
 * @param[out] outValue  Receives the mapped AIDL enum value on success.
 *
 * @return True when the token is a known PortProperty identifier.
 */
bool portPropertyFromString(
    const std::string& token,
    ::com::rdk::hal::compositeinput::PortProperty* outValue);

// PUBLIC_INTERFACE
/**
 * @brief Convert a UT `SignalStatus` token to its AIDL enum value.
 *
 * @param[in]  token     Enum identifier from UT (for example `STABLE`).
 * @param[out] outValue  Receives the mapped AIDL enum value on success.
 *
 * @return True when the token is a known SignalStatus identifier.
 */
bool signalStatusFromString(
    const std::string& token,
    ::com::rdk::hal::compositeinput::SignalStatus* outValue);

// PUBLIC_INTERFACE
/**
 * @brief Convert a UT command token to its internal dispatch value.
 *
 * Both the camelCase spellings (`setConnection`) and the snake_case spellings
 * emitted by the host control plane (`connection_status`, `signal_status`,
 * `video_mode`) are accepted and map onto the same dispatch value.
 *
 * @param[in]  token     Command identifier from UT (for example `setConnection`
 *                       or `connection_status`).
 * @param[out] outValue  Receives the mapped command value on success.
 *
 * @return True when the token is a supported CompositeInput UT command.
 */
bool utCommandFromString(const std::string& token, UtCommand* outValue);

// PUBLIC_INTERFACE
/**
 * @brief Build a valid AIDL VideoResolution parcelable from scalar values.
 *
 * @param[in]  pixelWidth     Horizontal resolution in pixels.
 * @param[in]  pixelHeight    Vertical resolution in pixels.
 * @param[in]  interlaced     Whether the video mode is interlaced.
 * @param[in]  frameRateInHz  Frame rate in hertz.
 * @param[out] outValue       Receives the constructed parcelable on success.
 *
 * @return True if the dimensions and frame rate are positive and the output was
 *         populated; otherwise false.
 */
bool makeVideoResolution(
    int32_t pixelWidth,
    int32_t pixelHeight,
    bool interlaced,
    float frameRateInHz,
    ::com::rdk::hal::compositeinput::VideoResolution* outValue);

// PUBLIC_INTERFACE
/**
 * @brief Convert an HFP property type token to its AIDL enum value.
 *
 * @param[in]  token     Enum identifier from the HFP (for example `LONG`).
 * @param[out] outValue  Receives the mapped AIDL enum value on success.
 *
 * @return True when the token is a known PropertyType identifier.
 */
bool propertyTypeFromString(
    const std::string& token,
    ::com::rdk::hal::compositeinput::PropertyMetadata::PropertyType* outValue);

// PUBLIC_INTERFACE
/**
 * @brief Convert a list of HFP property tokens to AIDL PortProperty values.
 *
 * Unknown tokens are skipped so a newer HFP never breaks an older skeleton.
 *
 * @param[in] tokens  HFP `supportedProperties` entries.
 *
 * @return Mapped AIDL PortProperty values in HFP declaration order.
 */
std::vector<::com::rdk::hal::compositeinput::PortProperty> toPortProperties(
    const std::vector<std::string>& tokens);

// PUBLIC_INTERFACE
/**
 * @brief Convert HFP property metadata entries to AIDL PropertyMetadata values.
 *
 * Entries with an unknown key or type token are skipped.
 *
 * @param[in] entries  HFP `propertyMetadata` entries.
 *
 * @return Mapped AIDL PropertyMetadata values in HFP declaration order.
 */
std::vector<::com::rdk::hal::compositeinput::PropertyMetadata> toPropertyMetadata(
    const std::vector<CompositeInputPropertyMetadataConfig>& entries);

// PUBLIC_INTERFACE
/**
 * @brief Convert HFP property metadata entries to the generated AIDL field type.
 *
 * The AIDL contract declares `@nullable PropertyMetadata[] propertyMetadata` in
 * both `PortCapabilities` and `PlatformCapabilities`. The C++ backend maps that
 * to a nullable array of nullable elements, i.e.
 * `std::optional<std::vector<std::optional<PropertyMetadata>>>`, so a plain
 * `std::vector<PropertyMetadata>` cannot be assigned to it directly. This helper
 * performs that mapping in one place, at the Binder boundary.
 *
 * Entries with an unknown key or type token are skipped (see toPropertyMetadata).
 * When no entry maps successfully, std::nullopt is returned so the field is
 * marshalled as a null array instead of an empty one.
 *
 * @param[in] entries  HFP `propertyMetadata` entries.
 *
 * @return Mapped AIDL PropertyMetadata values in HFP declaration order, or
 *         std::nullopt when there is nothing to report.
 */
std::optional<std::vector<std::optional<::com::rdk::hal::compositeinput::PropertyMetadata>>>
toNullablePropertyMetadata(const std::vector<CompositeInputPropertyMetadataConfig>& entries);

} // namespace vcomponent::compositeinput::utility
