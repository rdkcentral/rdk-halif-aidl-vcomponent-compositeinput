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
 * @file vcomponent_CompositeInputUtController.h
 * @brief UT-controller facade for the CompositeInput vcomponent.
 */

#include "utility/vcomponent_CompositeInputHfpConfigUtils.h"

#include <ut.h>
#include <ut_control_plane.h>
#include <ut_kvp_profile.h>
#include <ut_log.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <tuple>

namespace vcomponent::compositeinput::controller
{

/**
 * @brief UT-controller facade for the CompositeInput vcomponent.
 *
 * This facade loads the HFP YAML into the parsed CompositeInput configuration
 * model, exposes a lightweight inventory string for higher-level runners, and
 * manages the UT control-plane endpoint used by vcomponent orchestration
 * clients.
 */
class CompositeInputUtController
{
public:
    /**
     * @brief Construct the controller.
     */
    CompositeInputUtController();

    /**
     * @brief Destroy the controller and stop any running UT control-plane instance.
     */
    ~CompositeInputUtController();

    CompositeInputUtController(const CompositeInputUtController&) = delete;
    CompositeInputUtController& operator=(const CompositeInputUtController&) = delete;

    // PUBLIC_INTERFACE
    /**
     * @brief Load CompositeInput configuration from the HFP YAML.
     *
     * @param[in]  hfpYamlPath  Path to hfp-compositeinput.yaml.
     * @param[out] outError     Optional output error string.
     *
     * @return true on success, false on parse or file access error.
     */
    bool loadConfiguration(const std::string& hfpYamlPath, std::string* outError);

    // PUBLIC_INTERFACE
    /**
     * @brief Build a text inventory for the CompositeInput component using the
     *        configured HFP path.
     *
     * @param[out] outInventory  Output inventory string. Must not be nullptr.
     * @param[out] outError      Optional output error string.
     *
     * @return true on success.
     */
    bool buildInventory(std::string* outInventory, std::string* outError) const;

    // PUBLIC_INTERFACE
    /**
     * @brief Initialize and start the UT control-plane endpoint.
     *
     * The controller registers for messages under the `compositeinput` profile
     * key, mirroring the reference vcomponent controller pattern. Incoming KVP
     * messages are copied into an internal FIFO queue so service code can map
     * them into CompositeInput vcomponent APIs.
     *
     * @param[in] port      TCP port used by the UT control plane.
     * @param[in] userData  Optional caller context returned with queued messages.
     *
     * @return true when the control plane is initialized and started.
     */
    bool init(std::uint16_t port, void* userData = nullptr);

    // PUBLIC_INTERFACE
    /**
     * @brief Stop the UT control-plane endpoint and release its resources.
     */
    void shutdown();

    // PUBLIC_INTERFACE
    /**
     * @brief Pop the next pending UT control-plane message, if available.
     *
     * @return Optional tuple containing message key, payload, and caller user data.
     */
    std::optional<std::tuple<std::string, std::string, void*>> getMessage();

    // PUBLIC_INTERFACE
    /**
     * @brief Return whether the UT control-plane endpoint has been initialized.
     *
     * @return true when a UT control-plane instance is active.
     */
    bool isRunning() const;

private:
    static void messageCallback(char* key, ut_kvp_instance_t* instance, void* userData);
    void pushMessage(char* key, ut_kvp_instance_t* instance);

    std::string m_hfpYamlPath;
    utility::CompositeInputHfpConfig m_hfpConfig;

    ut_controlPlane_instance_t* m_controlPlaneInstance;
    void* m_userData;

    mutable std::mutex m_queueMutex;
    std::queue<std::tuple<std::string, std::string, void*>> m_messageQueue;
};

} // namespace vcomponent::compositeinput::controller
