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
 *
 * This header owns the complete UT control-plane vocabulary for the component,
 * mirroring the composite reference vcomponent:
 *  - the fully-qualified KVP message keys,
 *  - the recognised command set, and
 *  - the ut-core `ut_control_keyStringMapping_t` table used to decode a received
 *    command token into that command set.
 *
 * Keeping the vocabulary in one place means the manager (queue consumer) and the
 * port (command executor) can never drift apart, and a new command only has to
 * be declared once.
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

/*
 * UT control-plane message keys.
 *
 * The keys are fully-qualified KVP paths rooted at the `compositeinput` profile
 * key registered with the control plane, exactly as used by the composite
 * reference vcomponent and the host command YAMLs.
 */
#define COMPOSITEINPUT_UTCONTROL_PROFILE_KEY          "compositeinput"
#define COMPOSITEINPUT_UTCONTROL_COMMAND_KEY          "compositeinput.command"
#define COMPOSITEINPUT_UTCONTROL_PORT_KEY             "compositeinput.params.port"
#define COMPOSITEINPUT_UTCONTROL_CONNECTION_KEY       "compositeinput.params.connected"
#define COMPOSITEINPUT_UTCONTROL_SIGNAL_STATUS_KEY    "compositeinput.params.signalStatus"
#define COMPOSITEINPUT_UTCONTROL_VIDEO_WIDTH_KEY      "compositeinput.params.pixelWidth"
#define COMPOSITEINPUT_UTCONTROL_VIDEO_HEIGHT_KEY     "compositeinput.params.pixelHeight"
#define COMPOSITEINPUT_UTCONTROL_VIDEO_INTERLACED_KEY "compositeinput.params.interlaced"
#define COMPOSITEINPUT_UTCONTROL_VIDEO_FRAMERATE_KEY  "compositeinput.params.frameRateInHz"
#define COMPOSITEINPUT_UTCONTROL_PROPERTY_KEY         "compositeinput.params.key"
#define COMPOSITEINPUT_UTCONTROL_INT_VALUE_KEY        "compositeinput.params.intValue"
#define COMPOSITEINPUT_UTCONTROL_LONG_VALUE_KEY       "compositeinput.params.longValue"

namespace vcomponent::compositeinput::controller
{

/**
 * @brief Commands accepted over the CompositeInput UT control plane.
 *
 * The values are stable so they can be carried through the ut-core
 * `ut_control_keyStringMapping_t` table, which trades in `int32_t`.
 */
enum class CompositeInputUtCommand : std::int32_t
{
    CONNECTION_STATUS = 0, /**< Set the cable connection state on a port. */
    SIGNAL_STATUS     = 1, /**< Set the signal status on a port. */
    VIDEO_MODE        = 2, /**< Report a detected video mode on a started port. */
    SET_PROPERTY      = 3, /**< Seed a port property value. */
    CLEAR_VIDEO_MODE  = 4, /**< Drop the cached detected video mode. */
    UNKNOWN           = -1 /**< Unrecognised command token. */
};

/**
 * @brief ut-core token to CompositeInputUtCommand mapping table.
 *
 * Both the reference snake_case spellings (`connection_status`,
 * `signal_status`, `video_mode`) and the camelCase spellings (`setConnection`,
 * `setSignalStatus`, `setVideoMode`) are declared so a host naming difference
 * cannot silently drop a command. Terminated by a `{NULL, -1}` sentinel as
 * required by ut-control.
 */
extern const ut_control_keyStringMapping_t compositeInputUtControllerMapTable[];

/**
 * @brief One queued UT control-plane message: {message key, payload, user data}.
 */
using CompositeInputUtMessage = std::tuple<std::string, std::string, void*>;

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
     * @brief Decode a received UT command token into its command value.
     *
     * The decode goes through the ut-core map table (composite reference
     * parity), so every accepted spelling of a command resolves to the same
     * value and an unrecognised token yields CompositeInputUtCommand::UNKNOWN.
     *
     * @param[in] token Command token exactly as received from the control plane.
     *
     * @return The decoded command, or CompositeInputUtCommand::UNKNOWN.
     */
    static CompositeInputUtCommand decodeCommand(const std::string& token);

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
    std::optional<CompositeInputUtMessage> getMessage();

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
    std::queue<CompositeInputUtMessage> m_messageQueue;
};

} // namespace vcomponent::compositeinput::controller
