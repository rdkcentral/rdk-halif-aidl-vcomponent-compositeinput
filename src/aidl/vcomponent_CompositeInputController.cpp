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
#include "common/logger.h"

namespace com::rdk::hal::compositeinput
{

namespace
{
constexpr const char* kLogPrefix = "[VDEVICE_COMPOSITEINPUT]<CompositeInputController>";
} // namespace

CompositeInputController::CompositeInputController(CompositeInputPort* port)
    : m_port(port)
{
}

android::binder::Status CompositeInputController::start()
{
    LOGF_INFO("%s start entry", kLogPrefix);

    if (m_port == nullptr)
    {
        LOGF_ERROR("%s start: null port", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->startFromController();
}

android::binder::Status CompositeInputController::stop()
{
    LOGF_INFO("%s stop entry", kLogPrefix);

    if (m_port == nullptr)
    {
        LOGF_ERROR("%s stop: null port", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->stopFromController();
}

android::binder::Status CompositeInputController::setProperty(
    PortProperty property,
    const ::com::rdk::hal::PropertyValue& value)
{
    LOGF_INFO("%s setProperty entry property=%d",
              kLogPrefix,
              static_cast<int>(property));

    if (m_port == nullptr)
    {
        LOGF_ERROR("%s setProperty: null port", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->setPropertyFromController(property, value);
}

android::binder::Status CompositeInputController::setPropertyMulti(
    const std::vector<PropertyKVPair>& properties)
{
    LOGF_INFO("%s setPropertyMulti entry count=%zu", kLogPrefix, properties.size());

    if (m_port == nullptr)
    {
        LOGF_ERROR("%s setPropertyMulti: null port", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->setPropertyMultiFromController(properties);
}

android::binder::Status CompositeInputController::resetMetrics()
{
    LOGF_INFO("%s resetMetrics entry", kLogPrefix);

    if (m_port == nullptr)
    {
        LOGF_ERROR("%s resetMetrics: null port", kLogPrefix);
        return android::binder::Status::fromExceptionCode(
            android::binder::Status::EX_ILLEGAL_STATE);
    }

    return m_port->resetMetricsFromController();
}

} // namespace com::rdk::hal::compositeinput
