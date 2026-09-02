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
 * @brief UT-controller facade for the CompositeInput vcomponent.
 */

#include "controller/vcomponent_CompositeInputUtController.h"

#include "common/logger.h"
#include "utility/vcomponent_CompositeInputHelper.h"
#include "utility/vcomponent_CompositeInputParseConfig.h"

#include <cstdlib>
#include <sstream>

namespace vcomponent::compositeinput::controller
{

// UT control-plane profile key owned by this vcomponent.
static char* kControlPlaneComponentKey =
    const_cast<char*>(COMPOSITEINPUT_UTCONTROL_PROFILE_KEY);

static constexpr const char* kLogPrefix =
    "[VDEVICE_COMPOSITEINPUT]<CompositeInputUtController>";

const ut_control_keyStringMapping_t compositeInputUtControllerMapTable[] =
{
    // Reference host spellings.
    {const_cast<char*>("connection_status"),
     static_cast<int32_t>(CompositeInputUtCommand::CONNECTION_STATUS)},
    {const_cast<char*>("signal_status"),
     static_cast<int32_t>(CompositeInputUtCommand::SIGNAL_STATUS)},
    {const_cast<char*>("video_mode"),
     static_cast<int32_t>(CompositeInputUtCommand::VIDEO_MODE)},
    {const_cast<char*>("set_property"),
     static_cast<int32_t>(CompositeInputUtCommand::SET_PROPERTY)},
    {const_cast<char*>("clear_video_mode"),
     static_cast<int32_t>(CompositeInputUtCommand::CLEAR_VIDEO_MODE)},

    // camelCase spellings kept for backward compatibility with existing hosts.
    {const_cast<char*>("setConnection"),
     static_cast<int32_t>(CompositeInputUtCommand::CONNECTION_STATUS)},
    {const_cast<char*>("setSignalStatus"),
     static_cast<int32_t>(CompositeInputUtCommand::SIGNAL_STATUS)},
    {const_cast<char*>("setVideoMode"),
     static_cast<int32_t>(CompositeInputUtCommand::VIDEO_MODE)},
    {const_cast<char*>("setProperty"),
     static_cast<int32_t>(CompositeInputUtCommand::SET_PROPERTY)},
    {const_cast<char*>("clearVideoMode"),
     static_cast<int32_t>(CompositeInputUtCommand::CLEAR_VIDEO_MODE)},

    {nullptr, -1}
};

namespace
{
/**
 * @brief Append a comma-separated list of tokens to an inventory stream.
 *
 * @param[in,out] out     Destination stream.
 * @param[in]     tokens  Tokens to serialize.
 */
void appendTokenList(std::ostringstream& out, const std::vector<std::string>& tokens)
{
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        if (index != 0)
        {
            out << ",";
        }
        out << tokens[index];
    }
}

/**
 * @brief Append property metadata entries under a caller-provided key prefix.
 *
 * @param[in,out] out      Destination stream.
 * @param[in]     prefix   Key prefix, for example `platform` or `port.0`.
 * @param[in]     entries  Metadata entries to serialize.
 */
void appendPropertyMetadata(
    std::ostringstream& out,
    const std::string& prefix,
    const std::vector<utility::CompositeInputPropertyMetadataConfig>& entries)
{
    out << prefix << ".propertyMetadataCount=" << entries.size() << "\n";
    for (const auto& entry : entries)
    {
        out << prefix << ".propertyMetadata." << entry.key
            << ".type=" << entry.type
            << ",readOnly=" << (entry.readOnly ? "true" : "false")
            << ",isMetric=" << (entry.isMetric ? "true" : "false")
            << ",description=" << entry.description << "\n";
    }
}
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

CompositeInputUtCommand CompositeInputUtController::decodeCommand(const std::string& token)
{
    if (token.empty())
    {
        return CompositeInputUtCommand::UNKNOWN;
    }

    // ut-control takes a mutable buffer, so decode from a local copy rather than
    // casting away constness on the caller's string storage.
    std::string mutableToken = token;
    const int32_t decoded = UT_Control_GetMapValue(
        compositeInputUtControllerMapTable,
        mutableToken.data(),
        static_cast<int32_t>(CompositeInputUtCommand::UNKNOWN));

    return static_cast<CompositeInputUtCommand>(decoded);
}

bool CompositeInputUtController::loadConfiguration(
    const std::string& hfpYamlPath,
    std::string* outError)
{
    if (outError != nullptr)
    {
        outError->clear();
    }

    utility::CompositeInputHfpConfig cfg{};
    if (!utility::loadCompositeInputHfpConfigFromYaml(hfpYamlPath, &cfg, outError))
    {
        // Keep the controller unconfigured on any failure so callers can never
        // observe a partially parsed profile.
        m_hfpYamlPath.clear();
        m_hfpConfig = utility::CompositeInputHfpConfig{};
        return false;
    }

    m_hfpYamlPath = hfpYamlPath;
    m_hfpConfig = cfg;
    return true;
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

    if (outError != nullptr)
    {
        outError->clear();
    }

    std::ostringstream inv;
    inv << "CompositeInput inventory\n";
    inv << "hfpYamlPath=" << m_hfpYamlPath << "\n";
    inv << "interfaceVersion=" << m_hfpConfig.interfaceVersion << "\n";
    inv << "halVersion=" << m_hfpConfig.halVersion << "\n";
    inv << "portCount=" << m_hfpConfig.ports.size() << "\n";
    inv << "maxPorts=" << m_hfpConfig.maxPorts << "\n";
    inv << "maximumConcurrentStartedPorts=" << m_hfpConfig.maximumConcurrentStartedPorts << "\n";
    inv << "macrovisionDetectionSupported="
        << (m_hfpConfig.features.macrovisionDetectionSupported ? "true" : "false") << "\n";

    inv << "platform.supportedProperties=";
    appendTokenList(inv, m_hfpConfig.supportedProperties);
    inv << "\n";
    appendPropertyMetadata(inv, "platform", m_hfpConfig.propertyMetadata);

    for (const auto& port : m_hfpConfig.ports)
    {
        const std::string prefix = "port." + std::to_string(port.id);
        inv << prefix << ".name=" << port.name << "\n";
        inv << prefix << ".description=" << port.description << "\n";
        inv << prefix << ".supportedProperties=";
        appendTokenList(inv, port.supportedProperties);
        inv << "\n";
        appendPropertyMetadata(inv, prefix, port.propertyMetadata);
    }

    // Best-effort YAML size for observability; never affects the parsed result.
    if (!m_hfpYamlPath.empty())
    {
        const auto yaml = utility::readFileToString(m_hfpYamlPath);
        if (!yaml.has_value())
        {
            inv << "hfpYamlBytes=(unavailable)\n";
        }
        else
        {
            inv << "hfpYamlBytes=" << yaml->size() << "\n";
        }
    }

    *outInventory = inv.str();
    return true;
}

bool CompositeInputUtController::init(std::uint16_t port, void* userData)
{
    if (m_controlPlaneInstance != nullptr)
    {
        LOGF_INFO("%s CompositeInput UT control plane already initialized", kLogPrefix);
        return true;
    }

    m_userData = userData;
    m_controlPlaneInstance = UT_ControlPlane_Init(port);
    if (m_controlPlaneInstance == nullptr)
    {
        LOGF_ERROR("%s Failed to create CompositeInput UT control plane", kLogPrefix);
        m_userData = nullptr;
        return false;
    }

    const auto status = UT_ControlPlane_RegisterCallbackOnMessage(
        m_controlPlaneInstance,
        kControlPlaneComponentKey,
        &CompositeInputUtController::messageCallback,
        this);

    if (status != UT_CONTROL_PLANE_STATUS_OK)
    {
        LOGF_ERROR("%s Failed to register CompositeInput UT control-plane callback",
                   kLogPrefix);
        UT_ControlPlane_Exit(m_controlPlaneInstance);
        m_controlPlaneInstance = nullptr;
        m_userData = nullptr;
        return false;
    }

    LOGF_INFO("%s Registered UT callback key=%s", kLogPrefix, kControlPlaneComponentKey);
    UT_ControlPlane_Start(m_controlPlaneInstance);
    LOGF_INFO("%s Started CompositeInput UT control plane on port %u",
              kLogPrefix,
              static_cast<unsigned>(port));
    return true;
}

void CompositeInputUtController::shutdown()
{
    if (m_controlPlaneInstance != nullptr)
    {
        UT_ControlPlane_Exit(m_controlPlaneInstance);
        m_controlPlaneInstance = nullptr;
    }

    m_userData = nullptr;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::queue<CompositeInputUtMessage> emptyQueue;
    m_messageQueue.swap(emptyQueue);
}

std::optional<CompositeInputUtMessage> CompositeInputUtController::getMessage()
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
    LOGF_INFO("%s messageCallback invoked key=%s instance=%p userData=%p",
              kLogPrefix,
              key ? key : "(null)",
              instance,
              userData);

    auto* self = static_cast<CompositeInputUtController*>(userData);
    if (self != nullptr)
    {
        self->pushMessage(key, instance);
    }
}

void CompositeInputUtController::pushMessage(char* key, ut_kvp_instance_t* instance)
{
    const std::string messageKey = (key != nullptr) ? key : "";
    char* rawMessage = nullptr;

    if (instance != nullptr)
    {
        // Serialized KVP document carrying the UT command payload.
        rawMessage = ut_kvp_getData(instance);
    }

    const std::string messagePayload = (rawMessage != nullptr) ? rawMessage : "";
    // ut_kvp_getData() returns caller-owned heap memory: copy it before the
    // callback returns, then release it so high-frequency signal transitions
    // cannot exhaust the service process.
    std::free(rawMessage);

    LOGF_INFO("%s UT msg key=%s payload_size=%zu",
              kLogPrefix,
              messageKey.c_str(),
              messagePayload.size());
    LOGF_INFO("%s UT msg payload BEGIN\n%s\nUT msg payload END",
              kLogPrefix,
              messagePayload.c_str());

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_messageQueue.push({messageKey, messagePayload, m_userData});
}

} // namespace vcomponent::compositeinput::controller
