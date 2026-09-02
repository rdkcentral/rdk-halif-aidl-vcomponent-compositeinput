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

#include "aidl/vcomponent_CompositeInputPort.h"

#include "aidl/vcomponent_CompositeInputController.h"
#include "aidl/vcomponent_CompositeInputControllerListener.h"
#include "aidl/vcomponent_CompositeInputEventListener.h"
#include "common/logger.h"
#include "controller/vcomponent_CompositeInputUtController.h"
#include "utility/vcomponent_CompositeInputHelper.h"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <mutex>
#include <string>

#include <binder/IInterface.h>
#include <utils/Errors.h>

#include <cstdint>

namespace com::rdk::hal::compositeinput
{

namespace
{
// Global started-port enforcement. The limit mirrors the HFP-declared
// `platformCapabilities.maximumConcurrentStartedPorts`, which the AIDL contract
// guarantees to be at least one.
std::mutex g_startedMutex;
int32_t g_startedPorts = 0;
int32_t g_maxStartedPorts = 1;

using UtController = vcomponent::compositeinput::controller::CompositeInputUtController;
using UtCommand = vcomponent::compositeinput::controller::CompositeInputUtCommand;

constexpr const char* kLogPrefix = "[VDEVICE_COMPOSITEINPUT]<CompositeInputPort>";

constexpr const char* kUtCommandKey = COMPOSITEINPUT_UTCONTROL_COMMAND_KEY;
constexpr const char* kConnectedKey = COMPOSITEINPUT_UTCONTROL_CONNECTION_KEY;
constexpr const char* kSignalStatusKey = "compositeinput.params.signalStatus";
constexpr const char* kPropertyKey = "compositeinput.params.key";
constexpr const char* kIntValueKey = "compositeinput.params.intValue";
constexpr const char* kLongValueKey = "compositeinput.params.longValue";
constexpr const char* kPixelWidthKey = "compositeinput.params.pixelWidth";
constexpr const char* kPixelHeightKey = "compositeinput.params.pixelHeight";
constexpr const char* kInterlacedKey = "compositeinput.params.interlaced";
constexpr const char* kFrameRateInHzKey = "compositeinput.params.frameRateInHz";

// Alternative video-mode parameter spellings used by the host control plane;
// each parameter is resolved from its alias list instead of a fixed key.
constexpr const char* kWidthKey = "compositeinput.params.width";
constexpr const char* kHeightKey = "compositeinput.params.height";
constexpr const char* kIsInterlacedKey = "compositeinput.params.isInterlaced";
constexpr const char* kFrameRateKey = "compositeinput.params.frameRate";
constexpr const char* kFpsKey = "compositeinput.params.fps";

/**
 * @brief Return the first key of @p candidates that is present in @p kvp.
 *
 * @return The matching key, or nullptr when none of the candidates is present.
 */
static const char* firstPresentKey(
    ut_kvp_instance_t* kvp,
    std::initializer_list<const char*> candidates)
{
    for (const char* key : candidates)
    {
        if (key != nullptr && ut_kvp_fieldPresent(kvp, key))
        {
            return key;
        }
    }

    return nullptr;
}

/**
 * @brief Derive SIGNAL_STRENGTH (dBm) from a reported signal status.
 *
 * @param[in] status Reported signal status.
 *
 * @return Signal strength in dBm.
 */
static inline int64_t signalStrengthDbmFor(SignalStatus status)
{
    switch (status)
    {
        case SignalStatus::STABLE:        return -20;
        case SignalStatus::UNSTABLE:      return -50;
        case SignalStatus::NOT_SUPPORTED: return  0;
        case SignalStatus::NO_SIGNAL:
        default:                          return  0;
    }
}

/**
 * @brief Derive SIGNAL_QUALITY (0..100) from a reported signal status.
 *
 * The bands follow PortProperty.aidl: 0 for no signal, 1..30 poor, 71..90 good.
 *
 * @param[in] status Reported signal status.
 *
 * @return Signal quality percentage in the AIDL-defined range.
 */
static inline int32_t signalQualityFor(SignalStatus status)
{
    switch (status)
    {
        case SignalStatus::STABLE:        return 85;
        case SignalStatus::UNSTABLE:      return 20;
        case SignalStatus::NOT_SUPPORTED: return 5;
        case SignalStatus::NO_SIGNAL:     return 0;
        default:                          return 0;
    }
}

static inline int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief Current wall-clock time in milliseconds since the epoch.
 *
 * METRIC_LAST_RESET_TIMESTAMP is specified as wall-clock time and must not be
 * sourced from the monotonic clock used for uptime accounting.
 *
 * @return Milliseconds since the Unix epoch.
 */
static inline int64_t nowWallClockMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static inline ::com::rdk::hal::PropertyValue makeInt32(int32_t v)
{
    ::com::rdk::hal::PropertyValue pv;
    pv.value = ::com::rdk::hal::PropertyValue::Value::make<
        ::com::rdk::hal::PropertyValue::Value::Tag::intValue>(v);
    return pv;
}

static inline ::com::rdk::hal::PropertyValue makeInt64(int64_t v)
{
    ::com::rdk::hal::PropertyValue pv;
    pv.value = ::com::rdk::hal::PropertyValue::Value::make<
        ::com::rdk::hal::PropertyValue::Value::Tag::longValue>(v);
    return pv;
}

static inline ::com::rdk::hal::PropertyValue makeNullPropertyValue()
{
    ::com::rdk::hal::PropertyValue pv;
    pv.value = std::nullopt;
    return pv;
}

static bool areSameListener(
    const android::sp<ICompositeInputEventListener>& left,
    const android::sp<ICompositeInputEventListener>& right)
{
    return left != nullptr && right != nullptr &&
        android::IInterface::asBinder(left) == android::IInterface::asBinder(right);
}

} // namespace

void CompositeInputPort::setMaxConcurrentStartedPorts(int32_t maxStartedPorts)
{
    // The AIDL contract requires at least one concurrently startable port.
    const int32_t sanitized = (maxStartedPorts >= 1) ? maxStartedPorts : 1;

    LOGF_INFO("%s setMaxConcurrentStartedPorts requested=%d applied=%d",
              kLogPrefix,
              static_cast<int>(maxStartedPorts),
              static_cast<int>(sanitized));

    std::lock_guard<std::mutex> lock(g_startedMutex);
    g_maxStartedPorts = sanitized;
}

CompositeInputPort::CompositeInputPort(int32_t portId)
    : m_portId(portId)
    , m_state(State::CLOSED)
{
    m_cachedStatus.portId = m_portId;
    m_cachedStatus.connected = false;
    m_cachedStatus.active = false;
    m_cachedStatus.signalStatus = SignalStatus::NO_SIGNAL;
    m_cachedStatus.detectedResolution = std::nullopt;

    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::SIGNAL_STRENGTH), makeInt64(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::SIGNAL_QUALITY), makeInt32(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::METRIC_SIGNAL_LOCK_TIME), makeInt64(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::METRIC_SIGNAL_DROPS), makeInt64(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::METRIC_UPTIME), makeInt64(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::METRIC_SIGNAL_LOCK_COUNT), makeInt64(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::METRIC_LAST_SIGNAL_LOCK_TIME), makeInt64(0));
    m_cachedProperties.emplace(
        static_cast<int32_t>(PortProperty::METRIC_LAST_RESET_TIMESTAMP), makeInt64(0));

    LOGF_INFO("%s constructed port=%d state=CLOSED", kLogPrefix, m_portId);
}

