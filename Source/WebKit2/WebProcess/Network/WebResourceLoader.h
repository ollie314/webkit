/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebResourceLoader_h
#define WebResourceLoader_h

#include "Connection.h"
#include "MessageSender.h"
#include "ShareableResource.h"
#include <wtf/RefCounted.h>
#include <wtf/RefPtr.h>

#if USE(QUICK_LOOK)
#include <WebCore/QuickLook.h>
#endif

namespace IPC {
class DataReference;
}

namespace WebCore {
class ResourceError;
class ResourceLoader;
class ResourceRequest;
class ResourceResponse;
}

namespace WebKit {

typedef uint64_t ResourceLoadIdentifier;

class WebResourceLoader : public RefCounted<WebResourceLoader>, public IPC::MessageSender {
public:
    struct TrackingParameters {
        uint64_t pageID { 0 };
        uint64_t frameID { 0 };
        ResourceLoadIdentifier resourceID { 0 };
    };

    static Ref<WebResourceLoader> create(Ref<WebCore::ResourceLoader>&&, const TrackingParameters&);

    ~WebResourceLoader();

    void didReceiveWebResourceLoaderMessage(IPC::Connection&, IPC::Decoder&);

    WebCore::ResourceLoader* resourceLoader() const { return m_coreLoader.get(); }

    void detachFromCoreLoader();

    bool isAlwaysOnLoggingAllowed() const;

private:
    WebResourceLoader(Ref<WebCore::ResourceLoader>&&, const TrackingParameters&);

    // IPC::MessageSender
    IPC::Connection* messageSenderConnection() override;
    uint64_t messageSenderDestinationID() override;

    void willSendRequest(WebCore::ResourceRequest&&, WebCore::ResourceResponse&&);
    void didSendData(uint64_t bytesSent, uint64_t totalBytesToBeSent);
    void didReceiveResponse(const WebCore::ResourceResponse&, bool needsContinueDidReceiveResponseMessage);
    void didReceiveData(const IPC::DataReference&, int64_t encodedDataLength);
    void didFinishResourceLoad(double finishTime);
    void didFailResourceLoad(const WebCore::ResourceError&);
#if ENABLE(SHAREABLE_RESOURCE)
    void didReceiveResource(const ShareableResource::Handle&, double finishTime);
#endif

    RefPtr<WebCore::ResourceLoader> m_coreLoader;
    TrackingParameters m_trackingParameters;
    bool m_hasReceivedData { false };
};

} // namespace WebKit

#endif // WebResourceLoader_h
