/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller (mueller@kde.org)
    Copyright (C) 2002 Waldo Bastian (bastian@kde.org)
    Copyright (C) 2004-2016 Apple Inc. All rights reserved.
    Copyright (C) 2009 Torch Mobile Inc. http://www.torchmobile.com/

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

    This class provides all functionality needed for loading images, style sheets and html
    pages from the web. It has a memory cache for these objects.
*/

#include "config.h"
#include "CachedResourceLoader.h"

#include "CachedCSSStyleSheet.h"
#include "CachedSVGDocument.h"
#include "CachedFont.h"
#include "CachedImage.h"
#include "CachedRawResource.h"
#include "CachedResourceRequest.h"
#include "CachedSVGFont.h"
#include "CachedScript.h"
#include "CachedXSLStyleSheet.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "ContentExtensionError.h"
#include "ContentExtensionRule.h"
#include "ContentSecurityPolicy.h"
#include "DOMWindow.h"
#include "DiagnosticLoggingClient.h"
#include "DiagnosticLoggingKeys.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "HTMLElement.h"
#include "HTMLFrameOwnerElement.h"
#include "LoaderStrategy.h"
#include "LocalizedStrings.h"
#include "Logging.h"
#include "MainFrame.h"
#include "MemoryCache.h"
#include "Page.h"
#include "Performance.h"
#include "PingLoader.h"
#include "PlatformStrategies.h"
#include "RenderElement.h"
#include "ResourceLoadInfo.h"
#include "RuntimeEnabledFeatures.h"
#include "ScriptController.h"
#include "SecurityOrigin.h"
#include "SessionID.h"
#include "Settings.h"
#include "StyleSheetContents.h"
#include "SubresourceLoader.h"
#include "UserContentController.h"
#include "UserStyleSheet.h"
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

#if ENABLE(VIDEO_TRACK)
#include "CachedTextTrack.h"
#endif

#define PRELOAD_DEBUG 0

#define RELEASE_LOG_IF_ALLOWED(fmt, ...) RELEASE_LOG_IF(isAlwaysOnLoggingAllowed(), Network, "%p - CachedResourceLoader::" fmt, this, ##__VA_ARGS__)

