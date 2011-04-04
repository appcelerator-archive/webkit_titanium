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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"

#if ENABLE(VIDEO) && USE(AVFOUNDATION)

#include "MediaPlayerPrivateAVFoundation.h"

#include "ApplicationCacheHost.h"
#include "DocumentLoader.h"
#include "FrameView.h"
#include "GraphicsContext.h"
#include "GraphicsLayer.h"
#include "KURL.h"
#include "Logging.h"
#include "SoftLinking.h"
#include "TimeRanges.h"
#include <CoreMedia/CoreMedia.h>
#include <wtf/UnusedParam.h>

using namespace std;

namespace WebCore {

static const float invalidTime = -1.0f;

MediaPlayerPrivateAVFoundation::MediaPlayerPrivateAVFoundation(MediaPlayer* player)
    : m_player(player)
    , m_queuedNotifications()
    , m_queueMutex()
    , m_mainThreadCallPending(false)
    , m_networkState(MediaPlayer::Empty)
    , m_readyState(MediaPlayer::HaveNothing)
    , m_preload(MediaPlayer::Auto)
    , m_scaleFactor(1, 1)
    , m_cachedMaxTimeLoaded(0)
    , m_cachedMaxTimeSeekable(0)
    , m_cachedDuration(invalidTime)
    , m_reportedDuration(invalidTime)
    , m_seekTo(invalidTime)
    , m_requestedRate(1)
    , m_delayCallbacks(false)
    , m_havePreparedToPlay(false)
    , m_assetIsPlayable(false)
    , m_visible(false)
    , m_videoFrameHasDrawn(false)
    , m_loadingMetadata(false)
    , m_delayingLoad(false)
    , m_isAllowedToRender(false)
    , m_cachedHasAudio(false)
    , m_cachedHasVideo(false)
    , m_cachedHasCaptions(false)
    , m_ignoreLoadStateChanges(false)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::MediaPlayerPrivateAVFoundation(%p)", this);
}

MediaPlayerPrivateAVFoundation::~MediaPlayerPrivateAVFoundation()
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::~MediaPlayerPrivateAVFoundation(%p)", this);
    cancelCallOnMainThread(mainThreadCallback, this);
}

MediaPlayerPrivateAVFoundation::MediaRenderingMode MediaPlayerPrivateAVFoundation::currentRenderingMode() const
{
#if USE(ACCELERATED_COMPOSITING)
    if (platformLayer())
        return MediaRenderingToLayer;
#endif

    if (hasContextRenderer())
        return MediaRenderingToContext;

    return MediaRenderingNone;
}

MediaPlayerPrivateAVFoundation::MediaRenderingMode MediaPlayerPrivateAVFoundation::preferredRenderingMode() const
{
    if (!m_player->visible() || !m_player->frameView() || assetStatus() == MediaPlayerAVAssetStatusUnknown)
        return MediaRenderingNone;

#if USE(ACCELERATED_COMPOSITING)
    if (supportsAcceleratedRendering() && m_player->mediaPlayerClient()->mediaPlayerRenderingCanBeAccelerated(m_player))
        return MediaRenderingToLayer;
#endif

    return MediaRenderingToContext;
}

void MediaPlayerPrivateAVFoundation::setUpVideoRendering()
{
    if (!isReadyForVideoSetup())
        return;

    MediaRenderingMode currentMode = currentRenderingMode();
    MediaRenderingMode preferredMode = preferredRenderingMode();
    if (currentMode == preferredMode && currentMode != MediaRenderingNone)
        return;

    LOG(Media, "MediaPlayerPrivateAVFoundation::setUpVideoRendering(%p) - current mode = %d, preferred mode = %d", 
        this, static_cast<int>(currentMode), static_cast<int>(preferredMode));

    if (currentMode != MediaRenderingNone)  
        tearDownVideoRendering();

    switch (preferredMode) {
    case MediaRenderingNone:
    case MediaRenderingToContext:
        createContextVideoRenderer();
        break;
        
#if USE(ACCELERATED_COMPOSITING)
    case MediaRenderingToLayer:
        createVideoLayer();
        break;
#endif
    }

#if USE(ACCELERATED_COMPOSITING)
    // If using a movie layer, inform the client so the compositing tree is updated.
    if (currentMode == MediaRenderingToLayer || preferredMode == MediaRenderingToLayer) {
        LOG(Media, "MediaPlayerPrivateAVFoundation::setUpVideoRendering(%p) - calling mediaPlayerRenderingModeChanged()", this);
        m_player->mediaPlayerClient()->mediaPlayerRenderingModeChanged(m_player);
    }
#endif
}

