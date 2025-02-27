/*
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEB_RTC)

#include "JSDOMPromise.h"
#include "PeerConnectionStates.h"
#include "RTCDataChannel.h"

namespace WebCore {

class DOMError;
class Event;
class MediaStream;
class MediaStreamTrack;
class PeerConnectionBackend;
class RTCConfiguration;
class RTCDataChannelHandler;
class RTCIceCandidate;
class RTCPeerConnection;
class RTCRtpReceiver;
class RTCRtpSender;
class RTCRtpSenderClient;
class RTCRtpTransceiver;
class RTCSessionDescription;
class RTCStatsResponse;
class ScriptExecutionContext;

struct RTCAnswerOptions;
struct RTCOfferOptions;

namespace PeerConnection {
typedef DOMPromise<RTCSessionDescription> SessionDescriptionPromise;
typedef DOMPromise<void> VoidPromise;
typedef DOMPromise<RTCStatsResponse> StatsPromise;
}

typedef std::unique_ptr<PeerConnectionBackend> (*CreatePeerConnectionBackend)(RTCPeerConnection&);

class PeerConnectionBackend {
public:
    WEBCORE_EXPORT static CreatePeerConnectionBackend create;

    PeerConnectionBackend(RTCPeerConnection& peerConnection) : m_peerConnection(peerConnection) { }
    virtual ~PeerConnectionBackend() { }

    void createOffer(RTCOfferOptions&&, PeerConnection::SessionDescriptionPromise&&);
    void createAnswer(RTCAnswerOptions&&, PeerConnection::SessionDescriptionPromise&&);
    void setLocalDescription(RTCSessionDescription&, PeerConnection::VoidPromise&&);
    void setRemoteDescription(RTCSessionDescription&, PeerConnection::VoidPromise&&);
    void addIceCandidate(RTCIceCandidate&, PeerConnection::VoidPromise&&);

    virtual std::unique_ptr<RTCDataChannelHandler> createDataChannelHandler(const String&, const RTCDataChannelInit&) = 0;

    void stop();

    virtual RefPtr<RTCSessionDescription> localDescription() const = 0;
    virtual RefPtr<RTCSessionDescription> currentLocalDescription() const = 0;
    virtual RefPtr<RTCSessionDescription> pendingLocalDescription() const = 0;

    virtual RefPtr<RTCSessionDescription> remoteDescription() const = 0;
    virtual RefPtr<RTCSessionDescription> currentRemoteDescription() const = 0;
    virtual RefPtr<RTCSessionDescription> pendingRemoteDescription() const = 0;

    virtual void setConfiguration(RTCConfiguration&) = 0;

    virtual void getStats(MediaStreamTrack*, PeerConnection::StatsPromise&&) = 0;

    virtual Vector<RefPtr<MediaStream>> getRemoteStreams() const = 0;

    virtual Ref<RTCRtpReceiver> createReceiver(const String& transceiverMid, const String& trackKind, const String& trackId) = 0;
    virtual void replaceTrack(RTCRtpSender&, RefPtr<MediaStreamTrack>&&, PeerConnection::VoidPromise&&) = 0;

    virtual bool isNegotiationNeeded() const = 0;
    virtual void markAsNeedingNegotiation() = 0;
    virtual void clearNegotiationNeededState() = 0;

    virtual void emulatePlatformEvent(const String& action) = 0;

protected:
    void fireICECandidateEvent(RefPtr<RTCIceCandidate>&&);
    void doneGatheringCandidates();

    void updateSignalingState(PeerConnectionStates::SignalingState);

    void createOfferSucceeded(String&&);
    void createOfferFailed(Exception&&);

    void createAnswerSucceeded(String&&);
    void createAnswerFailed(Exception&&);

    void setLocalDescriptionSucceeded();
    void setLocalDescriptionFailed(Exception&&);

    void setRemoteDescriptionSucceeded();
    void setRemoteDescriptionFailed(Exception&&);

    void addIceCandidateSucceeded();
    void addIceCandidateFailed(Exception&&);

private:
    virtual void doCreateOffer(RTCOfferOptions&&) = 0;
    virtual void doCreateAnswer(RTCAnswerOptions&&) = 0;
    virtual void doSetLocalDescription(RTCSessionDescription&) = 0;
    virtual void doSetRemoteDescription(RTCSessionDescription&) = 0;
    virtual void doAddIceCandidate(RTCIceCandidate&) = 0;
    virtual void doStop() = 0;

protected:
    RTCPeerConnection& m_peerConnection;

private:
    Optional<PeerConnection::SessionDescriptionPromise> m_offerAnswerPromise;
    Optional<PeerConnection::VoidPromise> m_setDescriptionPromise;
    Optional<PeerConnection::VoidPromise> m_addIceCandidatePromise;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
