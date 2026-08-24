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

#include "aidl/vcomponent_CompositeInputControllerListener.h"

#include "common/logger.h"

namespace com::rdk::hal::compositeinput
{

void CompositeInputControllerListener::onConnectionChanged(
    const android::sp<ICompositeInputControllerListener>& listener,
    bool connected)
{
    if (listener == nullptr)
    {
        return;
    }

    const android::binder::Status status = listener->onConnectionChanged(connected);
    if (!status.isOk())
    {
        LOGF_WARN("[VDEVICE_COMPOSITEINPUT]<ControllerListener> "
                  "onConnectionChanged callback failed: %s",
                  status.toString8().c_str());
    }
}

void CompositeInputControllerListener::onSignalStatusChanged(
    const android::sp<ICompositeInputControllerListener>& listener,
    SignalStatus signalStatus)
{
    if (listener == nullptr)
    {
        return;
    }

    const android::binder::Status status =
        listener->onSignalStatusChanged(signalStatus);
    if (!status.isOk())
    {
        LOGF_WARN("[VDEVICE_COMPOSITEINPUT]<ControllerListener> "
                  "onSignalStatusChanged callback failed: %s",
                  status.toString8().c_str());
    }
}

void CompositeInputControllerListener::onVideoModeChanged(
    const android::sp<ICompositeInputControllerListener>& listener,
    const VideoResolution& resolution)
{
    if (listener == nullptr)
    {
        return;
    }

    const android::binder::Status status = listener->onVideoModeChanged(resolution);
    if (!status.isOk())
    {
        LOGF_WARN("[VDEVICE_COMPOSITEINPUT]<ControllerListener> "
                  "onVideoModeChanged callback failed: %s",
                  status.toString8().c_str());
    }
}

} // namespace com::rdk::hal::compositeinput