void MediaPlayerPrivateAVFoundation::tearDownVideoRendering()
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::tearDownVideoRendering(%p)", this);

    destroyContextVideoRenderer();

#if USE(ACCELERATED_COMPOSITING)
    if (platformLayer())
        destroyVideoLayer();
#endif
}

bool MediaPlayerPrivateAVFoundation::hasSetUpVideoRendering() const
{
    return hasLayerRenderer() || hasContextRenderer();
}

void MediaPlayerPrivateAVFoundation::resumeLoad()
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::resumeLoad(%p)", this);

    ASSERT(m_delayingLoad);
    m_delayingLoad = false;

    if (m_assetURL.length())
        prepareToPlay();
}

void MediaPlayerPrivateAVFoundation::load(const String& url)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::load(%p)", this);

    if (m_networkState != MediaPlayer::Loading) {
        m_networkState = MediaPlayer::Loading;
        m_player->networkStateChanged();
    }
    if (m_readyState != MediaPlayer::HaveNothing) {
        m_readyState = MediaPlayer::HaveNothing;
        m_player->readyStateChanged();
    }

    m_videoFrameHasDrawn = false;
    m_assetURL = url;

    // Don't do any more work if the url is empty.
    if (!url.length())
        return;

    if (m_preload == MediaPlayer::None) {
        LOG(Media, "MediaPlayerPrivateAVFoundation::load(%p) - preload==none so returning", this);
        m_delayingLoad = true;
        return;
    }

    prepareToPlay();
}

void MediaPlayerPrivateAVFoundation::playabilityKnown()
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::playabilityKnown(%p)", this);

    updateStates();
    if (m_assetIsPlayable)
        return;

    // Nothing more to do if we already have all of the item's metadata.
    if (assetStatus() > MediaPlayerAVAssetStatusLoading) {
        LOG(Media, "MediaPlayerPrivateAVFoundation::playabilityKnown(%p) - all metadata loaded", this);
        return;
    }

    // At this point we are supposed to load metadata. It is OK to ask the asset to load the same 
    // information multiple times, because if it has already been loaded the completion handler 
    // will just be called synchronously.
    m_loadingMetadata = true;
    beginLoadingMetadata();
}

void MediaPlayerPrivateAVFoundation::prepareToPlay()
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::prepareToPlay(%p)", this);

    m_preload = MediaPlayer::Auto;
    if (m_havePreparedToPlay)
        return;
    m_havePreparedToPlay = true;

    m_delayingLoad = false;
#if ENABLE(OFFLINE_WEB_APPLICATIONS)
    Frame* frame = m_player->frameView() ? m_player->frameView()->frame() : 0;
    ApplicationCacheHost* cacheHost = frame ? frame->loader()->documentLoader()->applicationCacheHost() : 0;
    ApplicationCacheResource* resource = 0;
    if (cacheHost && cacheHost->shouldLoadResourceFromApplicationCache(ResourceRequest(m_assetURL), resource) && resource)
        createAVPlayerForCacheResource(resource);
    else
#endif    
    createAVPlayerForURL(m_assetURL);
    checkPlayability();
}

void MediaPlayerPrivateAVFoundation::paint(GraphicsContext*, const IntRect&)
{
    // This is the base class, only need to remember that a frame has been drawn.
    m_videoFrameHasDrawn = true;
}

