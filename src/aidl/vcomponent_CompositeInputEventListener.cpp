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

namespace com::rdk::hal::compositeinput
{

void CompositeInputEventListener::onStateChanged(
    const std::vector<android::sp<ICompositeInputEventListener>>& listeners,
    State oldState,
    State newState)
{
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
            LOGF_WARN("[VDEVICE_COMPOSITEINPUT]<EventListener> "
                      "onStateChanged callback failed: %s",
                      status.toString8().c_str());
        }
    }
}

void CompositeInputEventListener::onPropertyChanged(
    const std::vector<android::sp<ICompositeInputEventListener>>& listeners,
    PortProperty property,
    const ::com::rdk::hal::PropertyValue& value)
{
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
            LOGF_WARN("[VDEVICE_COMPOSITEINPUT]<EventListener> "
                      "onPropertyChanged callback failed: %s",
                      status.toString8().c_str());
        }
    }
}

} // namespace com::rdk::hal::compositeinput
