/*
 * Copyright (C) 2012-2016 Apple Inc. All rights reserved.
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

#import "config.h"
#import "RemoteLayerTreeDrawingAreaProxy.h"

#import "Logging.h"
#import "RemoteLayerTreeDrawingAreaProxyMessages.h"
#import "DrawingAreaMessages.h"
#import "RemoteScrollingCoordinatorProxy.h"
#import "RemoteScrollingCoordinatorTransaction.h"
#import "WebPageProxy.h"
#import "WebProcessProxy.h"
#import <QuartzCore/QuartzCore.h>
#import <WebCore/GraphicsContextCG.h>
#import <WebCore/IOSurfacePool.h>
#import <WebCore/MachSendRight.h>
#import <WebCore/WebActionDisablingCALayerDelegate.h>
#import <wtf/SystemTracing.h>

using namespace IPC;
using namespace WebCore;

// FIXME: Mac will need something similar; we should figure out how to share this with DisplayRefreshMonitor without
// breaking WebKit1 behavior or WebKit2-WebKit1 coexistence.
#if PLATFORM(IOS)
@interface WKOneShotDisplayLinkHandler : NSObject {
    WebKit::RemoteLayerTreeDrawingAreaProxy* _drawingAreaProxy;
    CADisplayLink *_displayLink;
}

- (id)initWithDrawingAreaProxy:(WebKit::RemoteLayerTreeDrawingAreaProxy*)drawingAreaProxy;
- (void)displayLinkFired:(CADisplayLink *)sender;
- (void)invalidate;
- (void)schedule;

@end

@implementation WKOneShotDisplayLinkHandler

- (id)initWithDrawingAreaProxy:(WebKit::RemoteLayerTreeDrawingAreaProxy*)drawingAreaProxy
{
    if (self = [super init]) {
        _drawingAreaProxy = drawingAreaProxy;
        // Note that CADisplayLink retains its target (self), so a call to -invalidate is needed on teardown.
        _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(displayLinkFired:)];
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        _displayLink.paused = YES;
    }
    return self;
}

- (void)dealloc
{
    ASSERT(!_displayLink);
    [super dealloc];
}

- (void)displayLinkFired:(CADisplayLink *)sender
{
    ASSERT(isUIThread());
    _drawingAreaProxy->didRefreshDisplay();
}

- (void)invalidate
{
    [_displayLink invalidate];
    _displayLink = nullptr;
}

- (void)schedule
{
    _displayLink.paused = NO;
}

- (void)pause
{
    _displayLink.paused = YES;
}

@end
#endif

namespace WebKit {

RemoteLayerTreeDrawingAreaProxy::RemoteLayerTreeDrawingAreaProxy(WebPageProxy& webPageProxy)
    : DrawingAreaProxy(DrawingAreaTypeRemoteLayerTree, webPageProxy)
    , m_remoteLayerTreeHost(*this)
#if PLATFORM(IOS)
    , m_displayLinkHandler(adoptNS([[WKOneShotDisplayLinkHandler alloc] initWithDrawingAreaProxy:this]))
#endif
{
#if USE(IOSURFACE)
    // We don't want to pool surfaces in the UI process.
    // FIXME: We should do this somewhere else.
    IOSurfacePool::sharedPool().setPoolSize(0);
#endif

    m_webPageProxy.process().addMessageReceiver(Messages::RemoteLayerTreeDrawingAreaProxy::messageReceiverName(), m_webPageProxy.pageID(), *this);

    if (m_webPageProxy.preferences().tiledScrollingIndicatorVisible())
        initializeDebugIndicator();
}

RemoteLayerTreeDrawingAreaProxy::~RemoteLayerTreeDrawingAreaProxy()
{
    m_callbacks.invalidate(CallbackBase::Error::OwnerWasInvalidated);
    m_webPageProxy.process().removeMessageReceiver(Messages::RemoteLayerTreeDrawingAreaProxy::messageReceiverName(), m_webPageProxy.pageID());

#if PLATFORM(IOS)
    [m_displayLinkHandler invalidate];
#endif
}

void RemoteLayerTreeDrawingAreaProxy::sizeDidChange()
{
    if (!m_webPageProxy.isValid())
        return;

    if (m_isWaitingForDidUpdateGeometry)
        return;

    sendUpdateGeometry();
}

void RemoteLayerTreeDrawingAreaProxy::deviceScaleFactorDidChange()
{
    m_webPageProxy.process().send(Messages::DrawingArea::SetDeviceScaleFactor(m_webPageProxy.deviceScaleFactor()), m_webPageProxy.pageID());
}

void RemoteLayerTreeDrawingAreaProxy::didUpdateGeometry()
{
    ASSERT(m_isWaitingForDidUpdateGeometry);

    m_isWaitingForDidUpdateGeometry = false;

    // If the WKView was resized while we were waiting for a DidUpdateGeometry reply from the web process,
    // we need to resend the new size here.
    if (m_lastSentSize != m_size)
        sendUpdateGeometry();
}

void RemoteLayerTreeDrawingAreaProxy::sendUpdateGeometry()
{
    m_lastSentSize = m_size;
    m_webPageProxy.process().send(Messages::DrawingArea::UpdateGeometry(m_size, IntSize(), false, MachSendRight()), m_webPageProxy.pageID());
    m_isWaitingForDidUpdateGeometry = true;
}

void RemoteLayerTreeDrawingAreaProxy::willCommitLayerTree(uint64_t transactionID)
{
    m_pendingLayerTreeTransactionID = transactionID;
}

void RemoteLayerTreeDrawingAreaProxy::commitLayerTree(const RemoteLayerTreeTransaction& layerTreeTransaction, const RemoteScrollingCoordinatorTransaction& scrollingTreeTransaction)
{
    TraceScope tracingScope(RAFCommitLayerTreeStart, RAFCommitLayerTreeEnd);

    LOG(RemoteLayerTree, "%s", layerTreeTransaction.description().data());
    LOG(RemoteLayerTree, "%s", scrollingTreeTransaction.description().data());

    ASSERT(layerTreeTransaction.transactionID() == m_lastVisibleTransactionID + 1);
    m_transactionIDForPendingCACommit = layerTreeTransaction.transactionID();

    if (m_remoteLayerTreeHost.updateLayerTree(layerTreeTransaction)) {
        if (layerTreeTransaction.transactionID() >= m_transactionIDForUnhidingContent)
            m_webPageProxy.setAcceleratedCompositingRootLayer(m_remoteLayerTreeHost.rootLayer());
        else
            m_remoteLayerTreeHost.detachRootLayer();
    }

#if ENABLE(ASYNC_SCROLLING)
    RemoteScrollingCoordinatorProxy::RequestedScrollInfo requestedScrollInfo;
    m_webPageProxy.scrollingCoordinatorProxy()->commitScrollingTreeState(scrollingTreeTransaction, requestedScrollInfo);
#endif

    m_webPageProxy.didCommitLayerTree(layerTreeTransaction);

#if ENABLE(ASYNC_SCROLLING)
#if PLATFORM(IOS)
    if (m_webPageProxy.scrollingCoordinatorProxy()->hasFixedOrSticky()) {
        // If we got a new layer for a fixed or sticky node, its position from the WebProcess is probably stale. We need to re-run the "viewport" changed logic to udpate it with our UI-side state.
        FloatRect customFixedPositionRect = m_webPageProxy.computeCustomFixedPositionRect(m_webPageProxy.unobscuredContentRect(), m_webPageProxy.customFixedPositionRect(), m_webPageProxy.displayedContentScale(), WebPageProxy::UnobscuredRectConstraint::Unconstrained, m_webPageProxy.scrollingCoordinatorProxy()->visualViewportEnabled());
        m_webPageProxy.scrollingCoordinatorProxy()->viewportChangedViaDelegatedScrolling(m_webPageProxy.scrollingCoordinatorProxy()->rootScrollingNodeID(), customFixedPositionRect, m_webPageProxy.displayedContentScale());
    }
#endif

    // Handle requested scroll position updates from the scrolling tree transaction after didCommitLayerTree()
    // has updated the view size based on the content size.
    if (requestedScrollInfo.requestsScrollPositionUpdate)
        m_webPageProxy.requestScroll(requestedScrollInfo.requestedScrollPosition, layerTreeTransaction.scrollOrigin(), requestedScrollInfo.requestIsProgrammaticScroll);
#endif // ENABLE(ASYNC_SCROLLING)

    if (m_debugIndicatorLayerTreeHost) {
        float scale = indicatorScale(layerTreeTransaction.contentsSize());
        bool rootLayerChanged = m_debugIndicatorLayerTreeHost->updateLayerTree(layerTreeTransaction, scale);
        IntPoint scrollPosition;
#if PLATFORM(MAC)
        scrollPosition = layerTreeTransaction.scrollPosition();
#endif
        updateDebugIndicator(layerTreeTransaction.contentsSize(), rootLayerChanged, scale, scrollPosition);
        asLayer(m_debugIndicatorLayerTreeHost->rootLayer()).name = @"Indicator host root";
    }

    m_webPageProxy.layerTreeCommitComplete();

#if PLATFORM(IOS)
    if (std::exchange(m_didUpdateMessageState, NeedsDidUpdate) == MissedCommit)
        didRefreshDisplay();
    [m_displayLinkHandler schedule];
#else
    m_didUpdateMessageState = NeedsDidUpdate;
    didRefreshDisplay();
#endif

    if (auto milestones = layerTreeTransaction.newlyReachedLayoutMilestones())
        m_webPageProxy.didReachLayoutMilestone(milestones);

    for (auto& callbackID : layerTreeTransaction.callbackIDs()) {
        if (auto callback = m_callbacks.take<VoidCallback>(callbackID))
            callback->performCallback();
    }
}

void RemoteLayerTreeDrawingAreaProxy::acceleratedAnimationDidStart(uint64_t layerID, const String& key, double startTime)
{
    m_webPageProxy.process().send(Messages::DrawingArea::AcceleratedAnimationDidStart(layerID, key, startTime), m_webPageProxy.pageID());
}

void RemoteLayerTreeDrawingAreaProxy::acceleratedAnimationDidEnd(uint64_t layerID, const String& key)
{
    m_webPageProxy.process().send(Messages::DrawingArea::AcceleratedAnimationDidEnd(layerID, key), m_webPageProxy.pageID());
}

static const float indicatorInset = 10;

#if PLATFORM(MAC)
void RemoteLayerTreeDrawingAreaProxy::setViewExposedRect(Optional<WebCore::FloatRect> viewExposedRect)
{
    DrawingAreaProxy::setViewExposedRect(viewExposedRect);
    updateDebugIndicatorPosition();
}
#endif

FloatPoint RemoteLayerTreeDrawingAreaProxy::indicatorLocation() const
{
    if (m_webPageProxy.delegatesScrolling()) {
#if PLATFORM(IOS)
        FloatPoint tiledMapLocation = m_webPageProxy.unobscuredContentRect().location();
        tiledMapLocation = tiledMapLocation.expandedTo(m_webPageProxy.exposedContentRect().location() + FloatSize(0, 60));

        float absoluteInset = indicatorInset / m_webPageProxy.displayedContentScale();
        tiledMapLocation += FloatSize(absoluteInset, absoluteInset);
#else
        FloatPoint tiledMapLocation;
        if (viewExposedRect())
            tiledMapLocation = viewExposedRect().value().location();

        tiledMapLocation += FloatSize(indicatorInset, indicatorInset);
        float scale = 1 / m_webPageProxy.pageScaleFactor();
        tiledMapLocation.scale(scale);
#endif
        return tiledMapLocation;
    }
    
    return FloatPoint(indicatorInset, indicatorInset + m_webPageProxy.topContentInset());
}

void RemoteLayerTreeDrawingAreaProxy::updateDebugIndicatorPosition()
{
    if (!m_tileMapHostLayer)
        return;

    [m_tileMapHostLayer setPosition:indicatorLocation()];
}

float RemoteLayerTreeDrawingAreaProxy::indicatorScale(IntSize contentsSize) const
{
    // Pick a good scale.
    IntSize viewSize = m_webPageProxy.viewSize();

    float scale = 1;
    if (!contentsSize.isEmpty()) {
        float widthScale = std::min<float>((viewSize.width() - 2 * indicatorInset) / contentsSize.width(), 0.05);
        scale = std::min(widthScale, static_cast<float>(viewSize.height() - 2 * indicatorInset) / contentsSize.height());
    }
    
    return scale;
}

void RemoteLayerTreeDrawingAreaProxy::updateDebugIndicator()
{
    // FIXME: we should also update live information during scale.
    updateDebugIndicatorPosition();
}

void RemoteLayerTreeDrawingAreaProxy::updateDebugIndicator(IntSize contentsSize, bool rootLayerChanged, float scale, const IntPoint& scrollPosition)
{
    // Make sure we're the last sublayer.
    CALayer *rootLayer = asLayer(m_remoteLayerTreeHost.rootLayer());
    [m_tileMapHostLayer removeFromSuperlayer];
    [rootLayer addSublayer:m_tileMapHostLayer.get()];

    // Pick a good scale.
    IntSize viewSize = m_webPageProxy.viewSize();

    [m_tileMapHostLayer setBounds:FloatRect(FloatPoint(), contentsSize)];
    [m_tileMapHostLayer setPosition:indicatorLocation()];
    [m_tileMapHostLayer setTransform:CATransform3DMakeScale(scale, scale, 1)];

    if (rootLayerChanged) {
        [m_tileMapHostLayer setSublayers:@[]];
        [m_tileMapHostLayer addSublayer:asLayer(m_debugIndicatorLayerTreeHost->rootLayer())];
        [m_tileMapHostLayer addSublayer:m_exposedRectIndicatorLayer.get()];
    }
    
    const float indicatorBorderWidth = 1;
    float counterScaledBorder = indicatorBorderWidth / scale;

    [m_exposedRectIndicatorLayer setBorderWidth:counterScaledBorder];

    if (m_webPageProxy.delegatesScrolling()) {
        FloatRect scaledExposedRect;
#if PLATFORM(IOS)
        scaledExposedRect = m_webPageProxy.exposedContentRect();
#else
        if (viewExposedRect())
            scaledExposedRect = viewExposedRect().value();
        float scale = 1 / m_webPageProxy.pageScaleFactor();
        scaledExposedRect.scale(scale);
#endif
        [m_exposedRectIndicatorLayer setPosition:scaledExposedRect.location()];
        [m_exposedRectIndicatorLayer setBounds:FloatRect(FloatPoint(), scaledExposedRect.size())];
    } else {
        [m_exposedRectIndicatorLayer setPosition:scrollPosition];
        [m_exposedRectIndicatorLayer setBounds:FloatRect(FloatPoint(), viewSize)];
    }
}

void RemoteLayerTreeDrawingAreaProxy::initializeDebugIndicator()
{
    m_debugIndicatorLayerTreeHost = std::make_unique<RemoteLayerTreeHost>(*this);
    m_debugIndicatorLayerTreeHost->setIsDebugLayerTreeHost(true);

    m_tileMapHostLayer = adoptNS([[CALayer alloc] init]);
    [m_tileMapHostLayer setName:@"Tile map host"];
    [m_tileMapHostLayer setDelegate:[WebActionDisablingCALayerDelegate shared]];
    [m_tileMapHostLayer setAnchorPoint:CGPointZero];
    [m_tileMapHostLayer setOpacity:0.8];
    [m_tileMapHostLayer setMasksToBounds:YES];
    [m_tileMapHostLayer setBorderWidth:2];

    CGColorSpaceRef colorSpace = sRGBColorSpaceRef();
    {
        const CGFloat components[] = { 1, 1, 1, 0.6 };
        RetainPtr<CGColorRef> color = adoptCF(CGColorCreate(colorSpace, components));
        [m_tileMapHostLayer setBackgroundColor:color.get()];

        const CGFloat borderComponents[] = { 0, 0, 0, 1 };
        RetainPtr<CGColorRef> borderColor = adoptCF(CGColorCreate(colorSpace, borderComponents));
        [m_tileMapHostLayer setBorderColor:borderColor.get()];
    }
    
    m_exposedRectIndicatorLayer = adoptNS([[CALayer alloc] init]);
    [m_exposedRectIndicatorLayer setDelegate:[WebActionDisablingCALayerDelegate shared]];
    [m_exposedRectIndicatorLayer setAnchorPoint:CGPointZero];

    {
        const CGFloat components[] = { 0, 1, 0, 1 };
        RetainPtr<CGColorRef> color = adoptCF(CGColorCreate(colorSpace, components));
        [m_exposedRectIndicatorLayer setBorderColor:color.get()];
    }
}

void RemoteLayerTreeDrawingAreaProxy::didRefreshDisplay()
{
    if (!m_webPageProxy.isValid())
        return;

    if (m_didUpdateMessageState != NeedsDidUpdate) {
        m_didUpdateMessageState = MissedCommit;
#if PLATFORM(IOS)
        [m_displayLinkHandler pause];
#endif
        return;
    }
    
    m_didUpdateMessageState = DoesNotNeedDidUpdate;

    TraceScope tracingScope(RAFDidRefreshDisplayStart, RAFDidRefreshDisplayEnd);

    // Waiting for CA to commit is insufficient, because the render server can still be
    // using our backing store. We can improve this by waiting for the render server to commit
    // if we find API to do so, but for now we will make extra buffers if need be.
    m_webPageProxy.process().send(Messages::DrawingArea::DidUpdate(), m_webPageProxy.pageID());

    m_lastVisibleTransactionID = m_transactionIDForPendingCACommit;

    m_webPageProxy.didUpdateActivityState();
}

void RemoteLayerTreeDrawingAreaProxy::waitForDidUpdateActivityState()
{
    // We must send the didUpdate message before blocking on the next commit, otherwise
    // we can be guaranteed that the next commit won't come until after the waitForAndDispatchImmediately times out.
    if (m_didUpdateMessageState != DoesNotNeedDidUpdate)
        didRefreshDisplay();

    static Seconds activityStateUpdateTimeout = [] {
        if (id value = [[NSUserDefaults standardUserDefaults] objectForKey:@"WebKitOverrideActivityStateUpdateTimeout"])
            return Seconds([value doubleValue]);

#if PLATFORM(IOS)
        return Seconds::fromMilliseconds(500);
#else
        return Seconds::fromMilliseconds(250);
#endif
    }();
    m_webPageProxy.process().connection()->waitForAndDispatchImmediately<Messages::RemoteLayerTreeDrawingAreaProxy::CommitLayerTree>(m_webPageProxy.pageID(), activityStateUpdateTimeout, IPC::WaitForOption::InterruptWaitingIfSyncMessageArrives);
}

void RemoteLayerTreeDrawingAreaProxy::dispatchAfterEnsuringDrawing(std::function<void (CallbackBase::Error)> callbackFunction)
{
    if (!m_webPageProxy.isValid()) {
        callbackFunction(CallbackBase::Error::OwnerWasInvalidated);
        return;
    }

    m_webPageProxy.process().send(Messages::DrawingArea::AddTransactionCallbackID(m_callbacks.put(WTFMove(callbackFunction), nullptr)), m_webPageProxy.pageID());
}

void RemoteLayerTreeDrawingAreaProxy::hideContentUntilPendingUpdate()
{
    m_transactionIDForUnhidingContent = nextLayerTreeTransactionID();
    m_remoteLayerTreeHost.detachRootLayer();
}

void RemoteLayerTreeDrawingAreaProxy::hideContentUntilAnyUpdate()
{
    m_remoteLayerTreeHost.detachRootLayer();
}

bool RemoteLayerTreeDrawingAreaProxy::hasVisibleContent() const
{
    return m_remoteLayerTreeHost.rootLayer();
}

} // namespace WebKit