float MediaPlayerPrivateAVFoundation::duration() const
{
    if (!metaDataAvailable())
        return 0;

    if (m_cachedDuration == invalidTime) {
        m_cachedDuration = platformDuration();
        LOG(Media, "MediaPlayerPrivateAVFMac::duration(%p) - caching %f", this, m_cachedDuration);
    }

    return m_cachedDuration;
}

void MediaPlayerPrivateAVFoundation::seek(float time)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::seek(%p) - seeking to %f", this, time);
    if (!metaDataAvailable())
        return;

    if (time > duration())
        time = duration();

    m_seekTo = time;

    seekToTime(time);
}

void MediaPlayerPrivateAVFoundation::setRate(float rate)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::setRate(%p) - seting to %f", this, rate);
    m_requestedRate = rate;
}

bool MediaPlayerPrivateAVFoundation::paused() const
{
    if (!metaDataAvailable())
        return true;

    return rate() == 0;
}

bool MediaPlayerPrivateAVFoundation::seeking() const
{
    if (!metaDataAvailable())
        return false;

    return m_seekTo != invalidTime;
}

IntSize MediaPlayerPrivateAVFoundation::naturalSize() const
{
    if (!metaDataAvailable())
        return IntSize();

    // In spite of the name of this method, return the natural size transformed by the 
    // initial movie scale because the spec says intrinsic size is:
    //
    //    ... the dimensions of the resource in CSS pixels after taking into account the resource's 
    //    dimensions, aspect ratio, clean aperture, resolution, and so forth, as defined for the 
    //    format used by the resource

    return m_cachedNaturalSize;
}

void MediaPlayerPrivateAVFoundation::setNaturalSize(IntSize size)
{
    IntSize oldSize = m_cachedNaturalSize;
    m_cachedNaturalSize = size;
    if (oldSize != m_cachedNaturalSize)
        m_player->sizeChanged();
}

PassRefPtr<TimeRanges> MediaPlayerPrivateAVFoundation::buffered() const
{
    if (!m_cachedLoadedTimeRanges)
        m_cachedLoadedTimeRanges = platformBufferedTimeRanges();

    return m_cachedLoadedTimeRanges->copy();
}

float MediaPlayerPrivateAVFoundation::maxTimeSeekable() const
{
    if (!metaDataAvailable())
        return 0;

    if (!m_cachedMaxTimeSeekable)
        m_cachedMaxTimeSeekable = platformMaxTimeSeekable();

    LOG(Media, "MediaPlayerPrivateAVFoundation::maxTimeSeekable(%p) - returning %f", this, m_cachedMaxTimeSeekable);
    return m_cachedMaxTimeSeekable;   
}

float MediaPlayerPrivateAVFoundation::maxTimeLoaded() const
{
    if (!metaDataAvailable())
        return 0;

    if (!m_cachedMaxTimeLoaded)
        m_cachedMaxTimeLoaded = platformMaxTimeLoaded();

    return m_cachedMaxTimeLoaded;   
}

unsigned MediaPlayerPrivateAVFoundation::bytesLoaded() const
{
    float dur = duration();
    if (!dur)
        return 0;
    unsigned loaded = totalBytes() * maxTimeLoaded() / dur;
    LOG(Media, "MediaPlayerPrivateAVFoundation::bytesLoaded(%p) - returning %i", this, loaded);
    return loaded;
}

bool MediaPlayerPrivateAVFoundation::isReadyForVideoSetup() const
{
    return m_isAllowedToRender && m_readyState >= MediaPlayer::HaveMetadata && m_player->visible();
}

void MediaPlayerPrivateAVFoundation::prepareForRendering()
{
    if (m_isAllowedToRender)
        return;
    m_isAllowedToRender = true;

    setUpVideoRendering();

    if (currentRenderingMode() == MediaRenderingToLayer || preferredRenderingMode() == MediaRenderingToLayer)
        m_player->mediaPlayerClient()->mediaPlayerRenderingModeChanged(m_player);
}

