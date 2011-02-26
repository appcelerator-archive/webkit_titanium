/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
#include "DrawingAreaProxyImpl.h"

#include "DrawingAreaMessages.h"
#include "DrawingAreaProxyMessages.h"
#include "LayerTreeContext.h"
#include "Region.h"
#include "UpdateInfo.h"
#include "WebPageProxy.h"
#include "WebProcessProxy.h"

#if !PLATFORM(MAC) && !PLATFORM(WIN)
#error "This drawing area is not ready for use by other ports yet."
#endif

using namespace WebCore;

namespace WebKit {

PassOwnPtr<DrawingAreaProxyImpl> DrawingAreaProxyImpl::create(WebPageProxy* webPageProxy)
{
    return adoptPtr(new DrawingAreaProxyImpl(webPageProxy));
}

DrawingAreaProxyImpl::DrawingAreaProxyImpl(WebPageProxy* webPageProxy)
    : DrawingAreaProxy(DrawingAreaTypeImpl, webPageProxy)
    , m_currentStateID(0)
    , m_requestedStateID(0)
    , m_isWaitingForDidUpdateState(false)
{
}

DrawingAreaProxyImpl::~DrawingAreaProxyImpl()
{
    // Make sure to exit accelerated compositing mode.
    if (isInAcceleratedCompositingMode())
        exitAcceleratedCompositingMode();
}

void DrawingAreaProxyImpl::paint(BackingStore::PlatformGraphicsContext context, const IntRect& rect, Region& unpaintedRegion)
{
    unpaintedRegion = rect;

    if (!m_backingStore)
        return;

    ASSERT(!isInAcceleratedCompositingMode());

    if (m_isWaitingForDidUpdateState) {
        // Wait for a DidUpdateState message that contains the new bits before we paint
        // what's currently in the backing store.
        waitForAndDispatchDidUpdateState();

        // Dispatching DidUpdateState could destroy our backing store or change the compositing mode.
        if (!m_backingStore || isInAcceleratedCompositingMode())
            return;
    }

    m_backingStore->paint(context, rect);
    unpaintedRegion.subtract(IntRect(IntPoint(), m_backingStore->size()));
}

void DrawingAreaProxyImpl::didReceiveMessage(CoreIPC::Connection*, CoreIPC::MessageID, CoreIPC::ArgumentDecoder*)
{
    ASSERT_NOT_REACHED();
}

void DrawingAreaProxyImpl::didReceiveSyncMessage(CoreIPC::Connection*, CoreIPC::MessageID, CoreIPC::ArgumentDecoder*, CoreIPC::ArgumentEncoder*)
{
    ASSERT_NOT_REACHED();
}

bool DrawingAreaProxyImpl::paint(const WebCore::IntRect&, PlatformDrawingContext)
{
    ASSERT_NOT_REACHED();
    return false;
}

void DrawingAreaProxyImpl::sizeDidChange()
{
    sendUpdateState();
}

void DrawingAreaProxyImpl::visibilityDidChange()
{
    if (!m_webPageProxy->isViewVisible()) {
        // Suspend painting.
        m_webPageProxy->process()->send(Messages::DrawingArea::SuspendPainting(), m_webPageProxy->pageID());
        return;
    }

    // Resume painting.
    m_webPageProxy->process()->send(Messages::DrawingArea::ResumePainting(), m_webPageProxy->pageID());
}

void DrawingAreaProxyImpl::setPageIsVisible(bool)
{
}

void DrawingAreaProxyImpl::update(uint64_t stateID, const UpdateInfo& updateInfo)
{
    ASSERT_ARG(stateID, stateID <= m_currentStateID);
    if (stateID < m_currentStateID)
        return;

    // FIXME: Handle the case where the view is hidden.

    incorporateUpdate(updateInfo);
    m_webPageProxy->process()->send(Messages::DrawingArea::DidUpdate(), m_webPageProxy->pageID());
}

void DrawingAreaProxyImpl::didUpdateState(uint64_t stateID, const UpdateInfo& updateInfo, const LayerTreeContext& layerTreeContext)
{
    ASSERT_ARG(stateID, stateID <= m_requestedStateID);
    ASSERT_ARG(stateID, stateID > m_currentStateID);
    m_currentStateID = stateID;

    ASSERT(m_isWaitingForDidUpdateState);
    m_isWaitingForDidUpdateState = false;

    if (m_size != updateInfo.viewSize)
        sendUpdateState();

    if (layerTreeContext != m_layerTreeContext) {
        if (!m_layerTreeContext.isEmpty()) {
            exitAcceleratedCompositingMode();
            ASSERT(m_layerTreeContext.isEmpty());
        }

        if (!layerTreeContext.isEmpty()) {
            enterAcceleratedCompositingMode(layerTreeContext);
            ASSERT(layerTreeContext == m_layerTreeContext);
        }            
    }

    if (isInAcceleratedCompositingMode()) {
        ASSERT(!m_backingStore);
        return;
    }

    m_backingStore = nullptr;
    incorporateUpdate(updateInfo);
}

void DrawingAreaProxyImpl::enterAcceleratedCompositingMode(uint64_t stateID, const LayerTreeContext& layerTreeContext)
{
    ASSERT_ARG(stateID, stateID <= m_currentStateID);
    if (stateID < m_currentStateID)
        return;

    enterAcceleratedCompositingMode(layerTreeContext);
}

void DrawingAreaProxyImpl::exitAcceleratedCompositingMode(uint64_t stateID, const UpdateInfo& updateInfo)
{
    ASSERT_ARG(stateID, stateID <= m_currentStateID);
    if (stateID < m_currentStateID)
        return;

    exitAcceleratedCompositingMode();

    incorporateUpdate(updateInfo);
}

void DrawingAreaProxyImpl::incorporateUpdate(const UpdateInfo& updateInfo)
{
    ASSERT(!isInAcceleratedCompositingMode());

    if (updateInfo.updateRectBounds.isEmpty())
        return;

    if (!m_backingStore)
        m_backingStore = BackingStore::create(updateInfo.viewSize, m_webPageProxy);

    m_backingStore->incorporateUpdate(updateInfo);

    bool shouldScroll = !updateInfo.scrollRect.isEmpty();

    if (shouldScroll)
        m_webPageProxy->scrollView(updateInfo.scrollRect, updateInfo.scrollOffset);

    for (size_t i = 0; i < updateInfo.updateRects.size(); ++i)
        m_webPageProxy->setViewNeedsDisplay(updateInfo.updateRects[i]);

    if (WebPageProxy::debugPaintFlags() & kWKDebugFlashBackingStoreUpdates)
        m_webPageProxy->flashBackingStoreUpdates(updateInfo.updateRects);

    if (shouldScroll)
        m_webPageProxy->displayView();
}

void DrawingAreaProxyImpl::sendUpdateState()
{
    if (!m_webPageProxy->isValid())
        return;

    if (m_isWaitingForDidUpdateState)
        return;

    m_isWaitingForDidUpdateState = true;
    m_webPageProxy->process()->send(Messages::DrawingArea::UpdateState(++m_requestedStateID, m_size, m_scrollOffset), m_webPageProxy->pageID());
    m_scrollOffset = IntSize();

    if (!m_layerTreeContext.isEmpty()) {
        // Wait for the DidUpdateState message. Normally we don this in DrawingAreaProxyImpl::paint, but that
        // function is never called when in accelerated compositing mode.
        waitForAndDispatchDidUpdateState();
    }
}

void DrawingAreaProxyImpl::waitForAndDispatchDidUpdateState()
{
    ASSERT(m_isWaitingForDidUpdateState);

    if (!m_webPageProxy->isValid())
        return;
    if (m_webPageProxy->process()->isLaunching())
        return;
    
    // FIXME: waitForAndDispatchImmediately will always return the oldest DidUpdateState message that
    // hasn't yet been processed. But it might be better to skip ahead to some other DidUpdateState
    // message, if multiple DidUpdateState messages are waiting to be processed. For instance, we could
    // choose the most recent one, or the one that is closest to our current size.

    // The timeout, in seconds, we use when waiting for a DidUpdateState message when we're asked to paint.
    static const double didUpdateStateTimeout = 0.5;
    m_webPageProxy->process()->connection()->waitForAndDispatchImmediately<Messages::DrawingAreaProxy::DidUpdateState>(m_webPageProxy->pageID(), didUpdateStateTimeout);
}

void DrawingAreaProxyImpl::enterAcceleratedCompositingMode(const LayerTreeContext& layerTreeContext)
{
    ASSERT(!isInAcceleratedCompositingMode());

    m_backingStore = nullptr;
    m_layerTreeContext = layerTreeContext;
    m_webPageProxy->enterAcceleratedCompositingMode(layerTreeContext);
}

void DrawingAreaProxyImpl::exitAcceleratedCompositingMode()
{
    ASSERT(isInAcceleratedCompositingMode());

    m_layerTreeContext = LayerTreeContext();    
    m_webPageProxy->exitAcceleratedCompositingMode();
}

} // namespace WebKit
