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
 * @file vcomponent_CompositeInputParseConfig.h
 * @brief Public entrypoints for validated CompositeInput HFP configuration loading.
 *
 * The parser creates a ut-core KVP instance, opens the caller-provided YAML
 * profile, reads the `compositeinput` root, and destroys the instance on every
 * return path. It does not provide a built-in or file-independent fallback.
 */

#include "utility/vcomponent_CompositeInputHfpConfigUtils.h"

#include <string>

namespace vcomponent::compositeinput::utility
{

// PUBLIC_INTERFACE
/**
 * @brief Create and open a ut-core/KVP instance for a YAML configuration file.
 *
 * @param[in] fileName YAML file path. Must not be null or empty.
 *
 * @return Opaque KVP instance on success, or null when creation/opening fails.
 */
void* vcomponent_CompositeInput_kvpCreateInstance(char* fileName);

// PUBLIC_INTERFACE
/**
 * @brief Destroy a KVP instance created by vcomponent_CompositeInput_kvpCreateInstance().
 *
 * @param[in] instance Opaque KVP instance. A null value is ignored.
 */
void vcomponent_CompositeInput_kvpDestroyInstance(void* instance);

// PUBLIC_INTERFACE
/**
 * @brief Parse and validate CompositeInput HFP YAML.
 *
 * Required profile data includes the `compositeinput` root and interface
 * version; a nonempty port list; nonnegative unique port IDs; complete port
 * identity and property declarations; and complete platform capabilities. Every
 * declared property and metadata type must map to the supported AIDL vocabulary,
 * and property metadata must remain in the matching supported-property order.
 *
 * On any failure, this function clears @p compositeInputConfiguration, destroys
 * the temporary KVP instance, and records the failing key or validation rule in
 * @p outError when supplied.
 *
 * @param[in] configurationFile YAML file path. Must not be null or empty.
 * @param[out] compositeInputConfiguration Output configuration populated only on success.
 * @param[out] outError Optional key-specific error string.
 *
 * @return True when the complete profile is valid; false otherwise.
 */
bool vcomponent_CompositeInput_parseConfig(
    char* configurationFile,
    CompositeInputHfpConfig& compositeInputConfiguration,
    std::string* outError);

// PUBLIC_INTERFACE
/**
 * @brief Parse a CompositeInput HFP profile from an immutable path string.
 *
 * @param[in] path YAML file path.
 * @param[out] outConfig Output configuration. Must not be null.
 * @param[out] outError Optional key-specific error string.
 *
 * @return True when the complete profile is valid; false otherwise. On failure,
 *         @p outConfig is cleared and no fallback profile is substituted.
 */
bool loadCompositeInputHfpConfigFromYaml(
    const std::string& path,
    CompositeInputHfpConfig* outConfig,
    std::string* outError);

} // namespace vcomponent::compositeinput::utility
