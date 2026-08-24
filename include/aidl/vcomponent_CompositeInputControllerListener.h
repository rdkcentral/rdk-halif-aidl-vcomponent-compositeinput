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
 * @file vcomponent_CompositeInputControllerListener.h
 * @brief Dispatches exclusive-controller CompositeInput hardware events.
 */

#include <com/rdk/hal/compositeinput/ICompositeInputControllerListener.h>
#include <com/rdk/hal/compositeinput/SignalStatus.h>
#include <com/rdk/hal/compositeinput/VideoResolution.h>

#include <utils/StrongPointer.h>

namespace com::rdk::hal::compositeinput
{

/**
 * @brief Delivers the callbacks defined by ICompositeInputControllerListener.
 */
class CompositeInputControllerListener final
{
public:
    // PUBLIC_INTERFACE
    /**
     * @brief Dispatch the ICompositeInputControllerListener.onConnectionChanged callback.
     *
     * @param listener Listener supplied to ICompositeInputPort.open().
     * @param connected True when the physical composite cable is connected.
     */
    static void onConnectionChanged(
        const android::sp<ICompositeInputControllerListener>& listener,
        bool connected);

    // PUBLIC_INTERFACE
    /**
     * @brief Dispatch the ICompositeInputControllerListener.onSignalStatusChanged callback.
     *
     * @param listener Listener supplied to ICompositeInputPort.open().
     * @param signalStatus Newly detected CompositeInput signal status.
     */
    static void onSignalStatusChanged(
        const android::sp<ICompositeInputControllerListener>& listener,
        SignalStatus signalStatus);

    // PUBLIC_INTERFACE
    /**
     * @brief Dispatch the ICompositeInputControllerListener.onVideoModeChanged callback.
     *
     * @param listener Listener supplied to ICompositeInputPort.open().
     * @param resolution Newly detected CompositeInput video mode.
     */
    static void onVideoModeChanged(
        const android::sp<ICompositeInputControllerListener>& listener,
        const VideoResolution& resolution);
};

} // namespace com::rdk::hal::compositeinput