bool MediaPlayerPrivateAVFoundation::supportsFullscreen() const
{
#if ENABLE(FULLSCREEN_API)
    return true;
#else
    // FIXME: WebVideoFullscreenController assumes a QTKit/QuickTime media engine
    return false;
#endif
}

void MediaPlayerPrivateAVFoundation::updateStates()
{
    MediaPlayer::NetworkState oldNetworkState = m_networkState;
    MediaPlayer::ReadyState oldReadyState = m_readyState;

    LOG(Media, "MediaPlayerPrivateAVFoundation::updateStates(%p) - entering with networkState = %i, readyState = %i", 
        this, static_cast<int>(m_networkState), static_cast<int>(m_readyState));

    if (m_loadingMetadata)
        m_networkState = MediaPlayer::Loading;
    else {
        // -loadValuesAsynchronouslyForKeys:completionHandler: has invoked its handler; test status of keys and determine state.
        AVAssetStatus avAssetStatus = assetStatus();
        ItemStatus itemStatus = playerItemStatus();
        
        m_assetIsPlayable = (avAssetStatus == MediaPlayerAVAssetStatusPlayable);
        if (m_readyState < MediaPlayer::HaveMetadata && avAssetStatus > MediaPlayerAVAssetStatusLoading) {
            if (m_assetIsPlayable) {
                if (itemStatus == MediaPlayerAVPlayerItemStatusUnknown) {
                    if (avAssetStatus == MediaPlayerAVAssetStatusFailed || m_preload > MediaPlayer::MetaData) {
                        // We may have a playable asset that doesn't support inspection prior to playback; go ahead 
                        // and create the AVPlayerItem now. When the AVPlayerItem becomes ready to play, we will 
                        // have access to its metadata. Or we may have been asked to become ready to play immediately.
                        m_networkState = MediaPlayer::Loading;
                        prepareToPlay();
                    } else
                        m_networkState = MediaPlayer::Idle;
                }
                if (avAssetStatus == MediaPlayerAVAssetStatusLoaded)
                    m_readyState = MediaPlayer::HaveMetadata;
            } else {
                // FIX ME: fetch the error associated with the @"playable" key to distinguish between format 
                // and network errors.
                m_networkState = MediaPlayer::FormatError;
            }
        }
        
        if (avAssetStatus >= MediaPlayerAVAssetStatusLoaded && itemStatus > MediaPlayerAVPlayerItemStatusUnknown) {
            if (seeking())
                m_readyState = m_readyState >= MediaPlayer::HaveMetadata ? MediaPlayer::HaveMetadata : MediaPlayer::HaveNothing;
            else {
                float maxLoaded = maxTimeLoaded();
                switch (itemStatus) {
                case MediaPlayerAVPlayerItemStatusUnknown:
                    break;
                case MediaPlayerAVPlayerItemStatusFailed:
                    m_networkState = MediaPlayer::DecodeError;
                    break;
                case MediaPlayerAVPlayerItemStatusPlaybackLikelyToKeepUp:
                    m_readyState = MediaPlayer::HaveEnoughData;
                    break;
                case MediaPlayerAVPlayerItemStatusReadyToPlay:
                case MediaPlayerAVPlayerItemStatusPlaybackBufferEmpty:
                case MediaPlayerAVPlayerItemStatusPlaybackBufferFull:
                    if (maxLoaded > currentTime())
                        m_readyState = MediaPlayer::HaveFutureData;
                    else
                        m_readyState = MediaPlayer::HaveCurrentData;
                    break;
                }

                if (itemStatus >= MediaPlayerAVPlayerItemStatusReadyToPlay)
                    m_networkState = (maxLoaded == duration()) ? MediaPlayer::Loaded : MediaPlayer::Loading;
            }
        }
    }

    if (isReadyForVideoSetup() && currentRenderingMode() != preferredRenderingMode())
        setUpVideoRendering();

    if (m_networkState != oldNetworkState)
        m_player->networkStateChanged();

    if (m_readyState != oldReadyState)
        m_player->readyStateChanged();

    LOG(Media, "MediaPlayerPrivateAVFoundation::updateStates(%p) - exiting with networkState = %i, readyState = %i", 
        this, static_cast<int>(m_networkState), static_cast<int>(m_readyState));
}