namespace WebCore {

static CachedResource* createResource(CachedResource::Type type, CachedResourceRequest&& request, SessionID sessionID)
{
    switch (type) {
    case CachedResource::ImageResource:
        return new CachedImage(WTFMove(request), sessionID);
    case CachedResource::CSSStyleSheet:
        return new CachedCSSStyleSheet(WTFMove(request), sessionID);
    case CachedResource::Script:
        return new CachedScript(WTFMove(request), sessionID);
    case CachedResource::SVGDocumentResource:
        return new CachedSVGDocument(WTFMove(request), sessionID);
#if ENABLE(SVG_FONTS)
    case CachedResource::SVGFontResource:
        return new CachedSVGFont(WTFMove(request), sessionID);
#endif
    case CachedResource::FontResource:
        return new CachedFont(WTFMove(request), sessionID);
    case CachedResource::MediaResource:
    case CachedResource::RawResource:
    case CachedResource::MainResource:
        return new CachedRawResource(WTFMove(request), type, sessionID);
#if ENABLE(XSLT)
    case CachedResource::XSLStyleSheet:
        return new CachedXSLStyleSheet(WTFMove(request), sessionID);
#endif
#if ENABLE(LINK_PREFETCH)
    case CachedResource::LinkPrefetch:
        return new CachedResource(WTFMove(request), CachedResource::LinkPrefetch, sessionID);
    case CachedResource::LinkSubresource:
        return new CachedResource(WTFMove(request), CachedResource::LinkSubresource, sessionID);
#endif
#if ENABLE(VIDEO_TRACK)
    case CachedResource::TextTrackResource:
        return new CachedTextTrack(WTFMove(request), sessionID);
#endif
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

CachedResourceLoader::CachedResourceLoader(DocumentLoader* documentLoader)
    : m_document(nullptr)
    , m_documentLoader(documentLoader)
    , m_requestCount(0)
    , m_garbageCollectDocumentResourcesTimer(*this, &CachedResourceLoader::garbageCollectDocumentResources)
    , m_autoLoadImages(true)
    , m_imagesEnabled(true)
    , m_allowStaleResources(false)
{
}

CachedResourceLoader::~CachedResourceLoader()
{
    m_documentLoader = nullptr;
    m_document = nullptr;

    clearPreloads();
    for (auto& resource : m_documentResources.values())
        resource->setOwningCachedResourceLoader(nullptr);

    // Make sure no requests still point to this CachedResourceLoader
    ASSERT(m_requestCount == 0);
}

CachedResource* CachedResourceLoader::cachedResource(const String& resourceURL) const 
{
    ASSERT(!resourceURL.isNull());
    URL url = m_document->completeURL(resourceURL);
    return cachedResource(url); 
}

CachedResource* CachedResourceLoader::cachedResource(const URL& resourceURL) const
{
    URL url = MemoryCache::removeFragmentIdentifierIfNeeded(resourceURL);
    return m_documentResources.get(url).get(); 
}

Frame* CachedResourceLoader::frame() const
{
    return m_documentLoader ? m_documentLoader->frame() : nullptr;
}

SessionID CachedResourceLoader::sessionID() const
{
    SessionID sessionID = SessionID::defaultSessionID();

    if (Frame* f = frame())
        sessionID = f->page()->sessionID();

    return sessionID;
}

CachedResourceHandle<CachedImage> CachedResourceLoader::requestImage(CachedResourceRequest&& request)
{
    if (Frame* frame = this->frame()) {
        if (frame->loader().pageDismissalEventBeingDispatched() != FrameLoader::PageDismissalType::None) {
            if (Document* document = frame->document())
                document->contentSecurityPolicy()->upgradeInsecureRequestIfNeeded(request.mutableResourceRequest(), ContentSecurityPolicy::InsecureRequestType::Load);
            URL requestURL = request.resourceRequest().url();
            if (requestURL.isValid() && canRequest(CachedResource::ImageResource, requestURL, request.options(), request.forPreload()))
                PingLoader::loadImage(*frame, requestURL);
            return nullptr;
        }
    }

    request.setDefer(clientDefersImage(request.resourceRequest().url()) ? CachedResourceRequest::DeferredByClient : CachedResourceRequest::NoDefer);
    return downcast<CachedImage>(requestResource(CachedResource::ImageResource, WTFMove(request)).get());
}

CachedResourceHandle<CachedFont> CachedResourceLoader::requestFont(CachedResourceRequest&& request, bool isSVG)
{
#if ENABLE(SVG_FONTS)
    if (isSVG)
        return downcast<CachedSVGFont>(requestResource(CachedResource::SVGFontResource, WTFMove(request)).get());
#else
    UNUSED_PARAM(isSVG);
#endif
    return downcast<CachedFont>(requestResource(CachedResource::FontResource, WTFMove(request)).get());
}

#if ENABLE(VIDEO_TRACK)
CachedResourceHandle<CachedTextTrack> CachedResourceLoader::requestTextTrack(CachedResourceRequest&& request)
{
    return downcast<CachedTextTrack>(requestResource(CachedResource::TextTrackResource, WTFMove(request)).get());
}
#endif

CachedResourceHandle<CachedCSSStyleSheet> CachedResourceLoader::requestCSSStyleSheet(CachedResourceRequest&& request)
{
    return downcast<CachedCSSStyleSheet>(requestResource(CachedResource::CSSStyleSheet, WTFMove(request)).get());
}

CachedResourceHandle<CachedCSSStyleSheet> CachedResourceLoader::requestUserCSSStyleSheet(CachedResourceRequest&& request)
{
    URL url = MemoryCache::removeFragmentIdentifierIfNeeded(request.resourceRequest().url());

#if ENABLE(CACHE_PARTITIONING)
    request.mutableResourceRequest().setDomainForCachePartition(document()->topOrigin()->domainForCachePartition());
#endif

    auto& memoryCache = MemoryCache::singleton();
    if (request.allowsCaching()) {
        if (CachedResource* existing = memoryCache.resourceForRequest(request.resourceRequest(), sessionID())) {
            if (is<CachedCSSStyleSheet>(*existing))
                return downcast<CachedCSSStyleSheet>(existing);
            memoryCache.remove(*existing);
        }
    }

    if (url.string() != request.resourceRequest().url())
        request.mutableResourceRequest().setURL(url);

    CachedResourceHandle<CachedCSSStyleSheet> userSheet = new CachedCSSStyleSheet(WTFMove(request), sessionID());

    if (userSheet->allowsCaching())
        memoryCache.add(*userSheet);
    // FIXME: loadResource calls setOwningCachedResourceLoader() if the resource couldn't be added to cache. Does this function need to call it, too?

    userSheet->load(*this);
    return userSheet;
}

CachedResourceHandle<CachedScript> CachedResourceLoader::requestScript(CachedResourceRequest&& request)
{
    return downcast<CachedScript>(requestResource(CachedResource::Script, WTFMove(request)).get());
}

#if ENABLE(XSLT)
CachedResourceHandle<CachedXSLStyleSheet> CachedResourceLoader::requestXSLStyleSheet(CachedResourceRequest&& request)
{
    return downcast<CachedXSLStyleSheet>(requestResource(CachedResource::XSLStyleSheet, WTFMove(request)).get());
}
#endif

CachedResourceHandle<CachedSVGDocument> CachedResourceLoader::requestSVGDocument(CachedResourceRequest&& request)
{
    return downcast<CachedSVGDocument>(requestResource(CachedResource::SVGDocumentResource, WTFMove(request)).get());
}

#if ENABLE(LINK_PREFETCH)
CachedResourceHandle<CachedResource> CachedResourceLoader::requestLinkResource(CachedResource::Type type, CachedResourceRequest&& request)
{
    ASSERT(frame());
    ASSERT(type == CachedResource::LinkPrefetch || type == CachedResource::LinkSubresource);
    return requestResource(type, WTFMove(request));
}
#endif

CachedResourceHandle<CachedRawResource> CachedResourceLoader::requestMedia(CachedResourceRequest&& request)
{
    return downcast<CachedRawResource>(requestResource(CachedResource::MediaResource, WTFMove(request)).get());
}

CachedResourceHandle<CachedRawResource> CachedResourceLoader::requestRawResource(CachedResourceRequest&& request)
{
    return downcast<CachedRawResource>(requestResource(CachedResource::RawResource, WTFMove(request)).get());
}

CachedResourceHandle<CachedRawResource> CachedResourceLoader::requestMainResource(CachedResourceRequest&& request)
{
    return downcast<CachedRawResource>(requestResource(CachedResource::MainResource, WTFMove(request)).get());
}

static MixedContentChecker::ContentType contentTypeFromResourceType(CachedResource::Type type)
{
    switch (type) {
    // https://w3c.github.io/webappsec-mixed-content/#category-optionally-blockable
    // Editor's Draft, 11 February 2016
    // 3.1. Optionally-blockable Content
    case CachedResource::ImageResource:
    case CachedResource::MediaResource:
            return MixedContentChecker::ContentType::ActiveCanWarn;

    case CachedResource::CSSStyleSheet:
    case CachedResource::Script:
    case CachedResource::FontResource:
        return MixedContentChecker::ContentType::Active;

#if ENABLE(SVG_FONTS)
    case CachedResource::SVGFontResource:
        return MixedContentChecker::ContentType::Active;
#endif

    case CachedResource::RawResource:
    case CachedResource::SVGDocumentResource:
        return MixedContentChecker::ContentType::Active;
#if ENABLE(XSLT)
    case CachedResource::XSLStyleSheet:
        return MixedContentChecker::ContentType::Active;
#endif

#if ENABLE(LINK_PREFETCH)
    case CachedResource::LinkPrefetch:
    case CachedResource::LinkSubresource:
        return MixedContentChecker::ContentType::Active;
#endif

#if ENABLE(VIDEO_TRACK)
    case CachedResource::TextTrackResource:
        return MixedContentChecker::ContentType::Active;
#endif
    default:
        ASSERT_NOT_REACHED();
        return MixedContentChecker::ContentType::Active;
    }
}

bool CachedResourceLoader::checkInsecureContent(CachedResource::Type type, const URL& url) const
{
    switch (type) {
    case CachedResource::Script:
#if ENABLE(XSLT)
    case CachedResource::XSLStyleSheet:
#endif
    case CachedResource::SVGDocumentResource:
    case CachedResource::CSSStyleSheet:
        // These resource can inject script into the current document (Script,
        // XSL) or exfiltrate the content of the current document (CSS).
        if (Frame* f = frame()) {
            if (!f->loader().mixedContentChecker().canRunInsecureContent(m_document->securityOrigin(), url))
                return false;
            Frame& top = f->tree().top();
            if (&top != f && !top.loader().mixedContentChecker().canRunInsecureContent(top.document()->securityOrigin(), url))
                return false;
        }
        break;
#if ENABLE(VIDEO_TRACK)
    case CachedResource::TextTrackResource:
#endif
    case CachedResource::MediaResource:
    case CachedResource::RawResource:
    case CachedResource::ImageResource:
#if ENABLE(SVG_FONTS)
    case CachedResource::SVGFontResource:
#endif
    case CachedResource::FontResource: {
        // These resources can corrupt only the frame's pixels.
        if (Frame* f = frame()) {
            Frame& topFrame = f->tree().top();
            if (!topFrame.loader().mixedContentChecker().canDisplayInsecureContent(topFrame.document()->securityOrigin(), contentTypeFromResourceType(type), url))
                return false;
        }
        break;
    }
    case CachedResource::MainResource:
#if ENABLE(LINK_PREFETCH)
    case CachedResource::LinkPrefetch:
    case CachedResource::LinkSubresource:
        // Prefetch cannot affect the current document.
#endif
        break;
    }
    return true;
}

static inline bool isSameOriginDataURL(const URL& url, const ResourceLoaderOptions& options, bool didReceiveRedirectResponse)
{
    return !didReceiveRedirectResponse && url.protocolIsData() && options.sameOriginDataURLFlag == SameOriginDataURLFlag::Set;
}

bool CachedResourceLoader::allowedByContentSecurityPolicy(CachedResource::Type type, const URL& url, const ResourceLoaderOptions& options, bool didReceiveRedirectResponse)
{
    if (options.contentSecurityPolicyImposition == ContentSecurityPolicyImposition::SkipPolicyCheck)
        return true;

    ASSERT(m_document);
    ASSERT(m_document->contentSecurityPolicy());

    auto redirectResponseReceived = didReceiveRedirectResponse ? ContentSecurityPolicy::RedirectResponseReceived::Yes : ContentSecurityPolicy::RedirectResponseReceived::No;

    switch (type) {
#if ENABLE(XSLT)
    case CachedResource::XSLStyleSheet:
#endif
    case CachedResource::Script:
        if (!m_document->contentSecurityPolicy()->allowScriptFromSource(url, redirectResponseReceived))
            return false;
        break;
    case CachedResource::CSSStyleSheet:
        if (!m_document->contentSecurityPolicy()->allowStyleFromSource(url, redirectResponseReceived))
            return false;
        break;
    case CachedResource::SVGDocumentResource:
    case CachedResource::ImageResource:
        if (!m_document->contentSecurityPolicy()->allowImageFromSource(url, redirectResponseReceived))
            return false;
        break;
#if ENABLE(SVG_FONTS)
    case CachedResource::SVGFontResource:
#endif
    case CachedResource::FontResource:
        if (!m_document->contentSecurityPolicy()->allowFontFromSource(url, redirectResponseReceived))
            return false;
        break;
    case CachedResource::MediaResource:
#if ENABLE(VIDEO_TRACK)
    case CachedResource::TextTrackResource:
#endif
        if (!m_document->contentSecurityPolicy()->allowMediaFromSource(url, redirectResponseReceived))
            return false;
        break;
    case CachedResource::RawResource:
        return true;
    default:
        ASSERT_NOT_REACHED();
    }
    return true;
}

bool CachedResourceLoader::canRequest(CachedResource::Type type, const URL& url, const ResourceLoaderOptions& options, bool forPreload, bool didReceiveRedirectResponse)
{
    if (document() && !document()->securityOrigin()->canDisplay(url)) {
        if (!forPreload)
            FrameLoader::reportLocalLoadFailed(frame(), url.stringCenterEllipsizedToLength());
        LOG(ResourceLoading, "CachedResourceLoader::requestResource URL was not allowed by SecurityOrigin::canDisplay");
        return false;
    }

    // FIXME: Remove same-origin data URL flag since it was removed from fetch spec (see https://github.com/whatwg/fetch/issues/381).
    if (options.mode == FetchOptions::Mode::SameOrigin && !isSameOriginDataURL(url, options, didReceiveRedirectResponse) && !m_document->securityOrigin()->canRequest(url)) {
        printAccessDeniedMessage(url);
        return false;
    }

    if (!allowedByContentSecurityPolicy(type, url, options, didReceiveRedirectResponse))
        return false;

    // SVG Images have unique security rules that prevent all subresource requests except for data urls.
    if (type != CachedResource::MainResource && frame() && frame()->page()) {
        if (frame()->page()->chrome().client().isSVGImageChromeClient() && !url.protocolIsData())
            return false;
    }

    if (!canRequestInContentDispositionAttachmentSandbox(type, url))
        return false;

    // Last of all, check for insecure content. We do this last so that when
    // folks block insecure content with a CSP policy, they don't get a warning.
    // They'll still get a warning in the console about CSP blocking the load.

    // FIXME: Should we consider forPreload here?
    if (!checkInsecureContent(type, url))
        return false;

    return true;
}

bool CachedResourceLoader::canRequestInContentDispositionAttachmentSandbox(CachedResource::Type type, const URL& url) const
{
    Document* document;
    
    // FIXME: Do we want to expand this to all resource types that the mixed content checker would consider active content?
    switch (type) {
    case CachedResource::MainResource:
        if (auto ownerElement = frame() ? frame()->ownerElement() : nullptr) {
            document = &ownerElement->document();
            break;
        }
        return true;
    case CachedResource::CSSStyleSheet:
        document = m_document;
        break;
    default:
        return true;
    }

    if (!document->shouldEnforceContentDispositionAttachmentSandbox() || document->securityOrigin()->canRequest(url))
        return true;

    String message = "Unsafe attempt to load URL " + url.stringCenterEllipsizedToLength() + " from document with Content-Disposition: attachment at URL " + document->url().stringCenterEllipsizedToLength() + ".";
    document->addConsoleMessage(MessageSource::Security, MessageLevel::Error, message);
    return false;
}

bool CachedResourceLoader::shouldContinueAfterNotifyingLoadedFromMemoryCache(const CachedResourceRequest& request, CachedResource* resource)
{
    if (!resource || !frame() || resource->status() != CachedResource::Cached)
        return true;

    ResourceRequest newRequest = ResourceRequest(resource->url());
    if (request.resourceRequest().hiddenFromInspector())
        newRequest.setHiddenFromInspector(true);
    frame()->loader().loadedResourceFromMemoryCache(resource, newRequest);

    // FIXME <http://webkit.org/b/113251>: If the delegate modifies the request's
    // URL, it is no longer appropriate to use this CachedResource.
    return !newRequest.isNull();
}

bool CachedResourceLoader::shouldUpdateCachedResourceWithCurrentRequest(const CachedResource& resource, const CachedResourceRequest& request)
{
    if (resource.type() == CachedResource::Type::FontResource || resource.type() == CachedResource::Type::SVGFontResource) {
        // WebKit is not supporting CORS for fonts (https://bugs.webkit.org/show_bug.cgi?id=86817), no need to update the resource before reusing it.
        return false;
    }

    // FIXME: We should enable resource reuse for these resource types
    switch (resource.type()) {
    case CachedResource::SVGDocumentResource:
        return false;
    case CachedResource::MediaResource:
        return false;
    case CachedResource::RawResource:
        return false;
    case CachedResource::MainResource:
        return false;
#if ENABLE(XSLT)
    case CachedResource::XSLStyleSheet:
        return false;
#endif
#if ENABLE(LINK_PREFETCH)
    case CachedResource::LinkPrefetch:
        return false;
    case CachedResource::LinkSubresource:
        return false;
#endif
    default:
        break;
    }
    return resource.options().mode != request.options().mode || request.resourceRequest().httpOrigin() != resource.resourceRequest().httpOrigin();
}

CachedResourceHandle<CachedResource> CachedResourceLoader::updateCachedResourceWithCurrentRequest(const CachedResource& resource, CachedResourceRequest&& request)
{
    // FIXME: For being loaded requests, we currently do not use the same resource, as this may induce errors in the resource response tainting.
    // We should find a way to improve this.
    if (resource.status() != CachedResource::Cached) {
        request.setCachingPolicy(CachingPolicy::DisallowCaching);
        return loadResource(resource.type(), WTFMove(request));
    }

    auto resourceHandle = createResource(resource.type(), WTFMove(request), sessionID());
    resourceHandle->loadFrom(resource);
    return resourceHandle;
}

static inline void logMemoryCacheResourceRequest(Frame* frame, const String& description, const String& value = String())
{
    if (!frame || !frame->page())
        return;
    if (value.isNull())
        frame->page()->diagnosticLoggingClient().logDiagnosticMessage(DiagnosticLoggingKeys::resourceRequestKey(), description, ShouldSample::Yes);
    else
        frame->page()->diagnosticLoggingClient().logDiagnosticMessageWithValue(DiagnosticLoggingKeys::resourceRequestKey(), description, value, ShouldSample::Yes);
}

static inline String acceptHeaderValueFromType(CachedResource::Type type)
{
    switch (type) {
    case CachedResource::Type::MainResource:
        return ASCIILiteral("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    case CachedResource::Type::ImageResource:
        return ASCIILiteral("image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5");
    case CachedResource::Type::CSSStyleSheet:
        return ASCIILiteral("text/css,*/*;q=0.1");
    case CachedResource::Type::SVGDocumentResource:
        return ASCIILiteral("image/svg+xml");
#if ENABLE(XSLT)
    case CachedResource::Type::XSLStyleSheet:
        // FIXME: This should accept more general xml formats */*+xml, image/svg+xml for example.
        return ASCIILiteral("text/xml,application/xml,application/xhtml+xml,text/xsl,application/rss+xml,application/atom+xml");
#endif
    default:
        return ASCIILiteral("*/*");
    }
}

void CachedResourceLoader::prepareFetch(CachedResource::Type type, CachedResourceRequest& request)
{
    // Implementing step 1 to 7 of https://fetch.spec.whatwg.org/#fetching

    if (!request.origin() && document())
        request.setOrigin(document()->securityOrigin());

    if (!request.resourceRequest().hasHTTPHeader(HTTPHeaderName::Accept))
        request.mutableResourceRequest().setHTTPHeaderField(HTTPHeaderName::Accept, acceptHeaderValueFromType(type));

    // Accept-Language value is handled in underlying port-specific code.
    // FIXME: Decide whether to support client hints
}

CachedResourceHandle<CachedResource> CachedResourceLoader::requestResource(CachedResource::Type type, CachedResourceRequest&& request)
{
    if (Document* document = this->document())
        document->contentSecurityPolicy()->upgradeInsecureRequestIfNeeded(request.mutableResourceRequest(), ContentSecurityPolicy::InsecureRequestType::Load);

    URL url = request.resourceRequest().url();

    LOG(ResourceLoading, "CachedResourceLoader::requestResource '%s', charset '%s', priority=%d, forPreload=%u", url.stringCenterEllipsizedToLength().latin1().data(), request.charset().latin1().data(), request.priority() ? static_cast<int>(request.priority().value()) : -1, request.forPreload());

    // If only the fragment identifiers differ, it is the same resource.
    url = MemoryCache::removeFragmentIdentifierIfNeeded(url);

    if (!url.isValid()) {
        RELEASE_LOG_IF_ALLOWED("requestResource: URL is invalid (frame = %p)", frame());
        return nullptr;
    }

    prepareFetch(type, request);

    if (!canRequest(type, url, request.options(), request.forPreload())) {
        RELEASE_LOG_IF_ALLOWED("requestResource: Not allowed to request resource (frame = %p)", frame());
        return nullptr;
    }

#if ENABLE(CONTENT_EXTENSIONS)
    if (frame() && frame()->mainFrame().page() && m_documentLoader) {
        auto& resourceRequest = request.mutableResourceRequest();
        auto blockedStatus = frame()->mainFrame().page()->userContentProvider().processContentExtensionRulesForLoad(resourceRequest.url(), toResourceType(type), *m_documentLoader);
        applyBlockedStatusToRequest(blockedStatus, resourceRequest);
        if (blockedStatus.blockedLoad) {
            RELEASE_LOG_IF_ALLOWED("requestResource: Resource blocked by content blocker (frame = %p)", frame());
            if (type == CachedResource::Type::MainResource) {
                auto resource = createResource(type, WTFMove(request), sessionID());
                ASSERT(resource);
                resource->error(CachedResource::Status::LoadError);
                resource->setResourceError(ResourceError(ContentExtensions::WebKitContentBlockerDomain, 0, request.resourceRequest().url(), WEB_UI_STRING("The URL was blocked by a content blocker", "WebKitErrorBlockedByContentBlocker description")));
                return resource;
            }
            return nullptr;
        }
        if (blockedStatus.madeHTTPS
            && type == CachedResource::Type::MainResource
            && m_documentLoader->isLoadingMainResource()) {
            // This is to make sure the correct 'new' URL shows in the location bar.
            m_documentLoader->frameLoader()->client().dispatchDidChangeProvisionalURL();
        }
        url = request.resourceRequest().url(); // The content extension could have changed it from http to https.
        url = MemoryCache::removeFragmentIdentifierIfNeeded(url); // Might need to remove fragment identifier again.
    }
#endif

#if ENABLE(WEB_TIMING)
    LoadTiming loadTiming;
    loadTiming.markStartTimeAndFetchStart();
#endif

    auto& memoryCache = MemoryCache::singleton();
    if (request.allowsCaching() && memoryCache.disabled()) {
        DocumentResourceMap::iterator it = m_documentResources.find(url.string());
        if (it != m_documentResources.end()) {
            it->value->setOwningCachedResourceLoader(nullptr);
            m_documentResources.remove(it);
        }
    }

    // See if we can use an existing resource from the cache.
    CachedResourceHandle<CachedResource> resource;
#if ENABLE(CACHE_PARTITIONING)
    if (document())
        request.mutableResourceRequest().setDomainForCachePartition(document()->topOrigin()->domainForCachePartition());
#endif

    if (request.allowsCaching())
        resource = memoryCache.resourceForRequest(request.resourceRequest(), sessionID());

    logMemoryCacheResourceRequest(frame(), resource ? DiagnosticLoggingKeys::inMemoryCacheKey() : DiagnosticLoggingKeys::notInMemoryCacheKey());

    // These 3 fields will be used below after request is moved.
    // FIXME: We can rearrange the code to not require storing all 3 fields.
    auto forPreload = request.forPreload();
    auto defer = request.defer();
    auto priority = request.priority();

    RevalidationPolicy policy = determineRevalidationPolicy(type, request, resource.get());
    switch (policy) {
    case Reload:
        memoryCache.remove(*resource);
        FALLTHROUGH;
    case Load:
        if (resource)
            logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedKey());
        resource = loadResource(type, WTFMove(request));
        break;
    case Revalidate:
        if (resource)
            logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::revalidatingKey());
        resource = revalidateResource(WTFMove(request), *resource);
        break;
    case Use:
        ASSERT(resource);
        if (shouldUpdateCachedResourceWithCurrentRequest(*resource, request)) {
            resource = updateCachedResourceWithCurrentRequest(*resource, WTFMove(request));
            if (resource->status() != CachedResource::Status::Cached)
                policy = Load;
        } else {
            if (!shouldContinueAfterNotifyingLoadedFromMemoryCache(request, resource.get()))
                return nullptr;
            logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::usedKey());
            memoryCache.resourceAccessed(*resource);
#if ENABLE(WEB_TIMING)
            if (document() && RuntimeEnabledFeatures::sharedFeatures().resourceTimingEnabled()) {
                // FIXME (161170): The networkLoadTiming shouldn't be stored on the ResourceResponse.
                resource->response().networkLoadTiming().reset();
                loadTiming.setResponseEnd(monotonicallyIncreasingTime());
                m_resourceTimingInfo.storeResourceTimingInitiatorInformation(resource, request.initiatorName(), frame());
                m_resourceTimingInfo.addResourceTiming(resource.get(), *document(), loadTiming);
            }
#endif
        }
        break;
    }

    if (!resource)
        return nullptr;

    if (!forPreload || policy != Use)
        resource->setLoadPriority(priority);

    if (!forPreload && resource->loader() && resource->resourceRequest().ignoreForRequestCount()) {
        resource->resourceRequest().setIgnoreForRequestCount(false);
        incrementRequestCount(*resource);
    }

    if ((policy != Use || resource->stillNeedsLoad()) && CachedResourceRequest::NoDefer == defer) {
        resource->load(*this);

        // We don't support immediate loads, but we do support immediate failure.
        if (resource->errorOccurred()) {
            if (resource->allowsCaching() && resource->inCache())
                memoryCache.remove(*resource);
            return nullptr;
        }
    }

    if (document() && !document()->loadEventFinished() && !resource->resourceRequest().url().protocolIsData())
        m_validatedURLs.add(resource->resourceRequest().url());

    ASSERT(resource->url() == url.string());
    m_documentResources.set(resource->url(), resource);
    return resource;
}

void CachedResourceLoader::documentDidFinishLoadEvent()
{
    m_validatedURLs.clear();
}

CachedResourceHandle<CachedResource> CachedResourceLoader::revalidateResource(CachedResourceRequest&& request, CachedResource& resource)
{
    ASSERT(resource.inCache());
    auto& memoryCache = MemoryCache::singleton();
    ASSERT(!memoryCache.disabled());
    ASSERT(resource.canUseCacheValidator());
    ASSERT(!resource.resourceToRevalidate());
    ASSERT(resource.sessionID() == sessionID());
    ASSERT(resource.allowsCaching());

#if ENABLE(WEB_TIMING)
    AtomicString initiatorName = request.initiatorName();
#endif
    CachedResourceHandle<CachedResource> newResource = createResource(resource.type(), WTFMove(request), resource.sessionID());

    LOG(ResourceLoading, "Resource %p created to revalidate %p", newResource.get(), &resource);
    newResource->setResourceToRevalidate(&resource);

    memoryCache.remove(resource);
    memoryCache.add(*newResource);
#if ENABLE(WEB_TIMING)
    if (RuntimeEnabledFeatures::sharedFeatures().resourceTimingEnabled())
        m_resourceTimingInfo.storeResourceTimingInitiatorInformation(newResource, initiatorName, frame());
#endif
    return newResource;
}

CachedResourceHandle<CachedResource> CachedResourceLoader::loadResource(CachedResource::Type type, CachedResourceRequest&& request)
{
    auto& memoryCache = MemoryCache::singleton();
    ASSERT(!request.allowsCaching() || !memoryCache.resourceForRequest(request.resourceRequest(), sessionID()));

    LOG(ResourceLoading, "Loading CachedResource for '%s'.", request.resourceRequest().url().stringCenterEllipsizedToLength().latin1().data());

#if ENABLE(WEB_TIMING)
    AtomicString initiatorName = request.initiatorName();
#endif
    CachedResourceHandle<CachedResource> resource = createResource(type, WTFMove(request), sessionID());

    if (resource->allowsCaching() && !memoryCache.add(*resource))
        resource->setOwningCachedResourceLoader(this);
#if ENABLE(WEB_TIMING)
    if (RuntimeEnabledFeatures::sharedFeatures().resourceTimingEnabled())
        m_resourceTimingInfo.storeResourceTimingInitiatorInformation(resource, initiatorName, frame());
#endif
    return resource;
}

static void logRevalidation(const String& reason, DiagnosticLoggingClient& logClient)
{
    logClient.logDiagnosticMessageWithValue(DiagnosticLoggingKeys::cachedResourceRevalidationKey(), DiagnosticLoggingKeys::reasonKey(), reason, ShouldSample::Yes);
}

static void logResourceRevalidationDecision(CachedResource::RevalidationDecision reason, const Frame* frame)
{
    if (!frame || !frame->page())
        return;
    auto& logClient = frame->page()->diagnosticLoggingClient();
    switch (reason) {
    case CachedResource::RevalidationDecision::No:
        break;
    case CachedResource::RevalidationDecision::YesDueToExpired:
        logRevalidation(DiagnosticLoggingKeys::isExpiredKey(), logClient);
        break;
    case CachedResource::RevalidationDecision::YesDueToNoStore:
        logRevalidation(DiagnosticLoggingKeys::noStoreKey(), logClient);
        break;
    case CachedResource::RevalidationDecision::YesDueToNoCache:
        logRevalidation(DiagnosticLoggingKeys::noCacheKey(), logClient);
        break;
    case CachedResource::RevalidationDecision::YesDueToCachePolicy:
        logRevalidation(DiagnosticLoggingKeys::reloadKey(), logClient);
        break;
    }
}

CachedResourceLoader::RevalidationPolicy CachedResourceLoader::determineRevalidationPolicy(CachedResource::Type type, CachedResourceRequest& cachedResourceRequest, CachedResource* existingResource) const
{
    auto& request = cachedResourceRequest.resourceRequest();

    if (!existingResource)
        return Load;

    // We already have a preload going for this URL.
    if (cachedResourceRequest.forPreload() && existingResource->isPreloaded())
        return Use;

    // If the same URL has been loaded as a different type, we need to reload.
    if (existingResource->type() != type) {
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicy reloading due to type mismatch.");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonTypeMismatchKey());
        return Reload;
    }

    if (!existingResource->varyHeaderValuesMatch(request, *this))
        return Reload;

    auto* textDecoder = existingResource->textResourceDecoder();
    if (textDecoder && !textDecoder->hasEqualEncodingForCharset(cachedResourceRequest.charset()))
        return Reload;

    // FIXME: We should use the same cache policy for all resource types. The raw resource policy is overly strict
    //        while the normal subresource policy is too loose.
    if (existingResource->isMainOrMediaOrRawResource() && frame()) {
        bool strictPolicyDisabled = frame()->loader().isStrictRawResourceValidationPolicyDisabledForTesting();
        bool canReuseRawResource = strictPolicyDisabled || downcast<CachedRawResource>(*existingResource).canReuse(request);
        if (!canReuseRawResource)
            return Reload;
    }

    // Conditional requests should have failed canReuse check.
    ASSERT(!request.isConditional());

    // Do not load from cache if images are not enabled. The load for this image will be blocked
    // in CachedImage::load.
    if (cachedResourceRequest.defer() == CachedResourceRequest::DeferredByClient)
        return Reload;

    // Don't reload resources while pasting.
    if (m_allowStaleResources)
        return Use;

    // Always use preloads.
    if (existingResource->isPreloaded())
        return Use;

    // We can find resources that are being validated from cache only when validation is just successfully completing.
    if (existingResource->validationCompleting())
        return Use;
    ASSERT(!existingResource->validationInProgress());

    // Validate the redirect chain.
    bool cachePolicyIsHistoryBuffer = cachePolicy(type) == CachePolicyHistoryBuffer;
    if (!existingResource->redirectChainAllowsReuse(cachePolicyIsHistoryBuffer ? ReuseExpiredRedirection : DoNotReuseExpiredRedirection)) {
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicy reloading due to not cached or expired redirections.");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonRedirectChainKey());
        return Reload;
    }

    // CachePolicyHistoryBuffer uses the cache except if this is a main resource with "cache-control: no-store".
    if (cachePolicyIsHistoryBuffer) {
        // FIXME: Ignoring "cache-control: no-cache" for sub-resources on history navigation but not the main
        // resource is inconsistent. We should probably harmonize this.
        if (!existingResource->response().cacheControlContainsNoStore() || type != CachedResource::MainResource)
            return Use;
    }

    // Don't reuse resources with Cache-control: no-store.
    if (existingResource->response().cacheControlContainsNoStore()) {
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicy reloading due to Cache-control: no-store.");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonNoStoreKey());
        return Reload;
    }

    // If credentials were sent with the previous request and won't be
    // with this one, or vice versa, re-fetch the resource.
    //
    // This helps with the case where the server sends back
    // "Access-Control-Allow-Origin: *" all the time, but some of the
    // client's requests are made without CORS and some with.
    if (existingResource->resourceRequest().allowCookies() != request.allowCookies()) {
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicy reloading due to difference in credentials settings.");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonCredentialSettingsKey());
        return Reload;
    }

    // During the initial load, avoid loading the same resource multiple times for a single document, even if the cache policies would tell us to.
    if (document() && !document()->loadEventFinished() && m_validatedURLs.contains(existingResource->url()))
        return Use;

    // CachePolicyReload always reloads
    if (cachePolicy(type) == CachePolicyReload) {
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicy reloading due to CachePolicyReload.");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonReloadKey());
        return Reload;
    }
    
    // We'll try to reload the resource if it failed last time.
    if (existingResource->errorOccurred()) {
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicye reloading due to resource being in the error state");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonErrorKey());
        return Reload;
    }

    if (existingResource->isLoading()) {
        // Do not use cached main resources that are still loading because sharing
        // loading CachedResources in this case causes issues with regards to cancellation.
        // If one of the DocumentLoader clients decides to cancel the load, then the load
        // would be cancelled for all other DocumentLoaders as well.
        if (type == CachedResource::Type::MainResource)
            return Reload;
        // For cached subresources that are still loading we ignore the cache policy.
        return Use;
    }

    auto revalidationDecision = existingResource->makeRevalidationDecision(cachePolicy(type));
    logResourceRevalidationDecision(revalidationDecision, frame());

    // Check if the cache headers requires us to revalidate (cache expiration for example).
    if (revalidationDecision != CachedResource::RevalidationDecision::No) {
        // See if the resource has usable ETag or Last-modified headers.
        if (existingResource->canUseCacheValidator())
            return Revalidate;
        
        // No, must reload.
        LOG(ResourceLoading, "CachedResourceLoader::determineRevalidationPolicy reloading due to missing cache validators.");
        logMemoryCacheResourceRequest(frame(), DiagnosticLoggingKeys::inMemoryCacheKey(), DiagnosticLoggingKeys::unusedReasonMustRevalidateNoValidatorKey());
        return Reload;
    }

    return Use;
}

void CachedResourceLoader::printAccessDeniedMessage(const URL& url) const
{
    if (url.isNull())
        return;

    if (!frame())
        return;

    String message;
    if (!m_document || m_document->url().isNull())
        message = "Unsafe attempt to load URL " + url.stringCenterEllipsizedToLength() + '.';
    else
        message = "Unsafe attempt to load URL " + url.stringCenterEllipsizedToLength() + " from frame with URL " + m_document->url().stringCenterEllipsizedToLength() + ". Domains, protocols and ports must match.\n";

    frame()->document()->addConsoleMessage(MessageSource::Security, MessageLevel::Error, message);
}

void CachedResourceLoader::setAutoLoadImages(bool enable)
{
    if (enable == m_autoLoadImages)
        return;

    m_autoLoadImages = enable;

    if (!m_autoLoadImages)
        return;

    reloadImagesIfNotDeferred();
}

void CachedResourceLoader::setImagesEnabled(bool enable)
{
    if (enable == m_imagesEnabled)
        return;

    m_imagesEnabled = enable;

    if (!m_imagesEnabled)
        return;

    reloadImagesIfNotDeferred();
}

bool CachedResourceLoader::clientDefersImage(const URL&) const
{
    return !m_imagesEnabled;
}

bool CachedResourceLoader::shouldPerformImageLoad(const URL& url) const
{
    return m_autoLoadImages || url.protocolIsData();
}

bool CachedResourceLoader::shouldDeferImageLoad(const URL& url) const
{
    return clientDefersImage(url) || !shouldPerformImageLoad(url);
}

void CachedResourceLoader::reloadImagesIfNotDeferred()
{
    for (auto& resource : m_documentResources.values()) {
        if (is<CachedImage>(*resource) && resource->stillNeedsLoad() && !clientDefersImage(resource->url()))
            downcast<CachedImage>(*resource).load(*this);
    }
}

CachePolicy CachedResourceLoader::cachePolicy(CachedResource::Type type) const
{
    Frame* frame = this->frame();
    if (!frame)
        return CachePolicyVerify;

    if (type != CachedResource::MainResource)
        return frame->loader().subresourceCachePolicy();

    if (Page* page = frame->page()) {
        if (page->isResourceCachingDisabled())
            return CachePolicyReload;
    }

    switch (frame->loader().loadType()) {
    case FrameLoadType::ReloadFromOrigin:
    case FrameLoadType::Reload:
        return CachePolicyReload;
    case FrameLoadType::Back:
    case FrameLoadType::Forward:
    case FrameLoadType::IndexedBackForward:
        // Do not revalidate cached main resource on back/forward navigation.
        return CachePolicyHistoryBuffer;
    default:
        return CachePolicyVerify;
    }
}

void CachedResourceLoader::removeCachedResource(CachedResource& resource)
{
#ifndef NDEBUG
    DocumentResourceMap::iterator it = m_documentResources.find(resource.url());
    if (it != m_documentResources.end())
        ASSERT(it->value.get() == &resource);
#endif
    m_documentResources.remove(resource.url());
}

void CachedResourceLoader::loadDone(CachedResource* resource, bool shouldPerformPostLoadActions)
{
    RefPtr<DocumentLoader> protectDocumentLoader(m_documentLoader);
    RefPtr<Document> protectDocument(m_document);

#if ENABLE(WEB_TIMING)
    if (resource && document() && RuntimeEnabledFeatures::sharedFeatures().resourceTimingEnabled())
        m_resourceTimingInfo.addResourceTiming(resource, *document(), resource->loader()->loadTiming());
#else
    UNUSED_PARAM(resource);
#endif

    if (frame())
        frame()->loader().loadDone();

    if (shouldPerformPostLoadActions)
        performPostLoadActions();

    if (!m_garbageCollectDocumentResourcesTimer.isActive())
        m_garbageCollectDocumentResourcesTimer.startOneShot(0);
}

// Garbage collecting m_documentResources is a workaround for the
// CachedResourceHandles on the RHS being strong references. Ideally this
// would be a weak map, however CachedResourceHandles perform additional
// bookkeeping on CachedResources, so instead pseudo-GC them -- when the
// reference count reaches 1, m_documentResources is the only reference, so
// remove it from the map.
void CachedResourceLoader::garbageCollectDocumentResources()
{
    typedef Vector<String, 10> StringVector;
    StringVector resourcesToDelete;

    for (auto& resource : m_documentResources) {
        if (resource.value->hasOneHandle()) {
            resourcesToDelete.append(resource.key);
            resource.value->setOwningCachedResourceLoader(nullptr);
        }
    }

    for (auto& resource : resourcesToDelete)
        m_documentResources.remove(resource);
}

void CachedResourceLoader::performPostLoadActions()
{
    checkForPendingPreloads();

    platformStrategies()->loaderStrategy()->servePendingRequests();
}

void CachedResourceLoader::incrementRequestCount(const CachedResource& resource)
{
    if (resource.ignoreForRequestCount())
        return;

    ++m_requestCount;
}

void CachedResourceLoader::decrementRequestCount(const CachedResource& resource)
{
    if (resource.ignoreForRequestCount())
        return;

    --m_requestCount;
    ASSERT(m_requestCount > -1);
}

CachedResourceHandle<CachedResource> CachedResourceLoader::preload(CachedResource::Type type, CachedResourceRequest&& request, PreloadType preloadType)
{
    // We always preload resources on iOS. See <https://bugs.webkit.org/show_bug.cgi?id=91276>.
    // FIXME: We should consider adding a setting to toggle aggressive preloading behavior as opposed
    // to making this behavior specific to iOS.
#if !PLATFORM(IOS)
    bool hasRendering = m_document->bodyOrFrameset() && m_document->renderView();
    bool canBlockParser = type == CachedResource::Script || type == CachedResource::CSSStyleSheet;
    if (!hasRendering && !canBlockParser && preloadType == ImplicitPreload) {
        // Don't preload subresources that can't block the parser before we have something to draw.
        // This helps prevent preloads from delaying first display when bandwidth is limited.
        PendingPreload pendingPreload = { type, WTFMove(request) };
        m_pendingPreloads.append(pendingPreload);
        return nullptr;
    }
#else
    UNUSED_PARAM(preloadType);
#endif
    return requestPreload(type, WTFMove(request));
}

void CachedResourceLoader::checkForPendingPreloads()
{
    if (m_pendingPreloads.isEmpty())
        return;
    auto* body = m_document->bodyOrFrameset();
    if (!body || !body->renderer())
        return;
#if PLATFORM(IOS)
    // We always preload resources on iOS. See <https://bugs.webkit.org/show_bug.cgi?id=91276>.
    // So, we should never have any pending preloads.
    // FIXME: We should look to avoid compiling this code entirely when building for iOS.
    ASSERT_NOT_REACHED();
#endif
    while (!m_pendingPreloads.isEmpty()) {
        PendingPreload preload = m_pendingPreloads.takeFirst();
        // Don't request preload if the resource already loaded normally (this will result in double load if the page is being reloaded with cached results ignored).
        if (!cachedResource(preload.m_request.resourceRequest().url()))
            requestPreload(preload.m_type, WTFMove(preload.m_request));
    }
    m_pendingPreloads.clear();
}

CachedResourceHandle<CachedResource> CachedResourceLoader::requestPreload(CachedResource::Type type, CachedResourceRequest&& request)
{
    if (request.charset().isEmpty() && (type == CachedResource::Script || type == CachedResource::CSSStyleSheet))
        request.setCharset(m_document->charset());
    request.setForPreload(true);

    CachedResourceHandle<CachedResource> resource = requestResource(type, WTFMove(request));
    if (!resource || (m_preloads && m_preloads->contains(resource.get())))
        return nullptr;
    // Fonts need special treatment since just creating the resource doesn't trigger a load.
    if (type == CachedResource::FontResource)
        downcast<CachedFont>(resource.get())->beginLoadIfNeeded(*this);
    resource->increasePreloadCount();

    if (!m_preloads)
        m_preloads = std::make_unique<ListHashSet<CachedResource*>>();
    m_preloads->add(resource.get());

#if PRELOAD_DEBUG
    printf("PRELOADING %s\n",  resource->url().latin1().data());
#endif
    return resource;
}

bool CachedResourceLoader::isPreloaded(const String& urlString) const
{
    const URL& url = m_document->completeURL(urlString);

    if (m_preloads) {
        for (auto& resource : *m_preloads) {
            if (resource->url() == url)
                return true;
        }
    }

    for (auto& pendingPreload : m_pendingPreloads) {
        if (pendingPreload.m_request.resourceRequest().url() == url)
            return true;
    }
    return false;
}

void CachedResourceLoader::clearPreloads()
{
#if PRELOAD_DEBUG
    printPreloadStats();
#endif
    if (!m_preloads)
        return;

    for (auto* resource : *m_preloads) {
        resource->decreasePreloadCount();
        bool deleted = resource->deleteIfPossible();
        if (!deleted && resource->preloadResult() == CachedResource::PreloadNotReferenced)
            MemoryCache::singleton().remove(*resource);
    }
    m_preloads = nullptr;
}

void CachedResourceLoader::clearPendingPreloads()
{
    m_pendingPreloads.clear();
}

#if PRELOAD_DEBUG
void CachedResourceLoader::printPreloadStats()
{
    unsigned scripts = 0;
    unsigned scriptMisses = 0;
    unsigned stylesheets = 0;
    unsigned stylesheetMisses = 0;
    unsigned images = 0;
    unsigned imageMisses = 0;
    for (auto& resource : m_preloads) {
        if (resource->preloadResult() == CachedResource::PreloadNotReferenced)
            printf("!! UNREFERENCED PRELOAD %s\n", resource->url().latin1().data());
        else if (resource->preloadResult() == CachedResource::PreloadReferencedWhileComplete)
            printf("HIT COMPLETE PRELOAD %s\n", resource->url().latin1().data());
        else if (resource->preloadResult() == CachedResource::PreloadReferencedWhileLoading)
            printf("HIT LOADING PRELOAD %s\n", resource->url().latin1().data());

        if (resource->type() == CachedResource::Script) {
            scripts++;
            if (resource->preloadResult() < CachedResource::PreloadReferencedWhileLoading)
                scriptMisses++;
        } else if (resource->type() == CachedResource::CSSStyleSheet) {
            stylesheets++;
            if (resource->preloadResult() < CachedResource::PreloadReferencedWhileLoading)
                stylesheetMisses++;
        } else {
            images++;
            if (resource->preloadResult() < CachedResource::PreloadReferencedWhileLoading)
                imageMisses++;
        }

        if (resource->errorOccurred() && resource->preloadResult() == CachedResource::PreloadNotReferenced)
            MemoryCache::singleton().remove(resource);

        resource->decreasePreloadCount();
    }
    m_preloads = nullptr;

    if (scripts)
        printf("SCRIPTS: %d (%d hits, hit rate %d%%)\n", scripts, scripts - scriptMisses, (scripts - scriptMisses) * 100 / scripts);
    if (stylesheets)
        printf("STYLESHEETS: %d (%d hits, hit rate %d%%)\n", stylesheets, stylesheets - stylesheetMisses, (stylesheets - stylesheetMisses) * 100 / stylesheets);
    if (images)
        printf("IMAGES:  %d (%d hits, hit rate %d%%)\n", images, images - imageMisses, (images - imageMisses) * 100 / images);
}
#endif

const ResourceLoaderOptions& CachedResourceLoader::defaultCachedResourceOptions()
{
    static ResourceLoaderOptions options(SendCallbacks, SniffContent, BufferData, AllowStoredCredentials, ClientCredentialPolicy::MayAskClientForCredentials, FetchOptions::Credentials::Include, DoSecurityCheck, FetchOptions::Mode::NoCors, DoNotIncludeCertificateInfo, ContentSecurityPolicyImposition::DoPolicyCheck, DefersLoadingPolicy::AllowDefersLoading, CachingPolicy::AllowCaching);
    return options;
}

bool CachedResourceLoader::isAlwaysOnLoggingAllowed() const
{
    return m_documentLoader ? m_documentLoader->isAlwaysOnLoggingAllowed() : true;
}

}
