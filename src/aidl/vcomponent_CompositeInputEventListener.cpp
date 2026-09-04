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

#include "aidl/vcomponent_CompositeInputEventListener.h"

#include "common/logger.h"

#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <binder/IPCThreadState.h>

#include <utils/Errors.h>

namespace com::rdk::hal::compositeinput
{

namespace
{
constexpr const char* kLogPrefix =
    "[VDEVICE_COMPOSITEINPUT]<CompositeInputEventListener>";

/**
 * @brief Round-trip on a listener handle so a queued oneway callback is drained.
 *
 * ICompositeInputEventListener is a frozen oneway interface, so a dispatched
 * callback is only queued when the call returns; the ping is a transport
 * primitive that orders it before the next edge without touching the AIDL
 * contract. A failure means the observer is gone and must not fail a HAL call.
 *
 * @param listener Listener proxy that accepted the preceding callback.
 */
void waitForListenerDelivery(
    const android::sp<ICompositeInputEventListener>& listener)
{
    const android::sp<android::IBinder> binder =
        android::IInterface::asBinder(listener);
    if (binder == nullptr)
    {
        // A local listener has no remote transport, so the callback already ran.
        return;
    }

    const android::status_t pingStatus = binder->pingBinder();
    if (pingStatus != android::OK)
    {
        LOGF_WARN("%s delivery barrier ping failed: status=%d",
                  kLogPrefix,
                  static_cast<int>(pingStatus));
    }
}

} // namespace

void CompositeInputEventListener::onStateChanged(
    const std::vector<android::sp<ICompositeInputEventListener>>& listeners,
    State oldState,
    State newState)
{
    // Listeners that accepted the callback and therefore still have work
    // pending on their Binder node for the delivery barrier below to drain.
    std::vector<android::sp<ICompositeInputEventListener>> queuedListeners;

    for (const auto& listener : listeners)
    {
        if (listener == nullptr)
        {
            continue;
        }

        const android::binder::Status status =
            listener->onStateChanged(oldState, newState);
        if (!status.isOk())
        {
            LOGF_WARN("%s onStateChanged callback failed: %s",
                      kLogPrefix,
                      status.toString8().c_str());
        }
        else
        {
            queuedListeners.push_back(listener);
        }
    }

    if (!queuedListeners.empty())
    {
        // Flush hands the oneway transaction to the driver; the per-listener
        // round-trip then proves this edge is observed before the next one.
        android::IPCThreadState::self()->flushCommands();

        for (const auto& listener : queuedListeners)
        {
            waitForListenerDelivery(listener);
        }
    }
}

void CompositeInputEventListener::onPropertyChanged(
    const std::vector<android::sp<ICompositeInputEventListener>>& listeners,
    PortProperty property,
    const ::com::rdk::hal::PropertyValue& value)
{
    bool callbackQueued = false;
    for (const auto& listener : listeners)
    {
        if (listener == nullptr)
        {
            continue;
        }

        const android::binder::Status status =
            listener->onPropertyChanged(property, value);
        if (!status.isOk())
        {
            LOGF_WARN("%s onPropertyChanged callback failed: %s",
                      kLogPrefix,
                      status.toString8().c_str());
        }
        else
        {
            callbackQueued = true;
        }
    }

    if (callbackQueued)
    {
        // Property callbacks share the oneway channel with lifecycle callbacks,
        // so flush their batch for consistent ordering.
        android::IPCThreadState::self()->flushCommands();
    }
}

} // namespace com::rdk::hal::compositeinput