void MediaPlayerPrivateAVFoundation::setSize(const IntSize&) 
{ 
}

void MediaPlayerPrivateAVFoundation::setVisible(bool visible)
{
    if (m_visible == visible)
        return;

    m_visible = visible;
    if (visible)
        setUpVideoRendering();
    else
        tearDownVideoRendering();
}

bool MediaPlayerPrivateAVFoundation::hasAvailableVideoFrame() const
{
    if (currentRenderingMode() == MediaRenderingToLayer)
        return videoLayerIsReadyToDisplay();

    // When using the software renderer we hope someone will signal that a frame is available so we might as well
    // wait until we know that a frame has been drawn.
    return m_videoFrameHasDrawn;
}

void MediaPlayerPrivateAVFoundation::acceleratedRenderingStateChanged()
{
    // Set up or change the rendering path if necessary.
    setUpVideoRendering();
}

void MediaPlayerPrivateAVFoundation::metadataLoaded()
{
    m_loadingMetadata = false;
    updateStates();
}

void MediaPlayerPrivateAVFoundation::loadStateChanged()
{
    if (m_ignoreLoadStateChanges)
        return;
    updateStates();
}

void MediaPlayerPrivateAVFoundation::rateChanged()
{
    updateStates();
    m_player->rateChanged();
}

void MediaPlayerPrivateAVFoundation::loadedTimeRangesChanged()
{
    m_cachedLoadedTimeRanges = 0;
    m_cachedMaxTimeLoaded = 0;
    updateStates();

    // For some media files, reported duration is estimated and updated as media is loaded
    // so report duration changed when the estimate is upated.
    float dur = duration();
    if (dur != m_reportedDuration) {
        if (m_reportedDuration != invalidTime)
            m_player->durationChanged();
        m_reportedDuration = dur;
    }
}

void MediaPlayerPrivateAVFoundation::seekableTimeRangesChanged()
{
    m_cachedMaxTimeSeekable = 0;
}

void MediaPlayerPrivateAVFoundation::timeChanged(double time)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::timeChanged(%p) - time = %f", this, time);

    if (m_seekTo == invalidTime)
        return;

    // AVFoundation may call our observer more than once during a seek, and we can't currently tell
    // if we will be able to seek to an exact time, so assume that we are done seeking if we are
    // "close enough" to the seek time.
    const double smallSeekDelta = 1.0 / 100;

    float currentRate = rate();
    if ((currentRate > 0 && time >= m_seekTo) || (currentRate < 0 && time <= m_seekTo) || (abs(m_seekTo - time) <= smallSeekDelta)) {
        m_seekTo = invalidTime;
        updateStates();
        m_player->timeChanged();
    }
}

void MediaPlayerPrivateAVFoundation::didEnd()
{
    // Hang onto the current time and use it as duration from now on since we are definitely at
    // the end of the movie. Do this because the initial duration is sometimes an estimate.
    float now = currentTime();
    if (now > 0)
        m_cachedDuration = now;

    updateStates();
    m_player->timeChanged();
}

void MediaPlayerPrivateAVFoundation::repaint()
{
    m_videoFrameHasDrawn = true;
    m_player->repaint();
}

MediaPlayer::MovieLoadType MediaPlayerPrivateAVFoundation::movieLoadType() const
{
    if (!metaDataAvailable() || assetStatus() == MediaPlayerAVAssetStatusUnknown)
        return MediaPlayer::Unknown;

    if (isinf(duration()))
        return MediaPlayer::LiveStream;

    return MediaPlayer::Download;
}

void MediaPlayerPrivateAVFoundation::setPreload(MediaPlayer::Preload preload)
{
    m_preload = preload;
    if (m_delayingLoad && m_preload != MediaPlayer::None)
        resumeLoad();
}

