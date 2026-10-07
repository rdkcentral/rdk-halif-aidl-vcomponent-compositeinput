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
 * @file vcomponent_CompositeInputEventListener.h
 * @brief Dispatches CompositeInput port events to registered Binder listeners.
 */

#include <com/rdk/hal/PropertyValue.h>
#include <com/rdk/hal/compositeinput/ICompositeInputEventListener.h>
#include <com/rdk/hal/compositeinput/PortProperty.h>
#include <com/rdk/hal/compositeinput/State.h>

#include <utils/StrongPointer.h>

#include <vector>

namespace com::rdk::hal::compositeinput
{

/**
 * @brief Delivers the callbacks defined by ICompositeInputEventListener.
 *
 * A listener snapshot is supplied by the port so this class never invokes a
 * remote Binder callback while the port's state mutex is held.
 */
class CompositeInputEventListener final
{
public:
    // PUBLIC_INTERFACE
    /**
     * @brief Dispatch the ICompositeInputEventListener.onStateChanged callback.
     *
     * The callback itself is @c oneway. After dispatching, this method flushes
     * the Binder command buffer and then performs a synchronous Binder ping
     * round-trip to each listener that accepted the callback. That barrier
     * ensures the published state edge has been serviced by the listener's
     * process before the calling port advances to its next state or returns
     * from an in-flight lifecycle operation. Ping failures are logged and
     * swallowed, so an unresponsive observer cannot fail a HAL operation.
     *
     * @param listeners Snapshot of listeners registered with a port.
     * @param oldState State before the port transition.
     * @param newState State after the port transition.
     */
    static void onStateChanged(
        const std::vector<android::sp<ICompositeInputEventListener>>& listeners,
        State oldState,
        State newState);

    // PUBLIC_INTERFACE
    /**
     * @brief Dispatch the ICompositeInputEventListener.onPropertyChanged callback.
     *
     * @param listeners Snapshot of listeners registered with a port.
     * @param property Updated runtime or metric property.
     * @param value Updated property value.
     */
    static void onPropertyChanged(
        const std::vector<android::sp<ICompositeInputEventListener>>& listeners,
        PortProperty property,
        const ::com::rdk::hal::PropertyValue& value);
};

} // namespace com::rdk::hal::compositeinput
