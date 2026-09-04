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
 * @file vcomponent_CompositeInputController.h
 * @brief Exclusive controller implementation for a CompositeInput port.
 */

#include <com/rdk/hal/compositeinput/BnCompositeInputController.h>
#include <com/rdk/hal/compositeinput/PortProperty.h>
#include <com/rdk/hal/compositeinput/PropertyKVPair.h>
#include <com/rdk/hal/PropertyValue.h>

#include <binder/Status.h>

#include <vector>

namespace com::rdk::hal::compositeinput
{

class CompositeInputPort;

/**
 * @brief Owns exclusive mutation and start/stop access to one open port.
 */
class CompositeInputController final : public BnCompositeInputController
{
public:
    /**
     * @brief Construct a controller for an already-open port.
     *
     * @param port Owning port. The port outlives the controller.
     */
    explicit CompositeInputController(CompositeInputPort* port);

    // PUBLIC_INTERFACE
    /**
     * @brief Start the owning port.
     *
     * @return Binder operation status.
     */
    android::binder::Status start() override;

    // PUBLIC_INTERFACE
    /**
     * @brief Stop the owning port.
     *
     * @return Binder operation status.
     */
    android::binder::Status stop() override;

    // PUBLIC_INTERFACE
    /**
     * @brief Set a mutable port property.
     *
     * Writability is determined by the owning port's HFP-derived
     * `PropertyMetadata.readOnly` value.
     *
     * @param property Requested property key.
     * @param value Requested property value.
     * @return Binder operation status.
     */
    android::binder::Status setProperty(
        PortProperty property,
        const ::com::rdk::hal::PropertyValue& value) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Atomically set mutable port properties.
     *
     * @param properties Requested property updates.
     * @return Binder operation status.
     */
    android::binder::Status setPropertyMulti(
        const std::vector<PropertyKVPair>& properties) override;

    // PUBLIC_INTERFACE
    /**
     * @brief Reset telemetry metrics on the started port.
     *
     * @return Binder operation status.
     */
    android::binder::Status resetMetrics() override;

private:
    CompositeInputPort* const m_port;
};

} // namespace com::rdk::hal::compositeinput
