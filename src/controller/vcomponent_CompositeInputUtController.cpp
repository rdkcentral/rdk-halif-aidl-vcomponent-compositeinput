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
 * @file vcomponent_CompositeInputUtController.cpp
 * @brief SKELETON UT-controller facade for the CompositeInput vcomponent.
 *
 * Every method declared in vcomponent_CompositeInputUtController.h is defined
 * here. The FIFO queue plumbing is real so the manager worker loop compiles and
 * runs deterministically; the UT control-plane wiring, HFP load and inventory
 * generation are left as documented placeholders.
 */

#include "controller/vcomponent_CompositeInputUtController.h"

namespace vcomponent::compositeinput::controller
{

namespace
{
// UT control-plane profile key owned by this vcomponent.
static char* kControlPlaneComponentKey = "compositeinput";
} // namespace

CompositeInputUtController::CompositeInputUtController()
    : m_controlPlaneInstance(nullptr)
    , m_userData(nullptr)
{
}

CompositeInputUtController::~CompositeInputUtController()
{
    shutdown();
}

bool CompositeInputUtController::loadConfiguration(
    const std::string& hfpYamlPath,
    std::string* outError)
{
    // TODO(skeleton): call
    // utility::loadCompositeInputHfpConfigFromYaml(hfpYamlPath, &cfg, outError),
    // then cache `hfpYamlPath` in m_hfpYamlPath and the parsed profile in
    // m_hfpConfig on success. Reset both members on failure.
    (void)hfpYamlPath;

    m_hfpYamlPath.clear();
    m_hfpConfig = utility::CompositeInputHfpConfig{};

    if (outError != nullptr)
    {
        outError->clear();
    }

    return false;
}

bool CompositeInputUtController::buildInventory(
    std::string* outInventory,
    std::string* outError) const
{
    if (outInventory == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "outInventory is null";
        }
        return false;
    }

    // TODO(skeleton): emit one `key=value` line per observable HFP field, e.g.
    // hfpYamlPath, interfaceVersion, halVersion, portCount, maxPorts,
    // maximumConcurrentStartedPorts, then one block per port
    // (port.<id>.{name,description,supportedProperties,propertyMetadata}).
    outInventory->clear();
    return false;
}

bool CompositeInputUtController::init(std::uint16_t port, void* userData)
{
    if (m_controlPlaneInstance != nullptr)
    {
        UT_LOG_INFO("CompositeInput UT control plane already initialized");
        return true;
    }

    // TODO(skeleton): create the endpoint with UT_ControlPlane_Init(port),
    // register &CompositeInputUtController::messageCallback for
    // kControlPlaneComponentKey via UT_ControlPlane_RegisterCallbackOnMessage(),
    // then start it with UT_ControlPlane_Start(). Roll back with
    // UT_ControlPlane_Exit() when registration fails.
    (void)port;
    (void)kControlPlaneComponentKey;

    m_userData = userData;
    return false;
}

void CompositeInputUtController::shutdown()
{
    // TODO(skeleton): release the endpoint with UT_ControlPlane_Exit() once
    // init() creates one.
    m_controlPlaneInstance = nullptr;
    m_userData = nullptr;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::queue<std::tuple<std::string, std::string, void*>> emptyQueue;
    m_messageQueue.swap(emptyQueue);
}

std::optional<std::tuple<std::string, std::string, void*>>
CompositeInputUtController::getMessage()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_messageQueue.empty())
    {
        return std::nullopt;
    }

    auto item = m_messageQueue.front();
    m_messageQueue.pop();
    return item;
}

bool CompositeInputUtController::isRunning() const
{
    return m_controlPlaneInstance != nullptr;
}

void CompositeInputUtController::messageCallback(
    char* key,
    ut_kvp_instance_t* instance,
    void* userData)
{
    auto* self = static_cast<CompositeInputUtController*>(userData);
    if (self != nullptr)
    {
        self->pushMessage(key, instance);
    }
}

void CompositeInputUtController::pushMessage(char* key, ut_kvp_instance_t* instance)
{
    const std::string messageKey = (key != nullptr) ? key : "";

    // TODO(skeleton): extract the raw payload with ut_kvp_getData(instance) so
    // queued messages carry the serialized KVP document.
    (void)instance;
    const std::string messagePayload;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_messageQueue.push({messageKey, messagePayload, m_userData});
}

} // namespace vcomponent::compositeinput::controller
