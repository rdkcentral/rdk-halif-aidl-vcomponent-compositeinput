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

/**
 * @file vcomponent_CompositeInputManager.cpp
 * @brief ICompositeInputManager implementation backed by the CompositeInput HFP.
 */

#include "aidl/vcomponent_CompositeInputManager.h"

#include "aidl/vcomponent_CompositeInputPort.h"
#include "common/logger.h"
#include "service/vcomponent_CompositeInputService.h"
#include "utility/vcomponent_CompositeInputHelper.h"
#include "utility/vcomponent_CompositeInputParseConfig.h"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>

#include <ut_kvp_profile.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

namespace com::rdk::hal::compositeinput
{

namespace
{
constexpr const char* kLogPrefix = "[VDEVICE_COMPOSITEINPUT]<CompositeInputManager>";

// UT control-plane port for CompositeInput orchestration (default).
constexpr std::uint16_t kDefaultUtControlPlanePort = 8086;

// Configurable UT control-plane port (overridable by service main via
// setControlPlanePort()).
std::mutex g_utControlPlanePortMutex;
std::uint16_t g_utControlPlanePort = kDefaultUtControlPlanePort;

// Guarded HFP configuration path installed by the service entrypoint.
std::mutex g_configurationPathMutex;
std::string g_configurationPath =
    vcomponent::compositeinput::service::kDefaultHfpPath;

/**
 * @brief Get the currently configured HFP path.
 */
std::string getConfiguredPath()
{
    std::lock_guard<std::mutex> lock(g_configurationPathMutex);
    return g_configurationPath;
}

/**
 * @brief Get the currently configured UT control-plane port.
 */
std::uint16_t getConfiguredUtControlPlanePort()
{
    std::lock_guard<std::mutex> lock(g_utControlPlanePortMutex);
    return g_utControlPlanePort;
}

/**
 * @brief Map one parsed HFP port entry to the AIDL PortCapabilities parcelable.
 *
 * @param[in] portConfig Parsed HFP port entry.
 * @return Capabilities to feed into the corresponding CompositeInputPort.
 */
PortCapabilities mapPortCapabilities(
    const vcomponent::compositeinput::utility::CompositeInputPortConfig& portConfig)
{
    namespace hfp = vcomponent::compositeinput::utility;

    PortCapabilities capabilities{};
    capabilities.supportedProperties = hfp::toPortProperties(portConfig.supportedProperties);

    // `PortCapabilities.propertyMetadata` is an AIDL `@nullable PropertyMetadata[]`;
    // the helper yields std::nullopt when the profile declares no metadata.
    capabilities.propertyMetadata = hfp::toNullablePropertyMetadata(portConfig.propertyMetadata);

    return capabilities;
}
} // namespace

const char* CompositeInputManager::getServiceName()
{
    // serviceName() may return a temporary std::string, so cache it in static
    // storage to keep the returned C-string valid.
    static const std::string kServiceName = ICompositeInputManager::serviceName();
    return kServiceName.c_str();
}

void CompositeInputManager::setConfigurationPath(const std::string& configurationPath)
{
    if (configurationPath.empty())
    {
        LOGF_WARN("%s Empty CompositeInput configuration path ignored; keeping path=%s",
                  kLogPrefix,
                  getConfiguredPath().c_str());
        return;
    }

    std::lock_guard<std::mutex> lock(g_configurationPathMutex);
    g_configurationPath = configurationPath;
}

void CompositeInputManager::setControlPlanePort(std::uint16_t port)
{
    if (port == 0)
    {
        LOGF_WARN("%s Invalid UT control-plane port=0; keeping default=%u",
                  kLogPrefix,
                  static_cast<unsigned>(kDefaultUtControlPlanePort));
        return;
    }

    std::lock_guard<std::mutex> lock(g_utControlPlanePortMutex);
    g_utControlPlanePort = port;
}

CompositeInputManager::CompositeInputManager()
{
    // Bring up the UT control plane before configuration work so early
    // orchestration messages are queued rather than dropped.
    const auto port = getConfiguredUtControlPlanePort();
    if (!m_ut.init(port, nullptr))
    {
        LOGF_WARN("%s UT control plane init failed (see UT logs). port=%u",
                  kLogPrefix,
                  static_cast<unsigned>(port));
    }

    const std::string configurationPath = getConfiguredPath();
    if (loadCapabilitiesFromConfig(configurationPath))
    {
        LOGF_INFO("%s Initialized CompositeInput manager from HFP configuration "
                  "path=%s ports=%llu.",
                  kLogPrefix,
                  configurationPath.c_str(),
                  static_cast<unsigned long long>(m_ports.size()));
    }
    else
    {
        LOGF_WARN("%s CompositeInput manager has no capabilities or ports; the HFP "
                  "configuration was not applied and no fallback profile exists. path=%s",
                  kLogPrefix,
                  configurationPath.c_str());
    }

    m_stopUtWorker.store(false);
    m_utWorkerThread = std::thread(&CompositeInputManager::utWorkerLoop, this);
}

CompositeInputManager::~CompositeInputManager()
{
    m_stopUtWorker.store(true);
    if (m_utWorkerThread.joinable())
    {
        m_utWorkerThread.join();
    }
    m_ut.shutdown();
}

bool CompositeInputManager::loadCapabilitiesFromConfig(const std::string& configurationPath)
{
    if (configurationPath.empty())
    {
        LOGF_WARN("%s CompositeInput HFP configuration path is empty", kLogPrefix);
        return false;
    }

    vcomponent::compositeinput::utility::CompositeInputHfpConfig parsedConfig{};
    std::string parseError;

    if (!vcomponent::compositeinput::utility::loadCompositeInputHfpConfigFromYaml(
            configurationPath,
            &parsedConfig,
            &parseError))
    {
        LOGF_WARN("%s CompositeInput HFP parse failed path=%s error=%s",
                  kLogPrefix,
                  configurationPath.c_str(),
                  parseError.c_str());
        return false;
    }

    namespace hfp = vcomponent::compositeinput::utility;

    // Build the port objects first, skipping entries the parser could not fully
    // validate, so a partially broken profile still yields a usable service.
    std::vector<android::sp<CompositeInputPort>> ports;
    std::vector<int32_t> seenIds;
    ports.reserve(parsedConfig.ports.size());
    seenIds.reserve(parsedConfig.ports.size());

    for (const auto& portConfig : parsedConfig.ports)
    {
        if (portConfig.id < 0)
        {
            LOGF_WARN("%s Skipping HFP port with invalid id=%d",
                      kLogPrefix,
                      static_cast<int>(portConfig.id));
            continue;
        }

        if (std::find(seenIds.begin(), seenIds.end(), portConfig.id) != seenIds.end())
        {
            LOGF_WARN("%s Skipping HFP port with duplicate id=%d",
                      kLogPrefix,
                      static_cast<int>(portConfig.id));
            continue;
        }

        seenIds.push_back(portConfig.id);

        android::sp<CompositeInputPort> port = new CompositeInputPort(portConfig.id);
        port->setPortInfo(portConfig.name, portConfig.description);
        port->setCapabilities(mapPortCapabilities(portConfig));

        // Ports come up at the power-on hardware baseline; this is the only
        // place the snapshot is cleared, so UT-driven state survives later
        // controller close/open cycles.
        port->resetHardwareBaselineForBoot();

        ports.push_back(std::move(port));
    }

    if (ports.empty())
    {
        LOGF_WARN("%s CompositeInput HFP yielded no usable port path=%s",
                  kLogPrefix,
                  configurationPath.c_str());
        return false;
    }

    PlatformCapabilities platformCaps{};
    platformCaps.halVersion = parsedConfig.halVersion;

    // AIDL declares maxPorts as a byte; clamp defensively so an oversized or
    // missing HFP value can never wrap into a negative port count.
    int32_t maxPorts = parsedConfig.maxPorts;
    if (maxPorts < static_cast<int32_t>(ports.size()))
    {
        maxPorts = static_cast<int32_t>(ports.size());
    }
    if (maxPorts > std::numeric_limits<int8_t>::max())
    {
        maxPorts = std::numeric_limits<int8_t>::max();
    }
    platformCaps.maxPorts = static_cast<int8_t>(maxPorts);

    // The AIDL contract requires at least one concurrently startable port.
    platformCaps.maximumConcurrentStartedPorts =
        (parsedConfig.maximumConcurrentStartedPorts >= 1)
            ? parsedConfig.maximumConcurrentStartedPorts
            : 1;

    platformCaps.supportedProperties = hfp::toPortProperties(parsedConfig.supportedProperties);

    // `PlatformCapabilities.propertyMetadata` is an AIDL `@nullable PropertyMetadata[]`;
    // the helper yields std::nullopt when no entry maps, keeping the field null.
    platformCaps.propertyMetadata = hfp::toNullablePropertyMetadata(parsedConfig.propertyMetadata);

    platformCaps.features.macrovisionDetectionSupported =
        parsedConfig.features.macrovisionDetectionSupported;

    // Propagate the HFP-declared concurrency limit to the port layer so
    // start() enforcement follows the profile instead of a hardcoded value.
    CompositeInputPort::setMaxConcurrentStartedPorts(
        platformCaps.maximumConcurrentStartedPorts);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_platformCaps = platformCaps;
    m_ports = std::move(ports);

    return true;
}

void CompositeInputManager::utWorkerLoop()
{
    while (!m_stopUtWorker.load())
    {
        auto msg = m_ut.getMessage();
        if (!msg)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        handleQueuedUtMessage(*msg);
    }
}

void CompositeInputManager::handleQueuedUtMessage(
    const std::tuple<std::string, std::string, void*>& msg)
{
    const auto& payload = std::get<1>(msg);
    if (payload.empty())
    {
        return;
    }

    ut_kvp_instance_t* kvp = ut_kvp_createInstance();
    if (kvp == nullptr)
    {
        LOGF_ERROR("%s ut_kvp_createInstance failed", kLogPrefix);
        return;
    }

    // The UT parser mutates its input buffer, so it must not receive the
    // immutable string stored in the message queue.
    std::vector<char> mutablePayload(payload.begin(), payload.end());
    mutablePayload.push_back('\0');

    // The trailing NUL must NOT be included in the length: the YAML parser
    // validates every byte in range and rejects an embedded NUL.
    const auto status = ut_kvp_openMemory(
        kvp,
        mutablePayload.data(),
        static_cast<uint32_t>(payload.size()));
    if (status != UT_KVP_STATUS_SUCCESS)
    {
        LOGF_ERROR("%s ut_kvp_openMemory failed status=%d payload_size=%zu",
                   kLogPrefix,
                   status,
                   payload.size());
        ut_kvp_destroyInstance(kvp);
        return;
    }

    char receivedCommand[128] = {0};
    ut_kvp_getStringField(
        kvp, "compositeinput.command", receivedCommand, sizeof(receivedCommand));
    LOGF_INFO("%s UT_RX cmd='%s' has_cmd=%d has_connected=%d has_signalStatus=%d",
              kLogPrefix,
              receivedCommand,
              static_cast<int>(ut_kvp_fieldPresent(kvp, "compositeinput.command")),
              static_cast<int>(ut_kvp_fieldPresent(kvp, "compositeinput.params.connected")),
              static_cast<int>(ut_kvp_fieldPresent(kvp, "compositeinput.params.signalStatus")));

    int32_t portId = 0;
    if (ut_kvp_fieldPresent(kvp, "compositeinput.params.port"))
    {
        portId = static_cast<int32_t>(
            ut_kvp_getUInt32Field(kvp, "compositeinput.params.port"));
    }

    android::sp<CompositeInputPort> targetPort;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = std::find_if(
            m_ports.begin(),
            m_ports.end(),
            [portId](const auto& port) {
                return port != nullptr && port->id() == portId;
            });
        if (it != m_ports.end())
        {
            targetPort = *it;
        }
    }

