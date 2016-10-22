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

#include "config.h"
#include "UserMediaPermissionRequestManager.h"

#if ENABLE(MEDIA_STREAM)

#include "WebFrame.h"
#include "WebPage.h"
#include "WebPageProxyMessages.h"
#include <WebCore/CaptureDevice.h>
#include <WebCore/Document.h>
#include <WebCore/Frame.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/SecurityOrigin.h>

using namespace WebCore;

namespace WebKit {

using namespace WebCore;

static uint64_t generateRequestID()
{
    static uint64_t uniqueRequestID = 1;
    return uniqueRequestID++;
}

UserMediaPermissionRequestManager::UserMediaPermissionRequestManager(WebPage& page)
    : m_page(page)
{
}

UserMediaPermissionRequestManager::~UserMediaPermissionRequestManager()
{
    for (auto& sandboxExtension : m_userMediaDeviceSandboxExtensions)
        sandboxExtension->revoke();
}

void UserMediaPermissionRequestManager::startUserMediaRequest(UserMediaRequest& request)
{
    Document* document = request.document();
    Frame* frame = document ? document->frame() : nullptr;

    if (!frame) {
        request.deny(UserMediaRequest::OtherFailure, emptyString());
        return;
    }

    uint64_t requestID = generateRequestID();
    m_idToUserMediaRequestMap.add(requestID, &request);
    m_userMediaRequestToIDMap.add(&request, requestID);

    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    SecurityOrigin* topLevelDocumentOrigin = request.topLevelDocumentOrigin();
    String topLevelDocumentOriginString = topLevelDocumentOrigin ? topLevelDocumentOrigin->databaseIdentifier() : emptyString();
    m_page.send(Messages::WebPageProxy::RequestUserMediaPermissionForFrame(requestID, webFrame->frameID(), request.userMediaDocumentOrigin()->databaseIdentifier(), topLevelDocumentOriginString, request.audioConstraints().data(), request.videoConstraints().data()));
}

void UserMediaPermissionRequestManager::cancelUserMediaRequest(UserMediaRequest& request)
{
    uint64_t requestID = m_userMediaRequestToIDMap.take(&request);
    if (!requestID)
        return;
    m_idToUserMediaRequestMap.remove(requestID);
}

void UserMediaPermissionRequestManager::userMediaAccessWasGranted(uint64_t requestID, const String& audioDeviceUID, const String& videoDeviceUID)
{
    auto request = m_idToUserMediaRequestMap.take(requestID);
    if (!request)
        return;
    m_userMediaRequestToIDMap.remove(request);

    request->allow(audioDeviceUID, videoDeviceUID);
}

void UserMediaPermissionRequestManager::userMediaAccessWasDenied(uint64_t requestID, WebCore::UserMediaRequest::MediaAccessDenialReason reason, const String& invalidConstraint)
{
    auto request = m_idToUserMediaRequestMap.take(requestID);
    if (!request)
        return;
    m_userMediaRequestToIDMap.remove(request);

    request->deny(reason, invalidConstraint);
}

void UserMediaPermissionRequestManager::enumerateMediaDevices(MediaDevicesEnumerationRequest& request)
{
    auto* document = downcast<Document>(request.scriptExecutionContext());
    auto* frame = document ? document->frame() : nullptr;

    if (!frame) {
        request.setDeviceInfo(Vector<CaptureDevice>(), emptyString(), false);
        return;
    }

    uint64_t requestID = generateRequestID();
    m_idToMediaDevicesEnumerationRequestMap.add(requestID, &request);
    m_mediaDevicesEnumerationRequestToIDMap.add(&request, requestID);

    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    SecurityOrigin* topLevelDocumentOrigin = request.topLevelDocumentOrigin();
    String topLevelDocumentOriginString = topLevelDocumentOrigin ? topLevelDocumentOrigin->databaseIdentifier() : emptyString();
    m_page.send(Messages::WebPageProxy::EnumerateMediaDevicesForFrame(requestID, webFrame->frameID(), request.userMediaDocumentOrigin()->databaseIdentifier(), topLevelDocumentOriginString));
}

void UserMediaPermissionRequestManager::cancelMediaDevicesEnumeration(WebCore::MediaDevicesEnumerationRequest& request)
{
    uint64_t requestID = m_mediaDevicesEnumerationRequestToIDMap.take(&request);
    if (!requestID)
        return;
    m_idToMediaDevicesEnumerationRequestMap.remove(requestID);
}

void UserMediaPermissionRequestManager::didCompleteMediaDeviceEnumeration(uint64_t requestID, const Vector<CaptureDevice>& deviceList, const String& mediaDeviceIdentifierHashSalt, bool hasPersistentAccess)
{
    RefPtr<MediaDevicesEnumerationRequest> request = m_idToMediaDevicesEnumerationRequestMap.take(requestID);
    if (!request)
        return;
    m_mediaDevicesEnumerationRequestToIDMap.remove(request);
    
    request->setDeviceInfo(deviceList, mediaDeviceIdentifierHashSalt, hasPersistentAccess);
}

void UserMediaPermissionRequestManager::grantUserMediaDevicesSandboxExtension(const SandboxExtension::HandleArray& sandboxExtensionHandles)
{
    ASSERT(m_userMediaDeviceSandboxExtensions.size() <= 2);

    for (size_t i = 0; i < sandboxExtensionHandles.size(); i++) {
        if (RefPtr<SandboxExtension> extension = SandboxExtension::create(sandboxExtensionHandles[i])) {
            extension->consume();
            m_userMediaDeviceSandboxExtensions.append(extension.release());
        }
    }
}

} // namespace WebKit

#endif // ENABLE(MEDIA_STREAM)
