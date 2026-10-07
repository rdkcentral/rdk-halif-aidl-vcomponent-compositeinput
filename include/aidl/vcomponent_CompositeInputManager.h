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
 * @file vcomponent_CompositeInputManager.h
 * @brief Minimal AIDL binder service stub for the CompositeInput manager.
 *
 * Implemented AIDL interface:
 *   com.rdk.hal.compositeinput.ICompositeInputManager
 */

#include <com/rdk/hal/compositeinput/BnCompositeInputManager.h>
#include <com/rdk/hal/compositeinput/ICompositeInputManager.h>
#include <com/rdk/hal/compositeinput/ICompositeInputPort.h>
#include <com/rdk/hal/compositeinput/PlatformCapabilities.h>

#include <binder/BinderService.h>
#include <binder/Status.h>
#include <utils/StrongPointer.h>

#include "controller/vcomponent_CompositeInputUtController.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace com::rdk::hal::compositeinput
{

class CompositeInputPort;

/**
 * @brief Minimal manager exposing the HFP-declared CompositeInput ports.
 *
 * The HFP profile installed through setConfigurationPath() is the single source
 * of truth for ports, properties and platform capabilities. The manager also
 * owns the UT control-plane endpoint for the vcomponent and a worker thread that
 * drains queued KVP messages and routes them to the addressed port.
 */
class CompositeInputManager final
    : public android::BinderService<CompositeInputManager>
    , public BnCompositeInputManager
{
public:
    // PUBLIC_INTERFACE
    /**
     * @brief Return the authoritative well-known service name.
     *
     * @return The `composite_input` Binder service name.
     */
    static const char* getServiceName();

    // PUBLIC_INTERFACE
    /**
     * @brief Configure the HFP YAML path used when the manager is constructed.
     *
     * The service entrypoint calls this before publishing the Binder service.
     * The manager then invokes the CompositeInput parser and limits its own
     * responsibility to mapping parsed HFP fields into AIDL capabilities.
     *
     * @param[in] configurationPath Path to hfp-compositeinput.yaml.
     */
    static void setConfigurationPath(const std::string& configurationPath);

    // PUBLIC_INTERFACE
    /**
     * @brief Optionally override the UT control-plane port used by the manager.
     *
     * If not called, the manager uses its internal default port (currently 8086).
     *
     * @param[in] port TCP port for the UT control plane (1..65535).
     */
    static void setControlPlanePort(std::uint16_t port);

    // PUBLIC_INTERFACE
    /**
     * @brief Publish the manager on Binder and join the Binder thread pool.
     *
     * This call blocks for the lifetime of the service process.
     *
     * @return Process exit code: 0 on clean shutdown, 1 when publication failed.
     */
    static int publishAndJoinThreadPool();

    // PUBLIC_INTERFACE
    /**
     * @brief Construct the manager from the configured HFP profile.
     */
    CompositeInputManager();

    ~CompositeInputManager() override;

    CompositeInputManager(const CompositeInputManager&) = delete;
    CompositeInputManager& operator=(const CompositeInputManager&) = delete;

    // PUBLIC_INTERFACE
    /**
     * @brief Get platform-wide CompositeInput capabilities.
     *
     * @param[out] _aidl_return Receives the immutable capabilities snapshot.
     * @return Binder operation status.
     */
    android::binder::Status getPlatformCapabilities(
        PlatformCapabilities* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Get the IDs of available CompositeInput ports.
     *
     * @param[out] _aidl_return Receives the configured port IDs.
     * @return Binder operation status.
     */
    android::binder::Status getPortIds(std::vector<int32_t>* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Get a CompositeInput port interface.
     *
     * @param[in] portId Requested port identifier.
     * @param[out] _aidl_return Receives a persistent port interface.
     * @return Binder operation status.
     */
    android::binder::Status getPort(
        int32_t portId,
        android::sp<ICompositeInputPort>* _aidl_return) override;

private:
    /**
     * @brief Load the HFP profile and map it into AIDL capabilities and ports.
     *
     * @param[in] configurationPath HFP YAML path.
     * @return True when the profile was parsed and applied.
     */
    bool loadCapabilitiesFromConfig(const std::string& configurationPath);

    /**
     * @brief UT queue-consumer worker loop (KVP-driven).
     */
    void utWorkerLoop();

    /**
     * @brief Parse one queued UT control-plane message and route it to a port.
     *
     * @param[in] msg Tuple of {message key, payload, user data} from the queue.
     */
    void handleQueuedUtMessage(const std::tuple<std::string, std::string, void*>& msg);

    mutable std::mutex m_mutex;

    PlatformCapabilities m_platformCaps{};
    std::vector<android::sp<CompositeInputPort>> m_ports;

    // UT queue-consumer worker thread state.
    std::atomic<bool> m_stopUtWorker{false};
    std::thread m_utWorkerThread;

    // UT control plane owned by the manager (service lifetime).
    vcomponent::compositeinput::controller::CompositeInputUtController m_ut;
};

} // namespace com::rdk::hal::compositeinput