    if (targetPort != nullptr)
    {
        targetPort->handleUTControlPlaneMessage(kvp);
    }
    else
    {
        LOGF_WARN("%s UT message targeted unknown port id=%d",
                  kLogPrefix,
                  static_cast<int>(portId));
    }

    ut_kvp_destroyInstance(kvp);
}

int CompositeInputManager::publishAndJoinThreadPool()
{
    const char* serviceName = getServiceName();
    android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    android::sp<CompositeInputManager> service = new CompositeInputManager();

    LOGF_INFO("%s Publishing CompositeInput Binder service (serviceName=%s)",
              kLogPrefix,
              serviceName);

    const android::status_t status =
        serviceManager->addService(android::String16(serviceName), service);
    if (status != android::OK)
    {
        LOGF_ERROR("%s addService(%s) failed: %d",
                   kLogPrefix,
                   serviceName,
                   static_cast<int>(status));
        return 1;
    }

    android::ProcessState::self()->startThreadPool();
    android::IPCThreadState::self()->joinThreadPool();
    return 0;
}

android::binder::Status CompositeInputManager::getPlatformCapabilities(
    PlatformCapabilities* _aidl_return)
{
    LOGF_INFO("%s getPlatformCapabilities entry", kLogPrefix);

    if (_aidl_return == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *_aidl_return = m_platformCaps;
    return android::binder::Status::ok();
}

android::binder::Status CompositeInputManager::getPortIds(std::vector<int32_t>* _aidl_return)
{
    LOGF_INFO("%s getPortIds entry", kLogPrefix);
    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getPortIds: null _aidl_return", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    _aidl_return->clear();
    _aidl_return->reserve(m_ports.size());

    for (const auto& port : m_ports)
    {
        if (port != nullptr)
        {
            _aidl_return->push_back(port->id());
        }
    }

    return android::binder::Status::ok();
}

android::binder::Status CompositeInputManager::getPort(
    int32_t portId,
    android::sp<ICompositeInputPort>* _aidl_return)
{
    LOGF_INFO("%s getPort entry id=%d", kLogPrefix, static_cast<int>(portId));
    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s getPort: null _aidl_return", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *_aidl_return = nullptr;

    if (portId < 0)
    {
        LOGF_WARN("%s getPort: invalid id=%d", kLogPrefix, static_cast<int>(portId));
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    const auto it = std::find_if(
        m_ports.begin(),
        m_ports.end(),
        [portId](const auto& port) { return port != nullptr && port->id() == portId; });

    if (it != m_ports.end())
    {
        *_aidl_return = *it;
        return android::binder::Status::ok();
    }

    LOGF_WARN("%s getPort: not found id=%d", kLogPrefix, static_cast<int>(portId));
    return android::binder::Status::fromExceptionCode(
        android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

} // namespace com::rdk::hal::compositeinput
