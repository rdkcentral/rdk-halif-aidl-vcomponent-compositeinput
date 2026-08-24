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
 * @file vcomponent_CompositeInputService.h
 * @brief Public entrypoint definitions for the CompositeInput service executable.
 *
 * The executable publishes the CompositeInput manager under the AIDL-defined
 * `composite_input` Binder service name and joins the Binder thread pool.
 */

namespace vcomponent::compositeinput::service
{

/**
 * @brief Default relative path to the CompositeInput HFP YAML profile.
 */
inline constexpr const char* kDefaultHfpPath =
    "vcomponent_configurations/hfp-compositeinput.yaml";

// PUBLIC_INTERFACE
/**
 * @brief Print command-line usage for the CompositeInput service.
 *
 * @param argv0 The executable name used in the usage output.
 */
void printUsage(const char* argv0);

} // namespace vcomponent::compositeinput::service
