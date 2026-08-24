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

#include "aidl/vcomponent_CompositeInputController.h"

#include "aidl/vcomponent_CompositeInputPort.h"

namespace com::rdk::hal::compositeinput
{

CompositeInputController::CompositeInputController(CompositeInputPort* port)
    : m_port(port)
{
}

android::binder::Status CompositeInputController::start()
{
    if (m_port == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->startFromController();
}

android::binder::Status CompositeInputController::stop()
{
    if (m_port == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->stopFromController();
}

android::binder::Status CompositeInputController::setProperty(
    PortProperty property,
    const ::com::rdk::hal::PropertyValue& value)
{
    (void)value;

    // Scenario 1: invalid enum -> ILLEGAL_ARGUMENT
    if (static_cast<int32_t>(property) < 0)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    if (m_port == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    // If property not supported by this port -> ILLEGAL_ARGUMENT
    if (!m_port->isPropertySupported(property))
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_ARGUMENT);
    }

    // Scenario 2: supported but read-only -> UNSUPPORTED_OPERATION
    return android::binder::Status::fromExceptionCode(
        android::binder::Status::EX_UNSUPPORTED_OPERATION);
}

android::binder::Status CompositeInputController::setPropertyMulti(
    const std::vector<PropertyKVPair>& properties)
{
    if (m_port == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    // If any invalid/unsupported property exists -> ILLEGAL_ARGUMENT
    for (const auto& kv : properties)
    {
        if (static_cast<int32_t>(kv.property) < 0 ||
            !m_port->isPropertySupported(kv.property))
        {
            return android::binder::Status::fromExceptionCode(
                android::binder::Status::EX_ILLEGAL_ARGUMENT);
        }
    }

    // All supported but read-only -> UNSUPPORTED_OPERATION
    return android::binder::Status::fromExceptionCode(
        android::binder::Status::EX_UNSUPPORTED_OPERATION);
}

android::binder::Status CompositeInputController::resetMetrics()
{
    if (m_port == nullptr)
    {
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->resetMetricsFromController();
}

} // namespace com::rdk::hal::compositeinput