void CompositeInputPort::setPortInfo(const std::string& name, const std::string& description)
{
    LOGF_INFO("%s setPortInfo port=%d name=%s", kLogPrefix, m_portId, name.c_str());

    std::lock_guard<std::mutex> lock(m_mutex);
    m_name = name;
    m_description = description;
}

void CompositeInputPort::setCapabilities(const PortCapabilities& capabilities)
{
    LOGF_INFO("%s setCapabilities port=%d supportedProperties=%zu",
              kLogPrefix,
              m_portId,
              capabilities.supportedProperties.size());

    std::lock_guard<std::mutex> lock(m_mutex);
    m_capabilities = capabilities;

    // Keep property validation in sync with the advertised capability set.
    m_supportedProperties = m_capabilities.supportedProperties;
}

void CompositeInputPort::handleUTControlPlaneMessage(ut_kvp_instance_t* kvp)
{
   ////TODO in L3 
}

android::binder::Status CompositeInputPort::getId(int32_t* _aidl_return)
{
    LOGF_INFO("%s getId entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getId port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    *_aidl_return = m_portId;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getPortInfo(Port* _aidl_return)
{
    LOGF_INFO("%s getPortInfo entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getPortInfo port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    _aidl_return->name = m_name;
    _aidl_return->description = m_description;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getCapabilities(
    PortCapabilities* _aidl_return)
{
    LOGF_INFO("%s getCapabilities entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getCapabilities port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *_aidl_return = m_capabilities;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getState(State* _aidl_return)
{
    LOGF_INFO("%s getState entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getState port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *_aidl_return = m_state;

    LOGF_INFO("%s getState port=%d state=%d",
              kLogPrefix,
              m_portId,
              static_cast<int>(m_state));
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getStatus(PortStatus* _aidl_return)
{
    LOGF_INFO("%s getStatus entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getStatus port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    PortStatus status = m_cachedStatus;

    // Pre-start baseline: before STARTING/STARTED the port reports a clean
    // "not connected / no signal" status regardless of early UT updates.
    if (m_state != State::STARTING && m_state != State::STARTED)
    {
        status.connected = false;
        status.signalStatus = SignalStatus::NO_SIGNAL;
        status.detectedResolution = std::nullopt;
        status.active = false;
    }

    LOGF_INFO("%s getStatus port=%d state=%d connected=%d signalStatus=%d active=%d",
              kLogPrefix,
              m_portId,
              static_cast<int>(m_state),
              static_cast<int>(status.connected),
              static_cast<int>(status.signalStatus),
              static_cast<int>(status.active));

    *_aidl_return = status;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getProperty(
    PortProperty property,
    std::optional<::com::rdk::hal::PropertyValue>* _aidl_return)
{
    LOGF_INFO("%s getProperty entry port=%d property=%d",
              kLogPrefix,
              m_portId,
              static_cast<int>(property));

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getProperty port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    if (!isValidPortProperty(property))
    {
        LOGF_WARN("%s getProperty port=%d: invalid property=%d",
                  kLogPrefix,
                  m_portId,
                  static_cast<int>(property));
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Pre-start baseline: strength/quality remain 0/0 unless the port is
    // actively starting or started.
    if ((property == PortProperty::SIGNAL_STRENGTH ||
         property == PortProperty::SIGNAL_QUALITY) &&
        (m_state != State::STARTING && m_state != State::STARTED))
    {
        *_aidl_return = (property == PortProperty::SIGNAL_STRENGTH)
            ? makeInt64(0)
            : makeInt32(0);
        return android::binder::Status::ok();
    }

    const auto value = m_cachedProperties.find(static_cast<int32_t>(property));
    const bool supported = std::find(
        m_supportedProperties.begin(),
        m_supportedProperties.end(),
        property) != m_supportedProperties.end();
    if (supported && value != m_cachedProperties.end())
    {
        *_aidl_return = value->second;
    }
    else
    {
        LOGF_WARN("%s getProperty port=%d property=%d not available (supported=%d)",
                  kLogPrefix,
                  m_portId,
                  static_cast<int>(property),
                  static_cast<int>(supported));
        *_aidl_return = std::nullopt;
    }

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getPropertyMulti(
    const std::vector<PortProperty>& properties,
    std::vector<PropertyKVPair>* _aidl_return)
{
    LOGF_INFO("%s getPropertyMulti entry port=%d count=%zu",
              kLogPrefix,
              m_portId,
              properties.size());

    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getPropertyMulti port=%d: null _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    if (properties.empty())
    {
        LOGF_WARN("%s getPropertyMulti port=%d: empty property list", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    for (const PortProperty property : properties)
    {
        if (!isValidPortProperty(property))
        {
            LOGF_WARN("%s getPropertyMulti port=%d: invalid property=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(property));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_ARGUMENT);
        }
    }

    _aidl_return->clear();
    _aidl_return->reserve(properties.size());

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const PortProperty property : properties)
    {
        PropertyKVPair kv{};
        kv.property = property;

        // Pre-start baseline: strength/quality remain 0/0 unless the port is
        // actively starting or started.
        if ((property == PortProperty::SIGNAL_STRENGTH ||
             property == PortProperty::SIGNAL_QUALITY) &&
            (m_state != State::STARTING && m_state != State::STARTED))
        {
            kv.value = (property == PortProperty::SIGNAL_STRENGTH)
                ? makeInt64(0)
                : makeInt32(0);
            _aidl_return->push_back(std::move(kv));
            continue;
        }

        const auto value = m_cachedProperties.find(static_cast<int32_t>(property));
        kv.value = (std::find(
                        m_supportedProperties.begin(),
                        m_supportedProperties.end(),
                        property) != m_supportedProperties.end() &&
                    value != m_cachedProperties.end())
            ? value->second
            : makeNullPropertyValue();
        _aidl_return->push_back(std::move(kv));
    }

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::open(
    const android::sp<ICompositeInputControllerListener>& listener,
    android::sp<ICompositeInputController>* _aidl_return)
{
    LOGF_INFO("%s open entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr || listener == nullptr)
    {
        LOGF_ERROR("%s open port=%d: null listener or _aidl_return", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    // Resolving the client binder needs no port state, so it is done before the
    // session lock is taken. Only a remote client can die independently of this
    // process, so an in-process listener is never observed.
    const android::sp<android::IBinder> listenerBinder =
        android::IInterface::asBinder(listener);
    const bool observeListenerDeath =
        (listenerBinder != nullptr) && (listenerBinder->remoteBinder() != nullptr);

    State oldState;
    bool connected = false;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    android::sp<ControllerDeathRecipient> deathRecipient;
    std::uint64_t sessionEpoch = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::CLOSED)
        {
            LOGF_WARN("%s open port=%d rejected: state=%d is not CLOSED",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        eventListeners = setStateLocked(State::OPENING, &oldState);
        m_controllerListener = listener;
        // Capture the cached hardware state while locked; all binder callbacks
        // are emitted after the mutex has been released.
        connected = m_cachedStatus.connected;
        m_controller = new CompositeInputController(this);
        *_aidl_return = m_controller;

        // Arm a fresh session identity so a death notification queued for an
        // earlier session can never disturb this one.
        sessionEpoch = ++m_sessionEpoch;
        m_deathCleanupPending = false;
        m_controllerDeathRecipient.clear();
        if (observeListenerDeath)
        {
            deathRecipient = new ControllerDeathRecipient(
                android::wp<CompositeInputPort>(this), sessionEpoch);
            m_controllerDeathRecipient = deathRecipient;
        }
    }

    // linkToDeath() is a binder interaction and is therefore issued outside
    // m_mutex, like every other binder call in this file.
    if (deathRecipient != nullptr &&
        listenerBinder->linkToDeath(deathRecipient) != android::OK)
    {
        // The client is already gone. Handing out a controller for a session
        // that could never be cleaned up would leak the started-port slot, so
        // the half-opened session is rolled straight back to CLOSED. No state
        // event is published because the OPENING transition has not been
        // announced yet, so observers still see the port as CLOSED throughout.
        LOGF_ERROR("%s open port=%d rejected: linkToDeath failed, client is gone",
                   kLogPrefix,
                   m_portId);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_controller.clear();
            m_controllerListener.clear();
            m_controllerDeathRecipient.clear();

            // Retire the aborted session identity.
            ++m_sessionEpoch;
            m_deathCleanupPending = false;

            State rolledBackFrom;
            (void)setStateLocked(State::CLOSED, &rolledBackFrom);
            resetSessionStateLocked();
        }

        *_aidl_return = nullptr;
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    LOGF_INFO("%s open port=%d state %d -> OPENING",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::OPENING);

    // ICompositeInputPort.open() guarantees exactly one controller callback
    // during OPENING: onConnectionChanged(). onSignalStatusChanged() is
    // contractually emitted during STARTING instead.
    CompositeInputControllerListener::onConnectionChanged(listener, connected);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventListeners = setStateLocked(State::READY, &oldState);
    }
    LOGF_INFO("%s open port=%d state %d -> READY",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::READY);

    // The client may have died while this call was driving OPENING -> READY.
    // The cleanup is run only now, so observers see the complete open sequence
    // before the implicit close sequence begins.
    runPendingDeathCleanupIfNeeded();

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::close(
    const android::sp<ICompositeInputController>& controller,
    bool* _aidl_return)
{
    LOGF_INFO("%s close entry port=%d", kLogPrefix, m_portId);

    if (_aidl_return == nullptr || controller == nullptr)
    {
        LOGF_ERROR("%s close port=%d: null controller or _aidl_return",
                   kLogPrefix,
                   m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    State oldState;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::READY)
        {
            LOGF_WARN("%s close port=%d rejected: state=%d is not READY",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        if (controller != m_controller)
        {
            LOGF_WARN("%s close port=%d rejected: controller does not own the session",
                      kLogPrefix,
                      m_portId);
            *_aidl_return = false;
            return android::binder::Status::ok();
        }

        eventListeners = setStateLocked(State::CLOSING, &oldState);
    }
    LOGF_INFO("%s close port=%d state %d -> CLOSING",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::CLOSING);

    // The listener binder and its recipient are snapshotted under the lock and
    // cleared there; the unlink itself happens after the mutex is released.
    android::sp<android::IBinder> closingListenerBinder;
    android::sp<ControllerDeathRecipient> closingDeathRecipient;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_controllerListener != nullptr)
        {
            closingListenerBinder = android::IInterface::asBinder(m_controllerListener);
        }
        closingDeathRecipient = m_controllerDeathRecipient;
        m_controllerDeathRecipient.clear();

        m_controller.clear();
        m_controllerListener.clear();

        // Retire the session identity so a death notification already queued
        // for this session becomes a no-op.
        ++m_sessionEpoch;
        m_deathCleanupPending = false;

        eventListeners = setStateLocked(State::CLOSED, &oldState);

        // Closing ends the session, so session-scoped metrics restart from the
        // power-on baseline on the next open().
        resetSessionStateLocked();
    }

    // unlinkToDeath() is a binder interaction and is therefore issued outside
    // m_mutex. Its status is intentionally ignored: a client that died between
    // the state check and here leaves nothing left to unlink.
    if (closingListenerBinder != nullptr && closingDeathRecipient != nullptr)
    {
        (void)closingListenerBinder->unlinkToDeath(closingDeathRecipient);
    }

    LOGF_INFO("%s close port=%d state %d -> CLOSED",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::CLOSED);

    *_aidl_return = true;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::registerEventListener(
    const android::sp<ICompositeInputEventListener>& listener)
{
    LOGF_INFO("%s registerEventListener entry port=%d", kLogPrefix, m_portId);

    if (listener == nullptr)
    {
        LOGF_WARN("%s registerEventListener port=%d: null listener", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto existing = std::find_if(
        m_eventListeners.begin(),
        m_eventListeners.end(),
        [&listener](const android::sp<ICompositeInputEventListener>& registered) {
            return areSameListener(registered, listener);
        });
    if (existing == m_eventListeners.end())
    {
        m_eventListeners.push_back(listener);
    }

    LOGF_INFO("%s registerEventListener port=%d listeners=%zu",
              kLogPrefix,
              m_portId,
              m_eventListeners.size());
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::unregisterEventListener(
    const android::sp<ICompositeInputEventListener>& listener)
{
    LOGF_INFO("%s unregisterEventListener entry port=%d", kLogPrefix, m_portId);

    if (listener == nullptr)
    {
        LOGF_WARN("%s unregisterEventListener port=%d: null listener",
                  kLogPrefix,
                  m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto registered = std::find_if(
        m_eventListeners.begin(),
        m_eventListeners.end(),
        [&listener](const android::sp<ICompositeInputEventListener>& current) {
            return areSameListener(current, listener);
        });
    if (registered == m_eventListeners.end())
    {
        LOGF_WARN("%s unregisterEventListener port=%d: listener not registered",
                  kLogPrefix,
                  m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    m_eventListeners.erase(registered);

    LOGF_INFO("%s unregisterEventListener port=%d listeners=%zu",
              kLogPrefix,
              m_portId,
              m_eventListeners.size());
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::startFromController()
{
    LOGF_INFO("%s start entry port=%d", kLogPrefix, m_portId);

    {
        std::lock_guard<std::mutex> g(g_startedMutex);
        if (g_startedPorts >= g_maxStartedPorts)
        {
            LOGF_WARN("%s start port=%d rejected: started=%d max=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(g_startedPorts),
                      static_cast<int>(g_maxStartedPorts));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }
        g_startedPorts++;
    }

    State oldState;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::READY)
        {
            std::lock_guard<std::mutex> g(g_startedMutex);
            if (g_startedPorts > 0) g_startedPorts--;
            LOGF_WARN("%s start port=%d rejected: state=%d is not READY",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        eventListeners = setStateLocked(State::STARTING, &oldState);
    }
    LOGF_INFO("%s start port=%d state %d -> STARTING",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::STARTING);

    // The AIDL contract requires onSignalStatusChanged() to fire at least once
    // during the STARTING transition, before STARTED is reached.
    android::sp<ICompositeInputControllerListener> controllerListener;
    SignalStatus signalStatus = SignalStatus::NO_SIGNAL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        controllerListener = m_controllerListener;
        signalStatus = m_cachedStatus.signalStatus;
    }
    LOGF_INFO("%s start port=%d publishing signalStatus=%d during STARTING",
              kLogPrefix,
              m_portId,
              static_cast<int>(signalStatus));
    CompositeInputControllerListener::onSignalStatusChanged(
        controllerListener, signalStatus);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventListeners = setStateLocked(State::STARTED, &oldState);
    }
    LOGF_INFO("%s start port=%d state %d -> STARTED",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::STARTED);

    // Establish the uptime baseline and capture the telemetry snapshot while
    // locked; the callbacks below are dispatched after releasing the mutex.
    ::com::rdk::hal::PropertyValue signalStrength = makeInt64(0);
    ::com::rdk::hal::PropertyValue signalQuality = makeInt32(0);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_startTsMs = nowMs();
        eventListeners = m_eventListeners;

        const auto strength =
            m_cachedProperties.find(static_cast<int32_t>(PortProperty::SIGNAL_STRENGTH));
        if (strength != m_cachedProperties.end())
        {
            signalStrength = strength->second;
        }

        const auto quality =
            m_cachedProperties.find(static_cast<int32_t>(PortProperty::SIGNAL_QUALITY));
        if (quality != m_cachedProperties.end())
        {
            signalQuality = quality->second;
        }
    }

    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::SIGNAL_STRENGTH, signalStrength);
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::SIGNAL_QUALITY, signalQuality);

    // A death notification that arrived while this call was driving
    // READY -> STARTING -> STARTED was deferred; run it now that the port has
    // reached a stable state and the started-port slot is accounted for.
    runPendingDeathCleanupIfNeeded();

    LOGF_INFO("%s start exit port=%d", kLogPrefix, m_portId);
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::stopFromController()
{
    LOGF_INFO("%s stop entry port=%d", kLogPrefix, m_portId);

    State oldState;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::STARTED)
        {
            LOGF_WARN("%s stop port=%d rejected: state=%d is not STARTED",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        eventListeners = setStateLocked(State::STOPPING, &oldState);
    }
    LOGF_INFO("%s stop port=%d state %d -> STOPPING",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::STOPPING);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventListeners = setStateLocked(State::READY, &oldState);
    }
    LOGF_INFO("%s stop port=%d state %d -> READY",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::READY);

    {
        std::lock_guard<std::mutex> g(g_startedMutex);
        if (g_startedPorts > 0)
        {
            g_startedPorts--;
        }
    }

    // A death notification that arrived mid-stop was deferred; the graceful stop
    // has already released the started-port slot, so the cleanup below only has
    // to complete the implicit close.
    runPendingDeathCleanupIfNeeded();

    LOGF_INFO("%s stop exit port=%d", kLogPrefix, m_portId);
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::resetMetricsFromController()
{
    LOGF_INFO("%s resetMetrics entry port=%d", kLogPrefix, m_portId);

    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    const int64_t resetTimestampMs = nowMs();

    // METRIC_LAST_RESET_TIMESTAMP is exposed as wall-clock time, while internal
    // uptime accounting stays on the monotonic clock.
    const int64_t resetWallClockMs = nowWallClockMs();
    const ::com::rdk::hal::PropertyValue zeroValue = makeInt64(0);
    const ::com::rdk::hal::PropertyValue resetTimestampValue =
        makeInt64(resetWallClockMs);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::STARTED)
        {
            LOGF_WARN("%s resetMetrics port=%d rejected: state=%d is not STARTED",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        m_signalDrops = 0;
        m_signalLockCount = 0;
        m_signalLockTimeMs = 0;
        m_lastSignalLockTimeMs = 0;
        m_startTsMs = resetTimestampMs;
        m_lastResetTsMs = resetTimestampMs;

        m_cachedProperties[static_cast<int32_t>(PortProperty::METRIC_SIGNAL_DROPS)] =
            zeroValue;
        m_cachedProperties[static_cast<int32_t>(PortProperty::METRIC_SIGNAL_LOCK_COUNT)] =
            zeroValue;
        m_cachedProperties[static_cast<int32_t>(PortProperty::METRIC_SIGNAL_LOCK_TIME)] =
            zeroValue;
        m_cachedProperties[static_cast<int32_t>(
            PortProperty::METRIC_LAST_SIGNAL_LOCK_TIME)] = zeroValue;
        m_cachedProperties[static_cast<int32_t>(PortProperty::METRIC_UPTIME)] =
            zeroValue;
        m_cachedProperties[static_cast<int32_t>(
            PortProperty::METRIC_LAST_RESET_TIMESTAMP)] = resetTimestampValue;
        eventListeners = m_eventListeners;
    }

    LOGF_INFO("%s resetMetrics port=%d lastResetTimestampMs=%lld",
              kLogPrefix,
              m_portId,
              static_cast<long long>(resetWallClockMs));

    // Binder callbacks can re-enter the component, so dispatch all reset
    // notifications only after releasing the port mutex.
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::METRIC_SIGNAL_DROPS, zeroValue);
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::METRIC_SIGNAL_LOCK_COUNT, zeroValue);
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::METRIC_SIGNAL_LOCK_TIME, zeroValue);
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::METRIC_LAST_SIGNAL_LOCK_TIME, zeroValue);
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::METRIC_UPTIME, zeroValue);
    CompositeInputEventListener::onPropertyChanged(
        eventListeners,
        PortProperty::METRIC_LAST_RESET_TIMESTAMP,
        resetTimestampValue);

    LOGF_INFO("%s resetMetrics exit port=%d", kLogPrefix, m_portId);
    return android::binder::Status::ok();
}

bool CompositeInputPort::isValidPortProperty(PortProperty property)
{
    switch (property)
    {
        case PortProperty::SIGNAL_STRENGTH:
        case PortProperty::SIGNAL_QUALITY:
        case PortProperty::METRIC_SIGNAL_LOCK_TIME:
        case PortProperty::METRIC_SIGNAL_DROPS:
        case PortProperty::METRIC_UPTIME:
        case PortProperty::METRIC_SIGNAL_LOCK_COUNT:
        case PortProperty::METRIC_LAST_SIGNAL_LOCK_TIME:
        case PortProperty::METRIC_LAST_RESET_TIMESTAMP:
            return true;
    }

    return false;
}

bool CompositeInputPort::supportsProperty(PortProperty property) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return supportsPropertyLocked(property);
}

bool CompositeInputPort::supportsPropertyLocked(PortProperty property) const
{
    return std::find(
               m_supportedProperties.begin(),
               m_supportedProperties.end(),
               property) != m_supportedProperties.end();
}

bool CompositeInputPort::isPropertySupported(PortProperty property) const
{
    return supportsProperty(property);
}

android::binder::Status CompositeInputPort::setPropertyFromController(
    PortProperty property,
    const ::com::rdk::hal::PropertyValue& value)
{
    LOGF_INFO("%s setProperty entry port=%d property=%d",
              kLogPrefix,
              m_portId,
              static_cast<int>(property));

    if (!isValidPortProperty(property))
    {
        LOGF_WARN("%s setProperty port=%d: invalid property=%d",
                  kLogPrefix,
                  m_portId,
                  static_cast<int>(property));
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!supportsPropertyLocked(property))
        {
            LOGF_WARN("%s setProperty port=%d: unsupported property=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(property));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_ARGUMENT);
        }

        if (!isPropertyWritableLocked(property))
        {
            LOGF_WARN("%s setProperty port=%d: read-only property=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(property));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_UNSUPPORTED_OPERATION);
        }

        if (!isPropertyValueTypeValidLocked(property, value))
        {
            LOGF_WARN("%s setProperty port=%d: value type mismatch for property=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(property));
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_ARGUMENT);
        }

        m_cachedProperties[static_cast<int32_t>(property)] = value;
        eventListeners = m_eventListeners;
    }

    CompositeInputEventListener::onPropertyChanged(eventListeners, property, value);

    LOGF_INFO("%s setProperty exit port=%d property=%d",
              kLogPrefix,
              m_portId,
              static_cast<int>(property));
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::setPropertyMultiFromController(
    const std::vector<PropertyKVPair>& properties)
{
    LOGF_INFO("%s setPropertyMulti entry port=%d count=%zu",
              kLogPrefix,
              m_portId,
              properties.size());

    if (properties.empty())
    {
        LOGF_WARN("%s setPropertyMulti port=%d: empty property list", kLogPrefix, m_portId);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Validate the complete batch before changing the cache so this
        // operation remains all-or-nothing.
        for (const auto& property : properties)
        {
            if (!isValidPortProperty(property.property) ||
                !supportsPropertyLocked(property.property))
            {
                LOGF_WARN("%s setPropertyMulti port=%d: invalid or unsupported property=%d",
                          kLogPrefix,
                          m_portId,
                          static_cast<int>(property.property));
                return android::binder::Status::fromExceptionCode(
                    android::binder::Status::EX_ILLEGAL_ARGUMENT);
            }

            if (!isPropertyWritableLocked(property.property))
            {
                LOGF_WARN("%s setPropertyMulti port=%d: read-only property=%d",
                          kLogPrefix,
                          m_portId,
                          static_cast<int>(property.property));
                return android::binder::Status::fromExceptionCode(
                    android::binder::Status::EX_UNSUPPORTED_OPERATION);
            }

            if (!isPropertyValueTypeValidLocked(property.property, property.value))
            {
                LOGF_WARN("%s setPropertyMulti port=%d: value type mismatch for property=%d",
                          kLogPrefix,
                          m_portId,
                          static_cast<int>(property.property));
                return android::binder::Status::fromExceptionCode(
                    android::binder::Status::EX_ILLEGAL_ARGUMENT);
            }
        }

        for (const auto& property : properties)
        {
            m_cachedProperties[static_cast<int32_t>(property.property)] = property.value;
        }
        eventListeners = m_eventListeners;
    }

    for (const auto& property : properties)
    {
        CompositeInputEventListener::onPropertyChanged(
            eventListeners, property.property, property.value);
    }

    LOGF_INFO("%s setPropertyMulti exit port=%d count=%zu",
              kLogPrefix,
              m_portId,
              properties.size());
    return android::binder::Status::ok();
}

bool CompositeInputPort::isPropertyWritableLocked(PortProperty property) const
{
    if (!m_capabilities.propertyMetadata.has_value())
    {
        return false;
    }

    for (const auto& metadata : *m_capabilities.propertyMetadata)
    {
        if (metadata.has_value() && metadata->key == property)
        {
            return !metadata->readOnly;
        }
    }

    return false;
}

bool CompositeInputPort::isPropertyValueTypeValidLocked(
    PortProperty property,
    const ::com::rdk::hal::PropertyValue& value) const
{
    if (!m_capabilities.propertyMetadata.has_value() || !value.value.has_value())
    {
        return false;
    }

    using PropertyType = PropertyMetadata::PropertyType;
    using ValueTag = ::com::rdk::hal::PropertyValue::Value::Tag;

    for (const auto& metadata : *m_capabilities.propertyMetadata)
    {
        if (!metadata.has_value() || metadata->key != property)
        {
            continue;
        }

        switch (metadata->type)
        {
            case PropertyType::BOOLEAN:
                return value.value->getTag() == ValueTag::booleanValue;
            case PropertyType::INTEGER:
                return value.value->getTag() == ValueTag::intValue;
            case PropertyType::LONG:
                return value.value->getTag() == ValueTag::longValue;
            case PropertyType::FLOAT:
                return value.value->getTag() == ValueTag::floatValue;
            case PropertyType::DOUBLE:
                return value.value->getTag() == ValueTag::doubleValue;
            case PropertyType::STRING:
                return value.value->getTag() == ValueTag::stringValue;
        }
    }

    return false;
}

void CompositeInputPort::resetSessionStateLocked()
{
    LOGF_INFO("%s resetSessionState port=%d", kLogPrefix, m_portId);

    // The cached hardware snapshot is deliberately preserved: it describes the
    // physical input owned by the UT control plane, which a session end does
    // not change. Only the session-scoped metric accumulators restart.
    m_startTsMs = 0;
    m_lastResetTsMs = 0;
    m_signalLockTimeMs = 0;
    m_lastSignalLockTimeMs = 0;
    m_signalDrops = 0;
    m_signalLockCount = 0;

    using ValueTag = ::com::rdk::hal::PropertyValue::Value::Tag;
    static constexpr PortProperty kSessionScopedMetrics[] = {
        PortProperty::METRIC_SIGNAL_LOCK_TIME,
        PortProperty::METRIC_SIGNAL_DROPS,
        PortProperty::METRIC_UPTIME,
        PortProperty::METRIC_SIGNAL_LOCK_COUNT,
        PortProperty::METRIC_LAST_SIGNAL_LOCK_TIME,
        PortProperty::METRIC_LAST_RESET_TIMESTAMP,
    };

    for (const PortProperty metric : kSessionScopedMetrics)
    {
        const auto entry = m_cachedProperties.find(static_cast<int32_t>(metric));
        if (entry == m_cachedProperties.end())
        {
            continue;
        }

        auto& value = entry->second;
        if (!value.value.has_value())
        {
            continue;
        }

        // Zero the payload while preserving the AIDL-declared value type.
        switch (value.value->getTag())
        {
            case ValueTag::intValue:
                value = makeInt32(0);
                break;
            case ValueTag::longValue:
                value = makeInt64(0);
                break;
            default:
                break;
        }
    }

    // No onPropertyChanged() is emitted here: the port is CLOSED and a burst of
    // events on a closed port would pollute an observer's event stream.
}

void CompositeInputPort::resetHardwareBaselineForBoot()
{
    LOGF_INFO("%s resetHardwareBaselineForBoot port=%d", kLogPrefix, m_portId);

    std::lock_guard<std::mutex> lock(m_mutex);

    // Power-on baseline: nothing is plugged in and no signal has been reported.
    // `active` remains derived from the lifecycle state.
    m_cachedStatus.connected = false;
    m_cachedStatus.signalStatus = SignalStatus::NO_SIGNAL;
    m_cachedStatus.detectedResolution = std::nullopt;

    if (supportsPropertyLocked(PortProperty::SIGNAL_STRENGTH))
    {
        m_cachedProperties[static_cast<int32_t>(PortProperty::SIGNAL_STRENGTH)] =
            makeInt64(0);
    }

    if (supportsPropertyLocked(PortProperty::SIGNAL_QUALITY))
    {
        m_cachedProperties[static_cast<int32_t>(PortProperty::SIGNAL_QUALITY)] =
            makeInt32(0);
    }
}

std::vector<android::sp<ICompositeInputEventListener>>
CompositeInputPort::setStateLocked(State nextState, State* oldState)
{
    if (oldState != nullptr)
    {
        *oldState = m_state;
    }

    LOGF_INFO("%s state transition port=%d %d -> %d",
              kLogPrefix,
              m_portId,
              static_cast<int>(m_state),
              static_cast<int>(nextState));

    m_state = nextState;
    m_cachedStatus.active = (nextState == State::STARTED);
    return m_eventListeners;
}

void CompositeInputPort::ControllerDeathRecipient::binderDied(
    const android::wp<android::IBinder>& /*who*/)
{
    // The recipient holds only a weak reference, so a port that has already been
    // destroyed cannot be resurrected here.
    const android::sp<CompositeInputPort> port = m_port.promote();
    if (port == nullptr)
    {
        LOGF_WARN("%s binderDied ignored: port already destroyed", kLogPrefix);
        return;
    }

    port->cleanupAfterControllerDeath(m_sessionEpoch);
}

void CompositeInputPort::runPendingDeathCleanupIfNeeded()
{
    std::uint64_t sessionEpoch = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_deathCleanupPending)
        {
            return;
        }

        m_deathCleanupPending = false;
        sessionEpoch = m_sessionEpoch;
    }

    LOGF_WARN("%s running deferred death cleanup port=%d epoch=%llu",
              kLogPrefix,
              m_portId,
              static_cast<unsigned long long>(sessionEpoch));
    cleanupAfterControllerDeath(sessionEpoch);
}

void CompositeInputPort::cleanupAfterControllerDeath(std::uint64_t sessionEpoch)
{
    LOGF_WARN("%s controller client died port=%d epoch=%llu: performing implicit "
              "stop() and close()",
              kLogPrefix,
              m_portId,
              static_cast<unsigned long long>(sessionEpoch));

    State oldState = State::CLOSED;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    bool stopObserved = false;

    // Implicit stop() stage. The transition is decided under the mutex so that
    // exactly one path can observe STARTED -> STOPPING.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_sessionEpoch != sessionEpoch)
        {
            LOGF_INFO("%s death cleanup port=%d ignored: stale epoch=%llu current=%llu",
                      kLogPrefix,
                      m_portId,
                      static_cast<unsigned long long>(sessionEpoch),
                      static_cast<unsigned long long>(m_sessionEpoch));
            return;
        }

        if (m_state == State::CLOSED)
        {
            LOGF_INFO("%s death cleanup port=%d ignored: session already closed",
                      kLogPrefix,
                      m_portId);
            return;
        }

        if (m_state == State::OPENING || m_state == State::STARTING ||
            m_state == State::STOPPING || m_state == State::CLOSING)
        {
            // A lifecycle transition is in flight on another thread. Closing the
            // port from underneath it would produce an event sequence that no
            // client-driven path can produce, so the cleanup is handed to the
            // transition that is already running.
            m_deathCleanupPending = true;
            LOGF_INFO("%s death cleanup port=%d deferred: transient state=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return;
        }

        if (m_state == State::STARTED)
        {
            eventListeners = setStateLocked(State::STOPPING, &oldState);
            stopObserved = true;
        }
    }

    if (stopObserved)
    {
        LOGF_INFO("%s death cleanup port=%d state %d -> STOPPING",
                  kLogPrefix,
                  m_portId,
                  static_cast<int>(oldState));
        CompositeInputEventListener::onStateChanged(
            eventListeners, oldState, State::STOPPING);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            eventListeners = setStateLocked(State::READY, &oldState);
        }
        LOGF_INFO("%s death cleanup port=%d state %d -> READY",
                  kLogPrefix,
                  m_portId,
                  static_cast<int>(oldState));
        CompositeInputEventListener::onStateChanged(
            eventListeners, oldState, State::READY);

        // Mirror the accounting of stopFromController(). The release is gated on
        // this path having observed the STARTED -> STOPPING transition itself,
        // which makes it exactly-once when a graceful stop() and this cleanup
        // race each other: the loser never sees STARTED and skips this stage.
        {
            std::lock_guard<std::mutex> g(g_startedMutex);
            if (g_startedPorts > 0)
            {
                g_startedPorts--;
            }
        }
    }

    // Implicit close() stage.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_sessionEpoch != sessionEpoch || m_state == State::CLOSED)
        {
            return;
        }

        if (m_state != State::READY)
        {
            // A concurrent transition took the port out of READY; let it finish
            // and complete the cleanup at its tail.
            m_deathCleanupPending = true;
            LOGF_INFO("%s death cleanup port=%d close stage deferred: state=%d",
                      kLogPrefix,
                      m_portId,
                      static_cast<int>(m_state));
            return;
        }

        eventListeners = setStateLocked(State::CLOSING, &oldState);
    }
    LOGF_INFO("%s death cleanup port=%d state %d -> CLOSING",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::CLOSING);

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // No unlinkToDeath() is issued here: the observed binder node is already
        // dead, so dropping the recipient reference is sufficient.
        m_controllerDeathRecipient.clear();
        m_controller.clear();
        m_controllerListener.clear();

        // Retire the session identity so any further notification for it, and
        // any flag set while this cleanup was running, becomes a no-op.
        ++m_sessionEpoch;
        m_deathCleanupPending = false;

        eventListeners = setStateLocked(State::CLOSED, &oldState);

        // The session ended, so session-scoped state restarts from the power-on
        // baseline on the next open(), exactly as a graceful close() does.
        resetSessionStateLocked();
    }
    LOGF_INFO("%s death cleanup port=%d state %d -> CLOSED",
              kLogPrefix,
              m_portId,
              static_cast<int>(oldState));
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::CLOSED);

    LOGF_WARN("%s death cleanup port=%d complete: port is reusable", kLogPrefix, m_portId);
}

} // namespace com::rdk::hal::compositeinput
