/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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

#include "config.h"
#include "NetworkLoad.h"

#include "AuthenticationManager.h"
#include "DownloadProxyMessages.h"
#include "NetworkProcess.h"
#include "SessionTracker.h"
#include "WebCoreArgumentCoders.h"
#include "WebErrors.h"
#include <WebCore/NotImplemented.h>
#include <WebCore/ResourceHandle.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/SessionID.h>
#include <WebCore/SharedBuffer.h>
#include <wtf/MainThread.h>

#if PLATFORM(COCOA)
#include "NetworkDataTaskCocoa.h"
#endif

namespace WebKit {

using namespace WebCore;

#if USE(NETWORK_SESSION)

struct NetworkLoad::Throttle {
    Throttle(NetworkLoad& load, std::chrono::milliseconds delay, ResourceResponse&& response, ResponseCompletionHandler&& handler)
        : timer(load, &NetworkLoad::throttleDelayCompleted)
        , response(WTFMove(response))
        , responseCompletionHandler(WTFMove(handler))
    {
        timer.startOneShot(delay);
    }
    Timer timer;
    ResourceResponse response;
    ResponseCompletionHandler responseCompletionHandler;
};

NetworkLoad::NetworkLoad(NetworkLoadClient& client, NetworkLoadParameters&& parameters, NetworkSession& networkSession)
    : m_client(client)
    , m_parameters(WTFMove(parameters))
    , m_currentRequest(m_parameters.request)
{
    m_task = NetworkDataTask::create(networkSession, *this, m_parameters);
    if (!m_parameters.defersLoading)
        m_task->resume();
}

#else

NetworkLoad::NetworkLoad(NetworkLoadClient& client, NetworkLoadParameters&& parameters)
    : m_client(client)
    , m_parameters(WTFMove(parameters))
    , m_networkingContext(RemoteNetworkingContext::create(m_parameters.sessionID, m_parameters.shouldClearReferrerOnHTTPSToHTTPRedirect))
    , m_currentRequest(m_parameters.request)
{
    m_handle = ResourceHandle::create(m_networkingContext.get(), m_parameters.request, this, m_parameters.defersLoading, m_parameters.contentSniffingPolicy == SniffContent);
}

#endif

NetworkLoad::~NetworkLoad()
{
    ASSERT(RunLoop::isMain());
#if USE(NETWORK_SESSION)
    if (m_responseCompletionHandler)
        m_responseCompletionHandler(PolicyIgnore);
#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
    if (m_challengeCompletionHandler)
        m_challengeCompletionHandler(AuthenticationChallengeDisposition::Cancel, { });
#endif
    if (m_task)
        m_task->clearClient();
#else
#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
    if (m_handle && m_waitingForContinueCanAuthenticateAgainstProtectionSpace)
        m_handle->continueCanAuthenticateAgainstProtectionSpace(false);
#endif
    if (m_handle)
        m_handle->clearClient();
#endif
}

void NetworkLoad::setDefersLoading(bool defers)
{
#if USE(NETWORK_SESSION)
    if (m_task) {
        if (defers)
            m_task->suspend();
        else
            m_task->resume();
    }
#else
    if (m_handle)
        m_handle->setDefersLoading(defers);
#endif
}

void NetworkLoad::cancel()
{
#if USE(NETWORK_SESSION)
    if (m_task)
        m_task->cancel();
#else
    if (m_handle)
        m_handle->cancel();
#endif
}

void NetworkLoad::continueWillSendRequest(WebCore::ResourceRequest&& newRequest)
{
#if PLATFORM(COCOA)
    m_currentRequest.updateFromDelegatePreservingOldProperties(newRequest.nsURLRequest(DoNotUpdateHTTPBody));
#elif USE(SOUP)
    // FIXME: Implement ResourceRequest::updateFromDelegatePreservingOldProperties. See https://bugs.webkit.org/show_bug.cgi?id=126127.
    m_currentRequest.updateFromDelegatePreservingOldProperties(newRequest);
#endif

#if USE(NETWORK_SESSION)
    auto redirectCompletionHandler = std::exchange(m_redirectCompletionHandler, nullptr);
    ASSERT(redirectCompletionHandler);
    if (m_currentRequest.isNull()) {
        didCompleteWithError(cancelledError(m_currentRequest));
        if (redirectCompletionHandler)
            redirectCompletionHandler({ });
        return;
    }

    if (redirectCompletionHandler)
        redirectCompletionHandler(m_currentRequest);
#else
    if (m_currentRequest.isNull()) {
        if (m_handle)
            m_handle->cancel();
        didFail(m_handle.get(), cancelledError(m_currentRequest));
    } else if (m_handle) {
        auto currentRequestCopy = m_currentRequest;
        m_handle->continueWillSendRequest(WTFMove(currentRequestCopy));
    }
#endif
}

void NetworkLoad::continueDidReceiveResponse()
{
#if USE(NETWORK_SESSION)
    if (m_responseCompletionHandler) {
        auto responseCompletionHandler = std::exchange(m_responseCompletionHandler, nullptr);
        responseCompletionHandler(PolicyUse);
    }
#else
    if (m_handle)
        m_handle->continueDidReceiveResponse();
#endif
}

NetworkLoadClient::ShouldContinueDidReceiveResponse NetworkLoad::sharedDidReceiveResponse(ResourceResponse&& response)
{
    response.setSource(ResourceResponse::Source::Network);
    if (m_parameters.needsCertificateInfo)
        response.includeCertificateInfo();

    return m_client.get().didReceiveResponse(WTFMove(response));
}

void NetworkLoad::sharedWillSendRedirectedRequest(ResourceRequest&& request, ResourceResponse&& redirectResponse)
{
    // We only expect to get the willSendRequest callback from ResourceHandle as the result of a redirect.
    ASSERT(!redirectResponse.isNull());
    ASSERT(RunLoop::isMain());

    auto oldRequest = WTFMove(m_currentRequest);
    m_currentRequest = request;
    m_client.get().willSendRedirectedRequest(WTFMove(oldRequest), WTFMove(request), WTFMove(redirectResponse));
}

#if USE(NETWORK_SESSION)

void NetworkLoad::convertTaskToDownload(PendingDownload& pendingDownload, const ResourceRequest& updatedRequest, const ResourceResponse& response)
{
    if (!m_task)
        return;

    m_client = pendingDownload;
    m_currentRequest = updatedRequest;
    m_task->setPendingDownload(pendingDownload);

    if (m_responseCompletionHandler)
        NetworkProcess::singleton().findPendingDownloadLocation(*m_task.get(), std::exchange(m_responseCompletionHandler, nullptr), response);
}

void NetworkLoad::setPendingDownloadID(DownloadID downloadID)
{
    if (!m_task)
        return;

    m_task->setPendingDownloadID(downloadID);
}

void NetworkLoad::setSuggestedFilename(const String& suggestedName)
{
    if (!m_task)
        return;

    m_task->setSuggestedFilename(suggestedName);
}

void NetworkLoad::setPendingDownload(PendingDownload& pendingDownload)
{
    if (!m_task)
        return;

    m_task->setPendingDownload(pendingDownload);
}

void NetworkLoad::willPerformHTTPRedirection(ResourceResponse&& response, ResourceRequest&& request, RedirectCompletionHandler&& completionHandler)
{
    ASSERT(!m_redirectCompletionHandler);
    m_redirectCompletionHandler = WTFMove(completionHandler);
    sharedWillSendRedirectedRequest(WTFMove(request), WTFMove(response));
}

void NetworkLoad::didReceiveChallenge(const AuthenticationChallenge& challenge, ChallengeCompletionHandler&& completionHandler)
{
    // Handle server trust evaluation at platform-level if requested, for performance reasons.
#if PLATFORM(COCOA)
    if (challenge.protectionSpace().authenticationScheme() == ProtectionSpaceAuthenticationSchemeServerTrustEvaluationRequested
        && !NetworkProcess::singleton().canHandleHTTPSServerTrustEvaluation()) {
        if (m_task && m_task->allowsSpecificHTTPSCertificateForHost(challenge))
            completionHandler(AuthenticationChallengeDisposition::UseCredential, serverTrustCredential(challenge));
        else
            completionHandler(AuthenticationChallengeDisposition::RejectProtectionSpace, { });
        return;
    }
#endif

    m_challenge = challenge;
#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
    m_challengeCompletionHandler = WTFMove(completionHandler);
    m_client.get().canAuthenticateAgainstProtectionSpaceAsync(challenge.protectionSpace());
#else
    completeAuthenticationChallenge(WTFMove(completionHandler));
#endif
}

void NetworkLoad::completeAuthenticationChallenge(ChallengeCompletionHandler&& completionHandler)
{
    if (m_parameters.clientCredentialPolicy == ClientCredentialPolicy::CannotAskClientForCredentials) {
        completionHandler(AuthenticationChallengeDisposition::UseCredential, { });
        return;
    }

    if (!m_task)
        return;

    if (auto* pendingDownload = m_task->pendingDownload())
        NetworkProcess::singleton().authenticationManager().didReceiveAuthenticationChallenge(*pendingDownload, *m_challenge, WTFMove(completionHandler));
    else
        NetworkProcess::singleton().authenticationManager().didReceiveAuthenticationChallenge(m_parameters.webPageID, m_parameters.webFrameID, *m_challenge, WTFMove(completionHandler));
}

#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
void NetworkLoad::continueCanAuthenticateAgainstProtectionSpace(bool result)
{
    ASSERT(m_challengeCompletionHandler);
    auto completionHandler = std::exchange(m_challengeCompletionHandler, nullptr);
    if (!result) {
        if (m_task && m_task->allowsSpecificHTTPSCertificateForHost(*m_challenge))
            completionHandler(AuthenticationChallengeDisposition::UseCredential, serverTrustCredential(*m_challenge));
        else
            completionHandler(AuthenticationChallengeDisposition::RejectProtectionSpace, { });
        return;
    }

    completeAuthenticationChallenge(WTFMove(completionHandler));
}
#endif

void NetworkLoad::didReceiveResponseNetworkSession(ResourceResponse&& response, ResponseCompletionHandler&& completionHandler)
{
    ASSERT(isMainThread());
    ASSERT(!m_throttle);

    if (m_task && m_task->isDownload()) {
        NetworkProcess::singleton().findPendingDownloadLocation(*m_task.get(), WTFMove(completionHandler), response);
        return;
    }

    auto delay = NetworkProcess::singleton().loadThrottleLatency();
    if (delay > 0ms) {
        m_throttle = std::make_unique<Throttle>(*this, delay, WTFMove(response), WTFMove(completionHandler));
        return;
    }

    notifyDidReceiveResponse(WTFMove(response), WTFMove(completionHandler));
}

void NetworkLoad::notifyDidReceiveResponse(ResourceResponse&& response, ResponseCompletionHandler&& completionHandler)
{
    ASSERT(isMainThread());

    if (sharedDidReceiveResponse(WTFMove(response)) == NetworkLoadClient::ShouldContinueDidReceiveResponse::No) {
        m_responseCompletionHandler = WTFMove(completionHandler);
        return;
    }
    completionHandler(PolicyUse);
}

void NetworkLoad::didReceiveData(Ref<SharedBuffer>&& buffer)
{
    ASSERT(!m_throttle);

    // FIXME: This should be the encoded data length, not the decoded data length.
    auto size = buffer->size();
    m_client.get().didReceiveBuffer(WTFMove(buffer), size);
}

void NetworkLoad::didCompleteWithError(const ResourceError& error)
{
    ASSERT(!m_throttle);

    if (error.isNull())
        m_client.get().didFinishLoading(WTF::monotonicallyIncreasingTime());
    else
        m_client.get().didFailLoading(error);
}

void NetworkLoad::throttleDelayCompleted()
{
    ASSERT(m_throttle);

    auto throttle = WTFMove(m_throttle);

    notifyDidReceiveResponse(WTFMove(throttle->response), WTFMove(throttle->responseCompletionHandler));
}

void NetworkLoad::didSendData(uint64_t totalBytesSent, uint64_t totalBytesExpectedToSend)
{
    m_client.get().didSendData(totalBytesSent, totalBytesExpectedToSend);
}

void NetworkLoad::wasBlocked()
{
    m_client.get().didFailLoading(blockedError(m_currentRequest));
}

void NetworkLoad::cannotShowURL()
{
    m_client.get().didFailLoading(cannotShowURLError(m_currentRequest));
}

#else

void NetworkLoad::didReceiveResponseAsync(ResourceHandle* handle, ResourceResponse&& receivedResponse)
{
    ASSERT_UNUSED(handle, handle == m_handle);
    if (sharedDidReceiveResponse(WTFMove(receivedResponse)) == NetworkLoadClient::ShouldContinueDidReceiveResponse::Yes)
        m_handle->continueDidReceiveResponse();
}

void NetworkLoad::didReceiveData(ResourceHandle*, const char* /* data */, unsigned /* length */, int /* encodedDataLength */)
{
    // The NetworkProcess should never get a didReceiveData callback.
    // We should always be using didReceiveBuffer.
    ASSERT_NOT_REACHED();
}

void NetworkLoad::didReceiveBuffer(ResourceHandle* handle, Ref<SharedBuffer>&& buffer, int reportedEncodedDataLength)
{
    ASSERT_UNUSED(handle, handle == m_handle);
    m_client.get().didReceiveBuffer(WTFMove(buffer), reportedEncodedDataLength);
}

void NetworkLoad::didFinishLoading(ResourceHandle* handle, double finishTime)
{
    ASSERT_UNUSED(handle, handle == m_handle);
    m_client.get().didFinishLoading(finishTime);
}

void NetworkLoad::didFail(ResourceHandle* handle, const ResourceError& error)
{
    ASSERT_UNUSED(handle, !handle || handle == m_handle);
    ASSERT(!error.isNull());

    m_client.get().didFailLoading(error);
}

void NetworkLoad::willSendRequestAsync(ResourceHandle* handle, ResourceRequest&& request, ResourceResponse&& redirectResponse)
{
    ASSERT_UNUSED(handle, handle == m_handle);
    sharedWillSendRedirectedRequest(WTFMove(request), WTFMove(redirectResponse));
}

#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
void NetworkLoad::canAuthenticateAgainstProtectionSpaceAsync(ResourceHandle* handle, const ProtectionSpace& protectionSpace)
{
    ASSERT(RunLoop::isMain());
    ASSERT_UNUSED(handle, handle == m_handle);

    // Handle server trust evaluation at platform-level if requested, for performance reasons.
    if (protectionSpace.authenticationScheme() == ProtectionSpaceAuthenticationSchemeServerTrustEvaluationRequested
        && !NetworkProcess::singleton().canHandleHTTPSServerTrustEvaluation()) {
        continueCanAuthenticateAgainstProtectionSpace(false);
        return;
    }

    m_waitingForContinueCanAuthenticateAgainstProtectionSpace = true;
    m_client.get().canAuthenticateAgainstProtectionSpaceAsync(protectionSpace);
}

void NetworkLoad::continueCanAuthenticateAgainstProtectionSpace(bool result)
{
    m_waitingForContinueCanAuthenticateAgainstProtectionSpace = false;
    if (m_handle)
        m_handle->continueCanAuthenticateAgainstProtectionSpace(result);
}
#endif

#if USE(NETWORK_CFDATA_ARRAY_CALLBACK)
bool NetworkLoad::supportsDataArray()
{
    notImplemented();
    return false;
}

void NetworkLoad::didReceiveDataArray(ResourceHandle*, CFArrayRef)
{
    ASSERT_NOT_REACHED();
    notImplemented();
}
#endif

void NetworkLoad::didSendData(ResourceHandle* handle, unsigned long long bytesSent, unsigned long long totalBytesToBeSent)
{
    ASSERT_UNUSED(handle, handle == m_handle);

    m_client.get().didSendData(bytesSent, totalBytesToBeSent);
}

void NetworkLoad::wasBlocked(ResourceHandle* handle)
{
    ASSERT_UNUSED(handle, handle == m_handle);

    didFail(handle, WebKit::blockedError(m_currentRequest));
}

void NetworkLoad::cannotShowURL(ResourceHandle* handle)
{
    ASSERT_UNUSED(handle, handle == m_handle);

    didFail(handle, WebKit::cannotShowURLError(m_currentRequest));
}

bool NetworkLoad::shouldUseCredentialStorage(ResourceHandle* handle)
{
    ASSERT_UNUSED(handle, handle == m_handle || !m_handle); // m_handle will be 0 if called from ResourceHandle::start().

    // When the WebProcess is handling loading a client is consulted each time this shouldUseCredentialStorage question is asked.
    // In NetworkProcess mode we ask the WebProcess client up front once and then reuse the cached answer.

    // We still need this sync version, because ResourceHandle itself uses it internally, even when the delegate uses an async one.

    return m_parameters.allowStoredCredentials == AllowStoredCredentials;
}

void NetworkLoad::didReceiveAuthenticationChallenge(ResourceHandle* handle, const AuthenticationChallenge& challenge)
{
    ASSERT_UNUSED(handle, handle == m_handle);

    if (m_parameters.clientCredentialPolicy == ClientCredentialPolicy::CannotAskClientForCredentials) {
        challenge.authenticationClient()->receivedRequestToContinueWithoutCredential(challenge);
        return;
    }

    NetworkProcess::singleton().authenticationManager().didReceiveAuthenticationChallenge(m_parameters.webPageID, m_parameters.webFrameID, challenge);
}

void NetworkLoad::receivedCancellation(ResourceHandle* handle, const AuthenticationChallenge&)
{
    ASSERT_UNUSED(handle, handle == m_handle);

    m_handle->cancel();
    didFail(m_handle.get(), cancelledError(m_currentRequest));
}
#endif // USE(NETWORK_SESSION)

} // namespace WebKit
