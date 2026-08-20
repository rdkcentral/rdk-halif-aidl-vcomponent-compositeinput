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
#include "utility/vcomponent_CompositeInputHelper.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace com::rdk::hal::compositeinput
{

namespace
{
namespace hfp = vcomponent::compositeinput::utility;

// Global started-port enforcement (platformCapabilities.maximumConcurrentStartedPorts == 1).
std::mutex g_startedMutex;
int g_startedPorts = 0;
constexpr int kMaxStartedPorts = 1;

static inline int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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

/**
 * @brief Build the AIDL PortCapabilities parcelable from HFP port data.
 */
PortCapabilities buildCapabilities(const hfp::CompositeInputPortConfig& config)
{
    PortCapabilities capabilities{};
    capabilities.supportedProperties = hfp::toPortProperties(config.supportedProperties);
    capabilities.propertyMetadata = hfp::toNullablePropertyMetadata(config.propertyMetadata);
    return capabilities;
}

// Fallback metadata synthesis (used if HFP mapping yields null/empty).
static inline std::vector<std::optional<PropertyMetadata>> synthesizeMetadata(
    const std::vector<PortProperty>& props)
{
    std::vector<std::optional<PropertyMetadata>> meta;
    meta.reserve(props.size());

    for (auto p : props)
    {
        PropertyMetadata m{};
        m.key = p;
        m.readOnly = true;
        m.isMetric = (static_cast<int32_t>(p) >= 1000);

        // Match your built-in profile types:
        // SIGNAL_QUALITY is INTEGER, everything else in your profile is LONG.
        m.type = (p == PortProperty::SIGNAL_QUALITY)
                     ? PropertyMetadata::PropertyType::INTEGER
                     : PropertyMetadata::PropertyType::LONG;

        m.description = "skeleton";
        meta.push_back(m);
    }
    return meta;
}

} // namespace

CompositeInputPort::CompositeInputPort(
    const vcomponent::compositeinput::utility::CompositeInputPortConfig& config)
    : m_portId(config.id)
    , m_name(config.name)
    , m_description(config.description)
    , m_capabilities(buildCapabilities(config))
    , m_supportedProperties(m_capabilities.supportedProperties)
    , m_state(State::CLOSED)
{
}

void CompositeInputPort::handleUTControlPlaneMessage(ut_kvp_instance_t* kvp)
{
    if (kvp == nullptr)
    {
        return;
    }

    char command[128] = {0};
    ut_kvp_getStringField(kvp, "compositeinput.command", command, sizeof(command));
    (void)command;
}

android::binder::Status CompositeInputPort::getId(int32_t* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    *_aidl_return = m_portId;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getPortInfo(Port* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    _aidl_return->name = m_name;
    _aidl_return->description = m_description;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getCapabilities(
    PortCapabilities* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    *_aidl_return = m_capabilities;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getState(State* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *_aidl_return = m_state;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getStatus(PortStatus* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    PortStatus status{};
    status.portId = m_portId;
    status.connected = false;
    status.active = (m_state == State::STARTED);
    status.signalStatus = SignalStatus::NO_SIGNAL;
    status.detectedResolution = std::nullopt;

    *_aidl_return = status;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::getProperty(
    PortProperty property,
    std::optional<::com::rdk::hal::PropertyValue>* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    if (!supportsProperty(property))
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    using PP = PortProperty;
    switch (property)
    {
    case PP::SIGNAL_STRENGTH:
        *_aidl_return = makeInt64(0);
        return android::binder::Status::ok();

    case PP::SIGNAL_QUALITY:
        *_aidl_return = makeInt32(0);
        return android::binder::Status::ok();

    case PP::METRIC_SIGNAL_LOCK_TIME:
        *_aidl_return = makeInt64(m_signalLockTimeMs);
        return android::binder::Status::ok();

    case PP::METRIC_SIGNAL_DROPS:
        *_aidl_return = makeInt64(m_signalDrops);
        return android::binder::Status::ok();

    case PP::METRIC_UPTIME: {
        int64_t up = (m_startTsMs > 0) ? (nowMs() - m_startTsMs) : 0;
        *_aidl_return = makeInt64(up);
        return android::binder::Status::ok();
    }

    case PP::METRIC_SIGNAL_LOCK_COUNT:
        *_aidl_return = makeInt64(m_signalLockCount);
        return android::binder::Status::ok();

    case PP::METRIC_LAST_SIGNAL_LOCK_TIME:
        *_aidl_return = makeInt64(m_lastSignalLockTimeMs);
        return android::binder::Status::ok();

    case PP::METRIC_LAST_RESET_TIMESTAMP:
        *_aidl_return = makeInt64(m_lastResetTsMs);
        return android::binder::Status::ok();

    default:
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }
}

android::binder::Status CompositeInputPort::getPropertyMulti(
    const std::vector<PortProperty>& properties,
    std::vector<PropertyKVPair>* _aidl_return)
{
    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    if (properties.empty())
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    for (const PortProperty property : properties)
    {
        if (!supportsProperty(property))
        {
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_ARGUMENT);
        }
    }

    _aidl_return->clear();
    _aidl_return->reserve(properties.size());

    for (auto p : properties)
    {
        std::optional<::com::rdk::hal::PropertyValue> v;
        auto st = getProperty(p, &v);
        if (!st.isOk())
        {
            return st;
        }

        PropertyKVPair kv{};
        kv.property = p;
        kv.value = v.value();
        _aidl_return->push_back(std::move(kv));
    }

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::open(
    const android::sp<ICompositeInputControllerListener>& listener,
    android::sp<ICompositeInputController>* _aidl_return)
{
    if (_aidl_return == nullptr || listener == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    State oldState;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::CLOSED)
        {
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        eventListeners = setStateLocked(State::OPENING, &oldState);
        m_controllerListener = listener;
        m_controller = new CompositeInputController(this);
        *_aidl_return = m_controller;
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::OPENING);

    CompositeInputControllerListener::onConnectionChanged(listener, false);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventListeners = setStateLocked(State::READY, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::READY);

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::close(
    const android::sp<ICompositeInputController>& controller,
    bool* _aidl_return)
{
    if (_aidl_return == nullptr || controller == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    State oldState;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::READY)
        {
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        if (controller != m_controller)
        {
            *_aidl_return = false;
            return android::binder::Status::ok();
        }

        eventListeners = setStateLocked(State::CLOSING, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::CLOSING);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_controller.clear();
        m_controllerListener.clear();
        eventListeners = setStateLocked(State::CLOSED, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::CLOSED);

    *_aidl_return = true;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::registerEventListener(
    const android::sp<ICompositeInputEventListener>& listener)
{
    
    std::lock_guard<std::mutex> lock(m_mutex);

    if (listener != nullptr)
    {
        auto it = std::find(m_eventListeners.begin(), m_eventListeners.end(), listener);
        if (it == m_eventListeners.end())
        {
            m_eventListeners.push_back(listener);
        }
    }

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::unregisterEventListener(
    const android::sp<ICompositeInputEventListener>& listener)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (listener == nullptr)
    {
        return android::binder::Status::ok();
    }

    auto it = std::find(m_eventListeners.begin(), m_eventListeners.end(), listener);
    if (it == m_eventListeners.end())
    {
        // VTS _neg expects this
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    m_eventListeners.erase(it);
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::startFromController()
{
    {
        std::lock_guard<std::mutex> g(g_startedMutex);
        if (g_startedPorts >= kMaxStartedPorts)
        {
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
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        eventListeners = setStateLocked(State::STARTING, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::STARTING);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventListeners = setStateLocked(State::STARTED, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::STARTED);

    // Start uptime clock and notify "signal" properties (skeleton: no signal).
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_startTsMs = nowMs();
        eventListeners = m_eventListeners;
    }

    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::SIGNAL_STRENGTH, makeInt64(0));
    CompositeInputEventListener::onPropertyChanged(
        eventListeners, PortProperty::SIGNAL_QUALITY, makeInt32(0));

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::stopFromController()
{
    State oldState;
    std::vector<android::sp<ICompositeInputEventListener>> eventListeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::STARTED)
        {
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_STATE);
        }

        eventListeners = setStateLocked(State::STOPPING, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::STOPPING);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventListeners = setStateLocked(State::READY, &oldState);
    }
    CompositeInputEventListener::onStateChanged(
        eventListeners, oldState, State::READY);

    {
        std::lock_guard<std::mutex> g(g_startedMutex);
        if (g_startedPorts > 0)
        {
            g_startedPorts--;
        }
    }

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputPort::resetMetricsFromController()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_signalDrops = 0;
    m_signalLockCount = 0;
    m_signalLockTimeMs = 0;
    m_lastSignalLockTimeMs = 0;

    m_lastResetTsMs = nowMs();

    return android::binder::Status::ok();
}

bool CompositeInputPort::supportsProperty(PortProperty property) const
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

std::vector<android::sp<ICompositeInputEventListener>>
CompositeInputPort::setStateLocked(State nextState, State* oldState)
{
    if (oldState != nullptr)
    {
        *oldState = m_state;
    }

    m_state = nextState;
    return m_eventListeners;
}

} // namespace com::rdk::hal::compositeinput