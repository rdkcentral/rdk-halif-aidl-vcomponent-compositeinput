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
 *
 * Following the HDMI Output vcomponent pattern, this class owns no HFP parsing
 * logic. The manager parses hfp-compositeinput.yaml, maps the parsed tokens to
 * AIDL values through CompositeInputHelper, and feeds the result into each port
 * with setPortInfo()/setCapabilities(). Nothing here is hardcoded.
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

#include <binder/IBinder.h>
#include <binder/Status.h>
#include <utils/RefBase.h>
#include <utils/StrongPointer.h>

#include <ut_kvp_profile.h> // for ut_kvp_instance_t

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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
     * @brief Install the platform-wide limit of concurrently STARTED ports.
     *
     * The value originates from the HFP profile
     * (`platformCapabilities.maximumConcurrentStartedPorts`) and is applied by
     * the manager once the profile has been parsed, so no port-side limit is
     * hardcoded beyond the AIDL minimum of one.
     *
     * @param[in] maxStartedPorts Maximum concurrently started ports (values < 1 are clamped to 1).
     */
    static void setMaxConcurrentStartedPorts(int32_t maxStartedPorts);

    // PUBLIC_INTERFACE
    /**
     * @brief Create a CompositeInput port for an HFP-declared port identifier.
     *
     * Port metadata and capabilities are supplied afterwards by the manager
     * through setPortInfo() and setCapabilities().
     *
     * @param[in] portId HFP-declared port identifier.
     */
    explicit CompositeInputPort(int32_t portId);

    // PUBLIC_INTERFACE
    /**
     * @brief Install the HFP-declared immutable port metadata.
     *
     * @param[in] name        HFP `ports[].name`.
     * @param[in] description HFP `ports[].description`.
     */
    void setPortInfo(const std::string& name, const std::string& description);

    // PUBLIC_INTERFACE
    /**
     * @brief Install the HFP-mapped per-port AIDL capabilities.
     *
     * The supported-property list used for property validation is derived from
     * the same capabilities object, so discovery and access checks can never
     * diverge.
     *
     * @param[in] capabilities Capabilities mapped from the HFP profile.
     */
    void setCapabilities(const PortCapabilities& capabilities);

    // PUBLIC_INTERFACE
    /**
     * @brief Establish the power-on hardware baseline for this port.
     *
     * Invoked once by the manager while the ports are being created, after the
     * port metadata and capabilities have been installed. It puts the cached
     * hardware snapshot into the disconnected / no-signal state and zeroes the
     * signal-derived telemetry, so an L1/L2 run always starts from a clean
     * baseline. It is deliberately NOT called on close(), which keeps
     * UT-driven hardware state observable across an L3 close/open cycle.
     */
    void resetHardwareBaselineForBoot();

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
     *
     * @param[in] property Property key to test.
     * @return True when the HFP profile declared the property for this port.
     */
    bool isPropertySupported(PortProperty property) const;

    // PUBLIC_INTERFACE
    /**
     * @brief Apply one controller-originated writable property update.
     *
     * The property must be declared in the port capability metadata and have
     * `readOnly == false`. The cache is updated while holding the port mutex;
     * event notifications are emitted only after releasing it.
     *
     * @param[in] property Property key to update.
     * @param[in] value New property value.
     * @return Binder status for the requested update.
     */
    android::binder::Status setPropertyFromController(
        PortProperty property,
        const ::com::rdk::hal::PropertyValue& value);

    // PUBLIC_INTERFACE
    /**
     * @brief Atomically apply controller-originated writable property updates.
     *
     * Every update is validated before the cache changes. If any update is
     * invalid, unsupported, or read-only, the cache remains unchanged.
     * Notifications are emitted after releasing the port mutex.
     *
     * @param[in] properties Property updates to apply.
     * @return Binder status for the requested batch update.
     */
    android::binder::Status setPropertyMultiFromController(
        const std::vector<PropertyKVPair>& properties);

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
    /**
     * @brief Observes the death of the client that owns a controller session.
     *
     * The recipient is armed in open() against the client-supplied
     * ICompositeInputControllerListener binder, which is the only client-owned
     * binder the port holds for the lifetime of a session. It keeps a weak
     * reference to the port, so the port and the recipient can never form a
     * reference cycle, and it captures the session epoch it was armed for, so a
     * notification queued for an already retired session is discarded instead
     * of disturbing a newer one.
     */
    class ControllerDeathRecipient final : public android::IBinder::DeathRecipient
    {
    public:
        /**
         * @brief Arm a recipient for one controller session.
         *
         * @param[in] port         Weak reference to the owning port.
         * @param[in] sessionEpoch Session identity this recipient was armed for.
         */
        ControllerDeathRecipient(
            const android::wp<CompositeInputPort>& port,
            std::uint64_t sessionEpoch)
            : m_port(port)
            , m_sessionEpoch(sessionEpoch)
        {
        }

        /**
         * @brief Binder death notification entry point.
         *
         * @param[in] who The binder that died (unused; the session identity is
         *                carried by this recipient).
         */
        void binderDied(const android::wp<android::IBinder>& who) override;

    private:
        const android::wp<CompositeInputPort> m_port;
        const std::uint64_t m_sessionEpoch;
    };

    /**
     * @brief Perform the implicit stop()/close() required when a client dies.
     *
     * Mirrors the existing transition sequences instead of calling the public
     * stop()/close() entry points, which are gated on a controller identity that
     * a dead client can no longer present. Runs on a binder death-notification
     * thread and therefore observes the same discipline as every other path in
     * this component: state is mutated under m_mutex, and every notification is
     * dispatched after the mutex has been released. No callback is ever issued
     * to the controller listener, whose remote end is by definition gone.
     *
     * @param[in] sessionEpoch Session identity the cleanup was requested for.
     */
    void cleanupAfterControllerDeath(std::uint64_t sessionEpoch);

    /**
     * @brief Run a deferred binder-death cleanup, if one was requested.
     *
     * A death notification that arrives while a lifecycle transition is in
     * flight is recorded rather than executed, so the running transition can
     * complete and publish a well-formed event sequence first. This helper is
     * invoked at the tail of those transitions, with m_mutex released.
     */
    void runPendingDeathCleanupIfNeeded();

    static bool isValidPortProperty(PortProperty property);
    bool supportsProperty(PortProperty property) const;
    bool supportsPropertyLocked(PortProperty property) const;
    bool isPropertyWritableLocked(PortProperty property) const;
    bool isPropertyValueTypeValidLocked(
        PortProperty property,
        const ::com::rdk::hal::PropertyValue& value) const;

    /**
     * @brief Retire the session-scoped state of the port.
     *
     * Called when the port reaches CLOSED. Clears the internal metric counters
     * and zeroes the cached METRIC_* property values, so a later open() can
     * never observe accumulators produced by the previous controller session.
     *
     * The cached status snapshot (connected, signalStatus, detectedResolution)
     * and the signal-derived telemetry (SIGNAL_STRENGTH, SIGNAL_QUALITY) are
     * preserved: they describe the physical hardware condition owned by the
     * control plane, which a controller session ending does not change.
     * `active` is maintained by setStateLocked() from the lifecycle state.
     *
     * Use resetHardwareBaselineForBoot() to clear that hardware snapshot; it
     * is applied only once, at port creation.
     *
     * The caller must already hold m_mutex.
     */
    void resetSessionStateLocked();

    std::vector<android::sp<ICompositeInputEventListener>> setStateLocked(
        State nextState,
        State* oldState);

    const int32_t m_portId;
    mutable std::mutex m_mutex;

    // HFP-fed identity and capabilities (installed by the manager after parsing).
    std::string m_name;
    std::string m_description;
    PortCapabilities m_capabilities{};
    std::vector<PortProperty> m_supportedProperties;

    State m_state;
    PortStatus m_cachedStatus{};
    std::unordered_map<int32_t, ::com::rdk::hal::PropertyValue> m_cachedProperties;
    android::sp<ICompositeInputController> m_controller;
    android::sp<ICompositeInputControllerListener> m_controllerListener;
    std::vector<android::sp<ICompositeInputEventListener>> m_eventListeners;

    // Binder-death cleanup state for the current controller session. The epoch
    // identifies the session a death recipient was armed for; it is advanced on
    // every open(), close() and death-driven close, so a notification for a
    // retired session is a no-op.
    android::sp<ControllerDeathRecipient> m_controllerDeathRecipient;
    std::uint64_t m_sessionEpoch{0};
    bool m_deathCleanupPending{false};

    // Skeleton metric storage (deterministic, no hardware).
    int64_t m_startTsMs{0};
    int64_t m_lastResetTsMs{0};
    int64_t m_signalLockTimeMs{0};
    int64_t m_lastSignalLockTimeMs{0};
    int64_t m_signalDrops{0};
    int64_t m_signalLockCount{0};
};

} // namespace com::rdk::hal::compositeinput
