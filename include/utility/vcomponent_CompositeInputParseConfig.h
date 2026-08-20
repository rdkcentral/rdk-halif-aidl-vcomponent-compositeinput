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
 * @brief Public entrypoints for loading CompositeInput configuration from HFP YAML.
 *
 * This module mirrors the structure used by the other vcomponent parsers: create
 * a KVP instance with vcomponent_CompositeInput_kvpCreateInstance(), walk profile
 * keys, then release the instance with vcomponent_CompositeInput_kvpDestroyInstance().
 *
 * The parser entrypoint is component-named so service/controller integration
 * follows the vcomponent_CompositeInput_* convention instead of a generic load*
 * helper.
 */

#include "utility/vcomponent_CompositeInputHfpConfigUtils.h"

#include <string>

namespace vcomponent::compositeinput::utility
{

// PUBLIC_INTERFACE
/**
 * @brief Create and open a ut-core/KVP instance for a YAML configuration file.
 *
 * @param[in] fileName  YAML file path. Must not be nullptr.
 *
 * @return Opaque KVP instance on success, nullptr on failure.
 */
void* vcomponent_CompositeInput_kvpCreateInstance(char* fileName);

// PUBLIC_INTERFACE
/**
 * @brief Destroy a KVP instance created by vcomponent_CompositeInput_kvpCreateInstance().
 *
 * @param[in] instance  Opaque KVP instance. A nullptr value is ignored.
 */
void vcomponent_CompositeInput_kvpDestroyInstance(void* instance);

// PUBLIC_INTERFACE
/**
 * @brief Parse CompositeInput HFP YAML into CompositeInputHfpConfig.
 *
 * The parser reads the `compositeinput` profile from the supplied YAML file
 * using ut-core/ut-control KVP APIs. Field names intentionally match the YAML
 * names, and the internal flow follows the component parser convention: create
 * KVP instance, parse list/profile sections with prefixed keys, then destroy the
 * KVP instance.
 *
 * Expected YAML keys:
 *  - ports[] : { id, name, description }
 *  - maximumConcurrentStartedPorts
 *
 * Validation rules to enforce once implemented:
 *  - Each port requires id, name and description
 *  - Port IDs must be unique
 *  - At least one port must exist
 *  - maximumConcurrentStartedPorts >= 1
 *
 * @param[in]  configurationFile           YAML file path. Must not be nullptr.
 * @param[out] compositeInputConfiguration Output config populated on success.
 * @param[out] outError                    Optional error string populated on failure.
 *
 * @return true on success, false on error.
 */
bool vcomponent_CompositeInput_parseConfig(
    char* configurationFile,
    CompositeInputHfpConfig& compositeInputConfiguration,
    std::string* outError);

// PUBLIC_INTERFACE
/**
 * @brief Compatibility wrapper around vcomponent_CompositeInput_parseConfig().
 *
 * This is the entrypoint used by CompositeInputUtController; it forwards to
 * vcomponent_CompositeInput_parseConfig() once the parser is implemented.
 * Returning false makes the manager fall back to its built-in deterministic
 * profile.
 *
 * @param[in]  path      YAML file path.
 * @param[out] outConfig Output config. Must not be nullptr.
 * @param[out] outError  Optional error string populated on failure.
 *
 * @return true on success, false on error.
 */
bool loadCompositeInputHfpConfigFromYaml(
    const std::string& path,
    CompositeInputHfpConfig* outConfig,
    std::string* outError);

} // namespace vcomponent::compositeinput::utility
