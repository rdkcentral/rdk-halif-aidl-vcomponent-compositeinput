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
 * @file vcomponent_CompositeInputPort.h
 * @brief Per-port Binder implementation for the CompositeInput AIDL contract.
 */

#include <com/rdk/hal/compositeinput/BnCompositeInputPort.h>
#include <com/rdk/hal/compositeinput/ICompositeInputController.h>
#include <com/rdk/hal/compositeinput/ICompositeInputControllerListener.h>
#include <com/rdk/hal/compositeinput/ICompositeInputEventListener.h>
#include <com/rdk/hal/compositeinput/Port.h>
#include <com/rdk/hal/compositeinput/PortCapabilities.h>
#include <com/rdk/hal/compositeinput/PortProperty.h>
#include <com/rdk/hal/compositeinput/PortStatus.h>
#include <com/rdk/hal/compositeinput/PropertyKVPair.h>
#include <com/rdk/hal/compositeinput/SignalStatus.h>
#include <com/rdk/hal/compositeinput/State.h>
#include <com/rdk/hal/PropertyValue.h>

#include "utility/vcomponent_CompositeInputHfpConfigUtils.h"

#include <binder/Status.h>
#include <utils/StrongPointer.h>

#include <ut_kvp_profile.h> // for ut_kvp_instance_t

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace com::rdk::hal::compositeinput
{

class CompositeInputController;

/**
 * @brief Implements one HFP-declared CompositeInput port.
 */
class CompositeInputPort final : public BnCompositeInputPort
{
public:
    // PUBLIC_INTERFACE
    /**
     * @brief Create a CompositeInput port with immutable HFP metadata.
     *
     * @param config HFP port configuration declared by hfp-compositeinput.yaml.
     */
    explicit CompositeInputPort(
        const vcomponent::compositeinput::utility::CompositeInputPortConfig& config);

    // PUBLIC_INTERFACE
    /**
     * @brief Get the configured AIDL port identifier for this instance.
     *
     * @return The HFP-declared port ID.
     */
    int32_t id() const
    {
        return m_portId;
    }
    
    // PUBLIC_INTERFACE
    /**
     * @brief Check whether a property is supported by this port.
     *
     * Used by the controller implementation to decide whether to return
     * EX_ILLEGAL_ARGUMENT vs EX_UNSUPPORTED_OPERATION.
     */
    bool isPropertySupported(PortProperty property) const;

    // PUBLIC_INTERFACE
    /**
     * @brief Apply a parsed UT control-plane KVP message to this port.
     *
     * The manager owns the UT control plane, parses each queued payload into a
     * KVP instance and dispatches it to the addressed port. The instance is only
     * valid for the duration of this call.
     *
     * @param kvp Parsed KVP instance for one UT control-plane message.
     */
    void handleUTControlPlaneMessage(ut_kvp_instance_t* kvp);

    // PUBLIC_INTERFACE
    /**
     * @brief Return this port's configured ID.
     *
     * @param[out] _aidl_return Receives the port ID.
     * @return Binder operation status.
     */
    android::binder::Status getId(int32_t* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Return immutable port metadata.
     *
     * @param[out] _aidl_return Receives the port metadata.
     * @return Binder operation status.
     */
    android::binder::Status getPortInfo(Port* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Return immutable per-port capabilities.
     *
     * @param[out] _aidl_return Receives per-port capabilities.
     * @return Binder operation status.
     */
    android::binder::Status getCapabilities(PortCapabilities* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Return the current CompositeInput lifecycle state.
     *
     * @param[out] _aidl_return Receives the current state.
     * @return Binder operation status.
     */
    android::binder::Status getState(State* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Return the latest status snapshot.
     *
     * @param[out] _aidl_return Receives the current port status.
     * @return Binder operation status.
     */
    android::binder::Status getStatus(PortStatus* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Get a supported port property.
     *
     * @param property Requested property key.
     * @param[out] _aidl_return Receives the property when available.
     * @return Binder operation status.
     */
    android::binder::Status getProperty(
        PortProperty property,
        std::optional<::com::rdk::hal::PropertyValue>* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Get multiple supported port properties.
     *
     * @param properties Requested property keys.
     * @param[out] _aidl_return Receives property/value pairs.
     * @return Binder operation status.
     */
    android::binder::Status getPropertyMulti(
        const std::vector<PortProperty>& properties,
        std::vector<PropertyKVPair>* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Open the port and acquire its exclusive controller.
     *
     * @param listener Controller-owner event listener.
     * @param[out] _aidl_return Receives the exclusive controller.
     * @return Binder operation status.
     */
    android::binder::Status open(
        const android::sp<ICompositeInputControllerListener>& listener,
        android::sp<ICompositeInputController>* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Close a READY port using its owning controller.
     *
     * @param controller The controller returned by open().
     * @param[out] _aidl_return Receives whether the supplied controller owned the port.
     * @return Binder operation status.
     */
    android::binder::Status close(
        const android::sp<ICompositeInputController>& controller,
        bool* _aidl_return) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Register an observer for lifecycle and property events.
     *
     * @param listener Event listener to register.
     * @return Binder operation status.
     */
    android::binder::Status registerEventListener(
        const android::sp<ICompositeInputEventListener>& listener) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Unregister a previously registered observer.
     *
     * @param listener Event listener to unregister.
     * @return Binder operation status.
     */
    android::binder::Status unregisterEventListener(
        const android::sp<ICompositeInputEventListener>& listener) override;

    /**
     * @brief Transition this port from READY to STARTED for its controller.
     *
     * @return Binder operation status.
     */
    android::binder::Status startFromController();

    /**
     * @brief Transition this port from STARTED to READY for its controller.
     *
     * @return Binder operation status.
     */
    android::binder::Status stopFromController();

    /**
     * @brief Reset metric counters/timestamps for this port (controller API).
     *
     * @return Binder operation status.
     */
    android::binder::Status resetMetricsFromController();

private:
    bool supportsProperty(PortProperty property) const;
    std::vector<android::sp<ICompositeInputEventListener>> setStateLocked(
        State nextState,
        State* oldState);

    const int32_t m_portId;
    const std::string m_name;
    const std::string m_description;
    const PortCapabilities m_capabilities;
    const std::vector<PortProperty> m_supportedProperties;
    mutable std::mutex m_mutex;
    State m_state;
    android::sp<ICompositeInputController> m_controller;
    android::sp<ICompositeInputControllerListener> m_controllerListener;
    std::vector<android::sp<ICompositeInputEventListener>> m_eventListeners;

    // Skeleton metric storage (deterministic, no hardware).
    int64_t m_startTsMs{0};
    int64_t m_lastResetTsMs{0};
    int64_t m_signalLockTimeMs{0};
    int64_t m_lastSignalLockTimeMs{0};
    int64_t m_signalDrops{0};
    int64_t m_signalLockCount{0};
};

} // namespace com::rdk::hal::compositeinput