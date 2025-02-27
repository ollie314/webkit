/*
 * Copyright (C) 2004, 2006, 2007, 2008, 2009, 2010, 2013 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "ResourceHandle.h"
#include "ResourceHandleInternal.h"

#include "Logging.h"
#include "NetworkingContext.h"
#include "NotImplemented.h"
#include "ResourceHandleClient.h"
#include "Timer.h"
#include <algorithm>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/AtomicStringHash.h>
#include <wtf/text/CString.h>

namespace WebCore {

static bool shouldForceContentSniffing;

typedef HashMap<AtomicString, ResourceHandle::BuiltinConstructor> BuiltinResourceHandleConstructorMap;
static BuiltinResourceHandleConstructorMap& builtinResourceHandleConstructorMap()
{
#if PLATFORM(IOS)
    ASSERT(WebThreadIsLockedOrDisabled());
#else
    ASSERT(isMainThread());
#endif
    static NeverDestroyed<BuiltinResourceHandleConstructorMap> map;
    return map;
}

void ResourceHandle::registerBuiltinConstructor(const AtomicString& protocol, ResourceHandle::BuiltinConstructor constructor)
{
    builtinResourceHandleConstructorMap().add(protocol, constructor);
}

typedef HashMap<AtomicString, ResourceHandle::BuiltinSynchronousLoader> BuiltinResourceHandleSynchronousLoaderMap;
static BuiltinResourceHandleSynchronousLoaderMap& builtinResourceHandleSynchronousLoaderMap()
{
    ASSERT(isMainThread());
    static NeverDestroyed<BuiltinResourceHandleSynchronousLoaderMap> map;
    return map;
}

void ResourceHandle::registerBuiltinSynchronousLoader(const AtomicString& protocol, ResourceHandle::BuiltinSynchronousLoader loader)
{
    builtinResourceHandleSynchronousLoaderMap().add(protocol, loader);
}

ResourceHandle::ResourceHandle(NetworkingContext* context, const ResourceRequest& request, ResourceHandleClient* client, bool defersLoading, bool shouldContentSniff)
    : d(std::make_unique<ResourceHandleInternal>(this, context, request, client, defersLoading, shouldContentSniff && shouldContentSniffURL(request.url())))
{
    if (!request.url().isValid()) {
        scheduleFailure(InvalidURLFailure);
        return;
    }

    if (!portAllowed(request.url())) {
        scheduleFailure(BlockedFailure);
        return;
    }
}

RefPtr<ResourceHandle> ResourceHandle::create(NetworkingContext* context, const ResourceRequest& request, ResourceHandleClient* client, bool defersLoading, bool shouldContentSniff)
{
    if (auto constructor = builtinResourceHandleConstructorMap().get(request.url().protocol().toStringWithoutCopying()))
        return constructor(request, client);

    auto newHandle = adoptRef(*new ResourceHandle(context, request, client, defersLoading, shouldContentSniff));

    if (newHandle->d->m_scheduledFailureType != NoFailure)
        return WTFMove(newHandle);

    if (newHandle->start())
        return WTFMove(newHandle);

    return nullptr;
}

void ResourceHandle::scheduleFailure(FailureType type)
{
    d->m_scheduledFailureType = type;
    d->m_failureTimer.startOneShot(0);
}

void ResourceHandle::failureTimerFired()
{
    if (!client())
        return;

    switch (d->m_scheduledFailureType) {
        case NoFailure:
            ASSERT_NOT_REACHED();
            return;
        case BlockedFailure:
            d->m_scheduledFailureType = NoFailure;
            client()->wasBlocked(this);
            return;
        case InvalidURLFailure:
            d->m_scheduledFailureType = NoFailure;
            client()->cannotShowURL(this);
            return;
    }

    ASSERT_NOT_REACHED();
}

void ResourceHandle::loadResourceSynchronously(NetworkingContext* context, const ResourceRequest& request, StoredCredentials storedCredentials, ResourceError& error, ResourceResponse& response, Vector<char>& data)
{
    if (auto constructor = builtinResourceHandleSynchronousLoaderMap().get(request.url().protocol().toStringWithoutCopying())) {
        constructor(context, request, storedCredentials, error, response, data);
        return;
    }

    platformLoadResourceSynchronously(context, request, storedCredentials, error, response, data);
}

ResourceHandleClient* ResourceHandle::client() const
{
    return d->m_client;
}

void ResourceHandle::clearClient()
{
    d->m_client = nullptr;
}

void ResourceHandle::didReceiveResponse(ResourceResponse&& response)
{
    if (response.isHttpVersion0_9()) {
        auto url = response.url();
        Optional<uint16_t> port = url.port();
        if (port && !isDefaultPortForProtocol(port.value(), url.protocol())) {
            cancel();
            String message = "Cancelled load from '" + url.stringCenterEllipsizedToLength() + "' because it is using HTTP/0.9.";
            d->m_client->didFail(this, { String(), 0, url, message });
            return;
        }
    }
    if (d->m_usesAsyncCallbacks)
        d->m_client->didReceiveResponseAsync(this, WTFMove(response));
    else {
        d->m_client->didReceiveResponse(this, WTFMove(response));
        platformContinueSynchronousDidReceiveResponse();
    }
}

#if !PLATFORM(COCOA) && !USE(CFURLCONNECTION) && !USE(SOUP)
// ResourceHandle never uses async client calls on these platforms yet.
void ResourceHandle::continueWillSendRequest(ResourceRequest&&)
{
    notImplemented();
}

void ResourceHandle::continueDidReceiveResponse()
{
    notImplemented();
}

#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
void ResourceHandle::continueCanAuthenticateAgainstProtectionSpace(bool)
{
    notImplemented();
}
#endif
#endif

#if !USE(SOUP)
void ResourceHandle::platformContinueSynchronousDidReceiveResponse()
{
    // Do nothing.
}
#endif

ResourceRequest& ResourceHandle::firstRequest()
{
    return d->m_firstRequest;
}

NetworkingContext* ResourceHandle::context() const
{
    return d->m_context.get();
}

const String& ResourceHandle::lastHTTPMethod() const
{
    return d->m_lastHTTPMethod;
}

bool ResourceHandle::hasAuthenticationChallenge() const
{
    return !d->m_currentWebChallenge.isNull();
}

void ResourceHandle::clearAuthentication()
{
#if PLATFORM(COCOA)
    d->m_currentMacChallenge = nil;
#endif
    d->m_currentWebChallenge.nullify();
}
  
bool ResourceHandle::shouldContentSniff() const
{
    return d->m_shouldContentSniff;
}

bool ResourceHandle::shouldContentSniffURL(const URL& url)
{
#if PLATFORM(COCOA)
    if (shouldForceContentSniffing)
        return true;
#endif
    // We shouldn't content sniff file URLs as their MIME type should be established via their extension.
    return !url.protocolIs("file");
}

void ResourceHandle::forceContentSniffing()
{
    shouldForceContentSniffing = true;
}

void ResourceHandle::setDefersLoading(bool defers)
{
    LOG(Network, "Handle %p setDefersLoading(%s)", this, defers ? "true" : "false");

    ASSERT(d->m_defersLoading != defers); // Deferring is not counted, so calling setDefersLoading() repeatedly is likely to be in error.
    d->m_defersLoading = defers;

    if (defers) {
        ASSERT(d->m_failureTimer.isActive() == (d->m_scheduledFailureType != NoFailure));
        if (d->m_failureTimer.isActive())
            d->m_failureTimer.stop();
    } else if (d->m_scheduledFailureType != NoFailure) {
        ASSERT(!d->m_failureTimer.isActive());
        d->m_failureTimer.startOneShot(0);
    }

    platformSetDefersLoading(defers);
}

bool ResourceHandle::usesAsyncCallbacks() const
{
    return d->m_usesAsyncCallbacks;
}

} // namespace WebCore
