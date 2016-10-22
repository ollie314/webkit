/*
 * Copyright (C) 2014 Igalia S.L.
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef UserMediaPermissionRequestManagerProxy_h
#define UserMediaPermissionRequestManagerProxy_h

#include "UserMediaPermissionCheckProxy.h"
#include "UserMediaPermissionRequestProxy.h"
#include <WebCore/UserMediaRequest.h>
#include <wtf/HashMap.h>

namespace WebCore {
class CaptureDevice;
struct MediaConstraintsData;
};

namespace WebKit {

class WebPageProxy;

class UserMediaPermissionRequestManagerProxy {
public:
    explicit UserMediaPermissionRequestManagerProxy(WebPageProxy&);

    void invalidateRequests();

    void requestUserMediaPermissionForFrame(uint64_t userMediaID, uint64_t frameID, String userMediaDocumentOriginIdentifier, String topLevelDocumentOriginIdentifier, const WebCore::MediaConstraintsData& audioConstraintsData, const WebCore::MediaConstraintsData& videoConstraintsData);

    void userMediaAccessWasGranted(uint64_t, const String& audioDeviceUID, const String& videoDeviceUID);
    void userMediaAccessWasDenied(uint64_t, UserMediaPermissionRequestProxy::UserMediaAccessDenialReason);

    void enumerateMediaDevicesForFrame(uint64_t userMediaID, uint64_t frameID, String userMediaDocumentOriginIdentifier, String topLevelDocumentOriginIdentifier);

    void didCompleteUserMediaPermissionCheck(uint64_t, const String&, bool allow);

private:
    Ref<UserMediaPermissionRequestProxy> createRequest(uint64_t, const Vector<String>& audioDeviceUIDs, const Vector<String>& videoDeviceUIDs);
    Ref<UserMediaPermissionRequestProxy> createRequest(uint64_t userMediaID);
    void denyRequest(uint64_t userMediaID, UserMediaPermissionRequestProxy::UserMediaAccessDenialReason, const String& invalidConstraint);
    Ref<UserMediaPermissionCheckProxy> createUserMediaPermissionCheck(uint64_t userMediaID);
    void syncWithWebCorePrefs() const;

    HashMap<uint64_t, RefPtr<UserMediaPermissionRequestProxy>> m_pendingUserMediaRequests;
    HashMap<uint64_t, RefPtr<UserMediaPermissionCheckProxy>> m_pendingDeviceRequests;

    enum SandboxExtensionsGranted {
        None = 0,
        Video = 1 << 0,
        Audio = 1 << 1
    };
    HashMap<uint64_t, unsigned> m_pageSandboxExtensionsGranted;

    WebPageProxy& m_page;
};

} // namespace WebKit

#endif // UserMediaPermissionRequestManagerProxy_h