void MediaPlayerPrivateAVFoundation::setDelayCallbacks(bool delay)
{
    MutexLocker lock(m_queueMutex);
    if (delay)
        ++m_delayCallbacks;
    else {
        ASSERT(m_delayCallbacks);
        --m_delayCallbacks;
    }
}

void MediaPlayerPrivateAVFoundation::mainThreadCallback(void* context)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::mainThreadCallback(%p)", context);
    MediaPlayerPrivateAVFoundation* player = static_cast<MediaPlayerPrivateAVFoundation*>(context);
    player->clearMainThreadPendingFlag();
    player->dispatchNotification();
}

void MediaPlayerPrivateAVFoundation::clearMainThreadPendingFlag()
{
    MutexLocker lock(m_queueMutex);
    m_mainThreadCallPending = false;
}

void MediaPlayerPrivateAVFoundation::scheduleMainThreadNotification(Notification::Type type, double time)
{
    LOG(Media, "MediaPlayerPrivateAVFoundation::scheduleMainThreadNotification(%p) - notification %d", this, static_cast<int>(type));
    m_queueMutex.lock();

    // It is important to always process the properties in the order that we are notified, 
    // so always go through the queue because notifications happen on different threads.
    m_queuedNotifications.append(Notification(type, time));

    bool delayDispatch = m_delayCallbacks || !isMainThread();
    if (delayDispatch && !m_mainThreadCallPending) {
        m_mainThreadCallPending = true;
        callOnMainThread(mainThreadCallback, this);
    }

    m_queueMutex.unlock();

    if (delayDispatch) {
        LOG(Media, "MediaPlayerPrivateAVFoundation::scheduleMainThreadNotification(%p) - early return", this);
        return;
    }

    dispatchNotification();
}

void MediaPlayerPrivateAVFoundation::dispatchNotification()
{
    ASSERT(isMainThread());

    Notification notification = Notification();
    {
        MutexLocker lock(m_queueMutex);
        
        if (m_queuedNotifications.isEmpty())
            return;
        
        if (!m_delayCallbacks) {
            // Only dispatch one notification callback per invocation because they can cause recursion.
            notification = m_queuedNotifications.first();
            m_queuedNotifications.remove(0);
        }
        
        if (!m_queuedNotifications.isEmpty() && !m_mainThreadCallPending)
            callOnMainThread(mainThreadCallback, this);
        
        if (!notification.isValid())
            return;
    }

    LOG(Media, "MediaPlayerPrivateAVFoundation::dispatchNotification(%p) - dispatching %d", this, static_cast<int>(notification.type()));

    switch (notification.type()) {
    case Notification::ItemDidPlayToEndTime:
        didEnd();
        break;
    case Notification::ItemTracksChanged:
        tracksChanged();
        break;
    case Notification::ItemStatusChanged:
        loadStateChanged();
        break;
    case Notification::ItemSeekableTimeRangesChanged:
        seekableTimeRangesChanged();
        loadStateChanged();
        break;
    case Notification::ItemLoadedTimeRangesChanged:
        loadedTimeRangesChanged();
        loadStateChanged();
        break;
    case Notification::ItemPresentationSizeChanged:
        sizeChanged();
        break;
    case Notification::ItemIsPlaybackLikelyToKeepUpChanged:
        loadStateChanged();
        break;
    case Notification::ItemIsPlaybackBufferEmptyChanged:
        loadStateChanged();
        break;
    case Notification::ItemIsPlaybackBufferFullChanged:
        loadStateChanged();
        break;
    case Notification::PlayerRateChanged:
        rateChanged();
        break;
    case Notification::PlayerTimeChanged:
        timeChanged(notification.time());
        break;
    case Notification::AssetMetadataLoaded:
        metadataLoaded();
        break;
    case Notification::AssetPlayabilityKnown:
        playabilityKnown();
        break;
    case Notification::None:
        ASSERT_NOT_REACHED();
        break;
    }
}

} // namespace WebCore

#endif
