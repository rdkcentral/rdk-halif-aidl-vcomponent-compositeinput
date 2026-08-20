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
 * @brief SKELETON ICompositeInputManager implementation backed by the CompositeInput HFP.
 *
 * Every method declared in vcomponent_CompositeInputManager.h is defined here.
 * The Binder plumbing (service name, publication, guarded accessors) is real so
 * the service starts and answers calls deterministically; the HFP mapping and UT
 * command routing are left as documented placeholders.
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

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

namespace com::rdk::hal::compositeinput
{

namespace
{
constexpr const char* componentName = "CompositeInputManager";

// UT control-plane port for CompositeInput orchestration (default).
constexpr std::uint16_t kDefaultUtControlPlanePort = 8080;

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
} // namespace

const char* CompositeInputManager::getServiceName()
{
    // NOTE:
    // Depending on the AIDL-generated headers, ICompositeInputManager::serviceName()
    // may return an std::string by value. Calling c_str() on that temporary would
    // yield a dangling pointer. Cache the value in static storage to ensure a
    // stable C-string.
    static const std::string kServiceName = ICompositeInputManager::serviceName();
    return kServiceName.c_str();
}

void CompositeInputManager::setConfigurationPath(const std::string& configurationPath)
{
    if (configurationPath.empty())
    {
        LOGF_WARN("%s: Empty CompositeInput configuration path ignored; keeping path=%s",
                  componentName,
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
        LOGF_WARN("%s: Invalid UT control-plane port=0; keeping default=%u",
                  componentName,
                  static_cast<unsigned>(kDefaultUtControlPlanePort));
        return;
    }

    std::lock_guard<std::mutex> lock(g_utControlPlanePortMutex);
    g_utControlPlanePort = port;
}

CompositeInputManager::CompositeInputManager()
{
    // Bring up the UT control plane before any configuration work so early
    // orchestration messages are queued rather than dropped.
    const auto port = getConfiguredUtControlPlanePort();
    if (!m_ut.init(port, nullptr))
    {
        LOGF_WARN("%s: UT control plane init failed (see UT logs). port=%u",
                  componentName,
                  static_cast<unsigned>(port));
    }

    const std::string configurationPath = getConfiguredPath();
    if (loadCapabilitiesFromConfig(configurationPath))
    {
        LOGF_INFO("%s: Initialized CompositeInput manager from HFP configuration "
                  "path=%s ports=%llu.",
                  componentName,
                  configurationPath.c_str(),
                  static_cast<unsigned long long>(m_ports.size()));
    }
    else
    {
        LOGF_WARN("%s: Initialized CompositeInput manager with default capabilities; "
                  "HFP configuration was not applied. path=%s",
                  componentName,
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
        LOGF_WARN("%s: CompositeInput HFP configuration path is empty", componentName);
        return false;
    }

    vcomponent::compositeinput::utility::CompositeInputHfpConfig parsedConfig{};
    std::string parseError;

    if (!vcomponent::compositeinput::utility::loadCompositeInputHfpConfigFromYaml(
            configurationPath,
            &parsedConfig,
            &parseError))
    {
        LOGF_WARN("%s: CompositeInput HFP parse failed path=%s error=%s",
                  componentName,
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
            LOGF_WARN("%s: Skipping HFP port with invalid id=%d",
                      componentName,
                      static_cast<int>(portConfig.id));
            continue;
        }

        if (std::find(seenIds.begin(), seenIds.end(), portConfig.id) != seenIds.end())
        {
            LOGF_WARN("%s: Skipping HFP port with duplicate id=%d",
                      componentName,
                      static_cast<int>(portConfig.id));
            continue;
        }

        seenIds.push_back(portConfig.id);
        ports.push_back(new CompositeInputPort(portConfig));
    }

    if (ports.empty())
    {
        LOGF_WARN("%s: CompositeInput HFP yielded no usable port path=%s",
                  componentName,
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

    // TODO(impl): parse `payload` into a KVP instance
    // (ut_kvp_createInstance + ut_kvp_openMemory over a mutable copy), read
    // `compositeinput.command`, resolve the target port from
    // `compositeinput.params.port` (defaulting to 0), forward the instance via
    // CompositeInputPort::handleUTControlPlaneMessage(), then destroy the
    // instance. Not required by the L1 suite; lands with the real
    // implementation (see TEVDevice/impl/README.md).
}

int CompositeInputManager::publishAndJoinThreadPool()
{
    const char* serviceName = getServiceName();
    android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    android::sp<CompositeInputManager> service = new CompositeInputManager();

    LOGF_INFO("%s: Publishing CompositeInput Binder service (serviceName=%s)",
              componentName,
              serviceName);

    const android::status_t status =
        serviceManager->addService(android::String16(serviceName), service);
    if (status != android::OK)
    {
        LOGF_ERROR("%s: addService(%s) failed: %d",
                   componentName,
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
    LOGF_INFO("[VDEVICE_COMPOSITEINPUT][%s::%s] entry", componentName, "getPlatformCapabilities");

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
    LOGF_INFO("[VDEVICE_COMPOSITEINPUT][%s::%s] entry", componentName, "getPortIds");
    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s: getPortIds: null _aidl_return", componentName);
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
    LOGF_INFO("[VDEVICE_COMPOSITEINPUT][%s::%s] entry id=%d",
              componentName,
              "getPort",
              static_cast<int>(portId));
    if (_aidl_return == nullptr)
    {
        LOGF_ERROR("%s: getPort: null _aidl_return", componentName);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_NULL_POINTER);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *_aidl_return = nullptr;

    if (portId < 0)
    {
        LOGF_WARN("%s: getPort: invalid id=%d", componentName, static_cast<int>(portId));
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

    LOGF_WARN("%s: getPort: not found id=%d", componentName, static_cast<int>(portId));
    return android::binder::Status::fromExceptionCode(
        android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

} // namespace com::rdk::hal::compositeinput
