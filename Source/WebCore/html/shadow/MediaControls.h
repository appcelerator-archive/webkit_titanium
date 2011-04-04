/*
 * Copyright (C) 2007, 2008, 2009, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2011 Google Inc. All rights reserved.
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

#ifndef MediaControls_h
#define MediaControls_h

#if ENABLE(VIDEO)

#include "Timer.h"
#include <wtf/RefPtr.h>

namespace WebCore {

class HTMLElement;
class HTMLInputElement;
class HTMLMediaElement;
class Event;
class MediaControlMuteButtonElement;
class MediaControlPlayButtonElement;
class MediaControlSeekButtonElement;
class MediaControlShadowRootElement;
class MediaControlRewindButtonElement;
class MediaControlReturnToRealtimeButtonElement;
class MediaControlToggleClosedCaptionsButtonElement;
class MediaControlTimelineElement;
class MediaControlVolumeSliderElement;
class MediaControlFullscreenButtonElement;
class MediaControlTimeDisplayElement;
class MediaControlStatusDisplayElement;
class MediaControlTimelineContainerElement;
class MediaControlVolumeSliderContainerElement;
class MediaControlElement;
class MediaControlFullscreenVolumeMinButtonElement;
class MediaControlFullscreenVolumeSliderElement;
class MediaControlFullscreenVolumeMaxButtonElement;
class MediaPlayer;

class RenderBox;
class RenderMedia;

class MediaControls {
public:
    MediaControls(HTMLMediaElement*);

    void reset();

    void playbackProgressed();
    void playbackStarted();
    void playbackStopped();

    void changedMute();
    void changedVolume();
    void changedClosedCaptionsVisibility();

    void destroy();
    void update();
    void updateStyle();
    void forwardEvent(Event*);
    void updateTimeDisplay();

    // FIXME: This is temporary to allow RenderMedia::layout tweak the position of controls.
    // Once shadow DOM refactoring is complete, the tweaking will be in MediaControlsShadowRoot and this accessor will no longer be necessary.
    RenderBox* renderBox();

private:
    PassRefPtr<MediaControlShadowRootElement> create(HTMLMediaElement*);

    void updateControlVisibility();
    void changeOpacity(HTMLElement*, float opacity);
    void opacityAnimationTimerFired(Timer<MediaControls>*);

    void updateVolumeSliderContainer(bool visible);

private:
    RefPtr<MediaControlShadowRootElement> m_controlsShadowRoot;
    RefPtr<MediaControlElement> m_panel;
    RefPtr<MediaControlMuteButtonElement> m_muteButton;
    RefPtr<MediaControlPlayButtonElement> m_playButton;
    RefPtr<MediaControlSeekButtonElement> m_seekBackButton;
    RefPtr<MediaControlSeekButtonElement> m_seekForwardButton;
    RefPtr<MediaControlRewindButtonElement> m_rewindButton;
    RefPtr<MediaControlReturnToRealtimeButtonElement> m_returnToRealtimeButton;
    RefPtr<MediaControlToggleClosedCaptionsButtonElement> m_toggleClosedCaptionsButton;
    RefPtr<MediaControlTimelineElement> m_timeline;
    RefPtr<MediaControlVolumeSliderElement> m_volumeSlider;
    RefPtr<MediaControlMuteButtonElement> m_volumeSliderMuteButton;
    RefPtr<MediaControlFullscreenButtonElement> m_fullscreenButton;
    RefPtr<MediaControlTimelineContainerElement> m_timelineContainer;
    RefPtr<MediaControlVolumeSliderContainerElement> m_volumeSliderContainer;
    RefPtr<MediaControlTimeDisplayElement> m_currentTimeDisplay;
    RefPtr<MediaControlTimeDisplayElement> m_timeRemainingDisplay;
    RefPtr<MediaControlStatusDisplayElement> m_statusDisplay;
    RefPtr<MediaControlFullscreenVolumeMinButtonElement> m_fullScreenMinVolumeButton;
    RefPtr<MediaControlFullscreenVolumeSliderElement> m_fullScreenVolumeSlider;
    RefPtr<MediaControlFullscreenVolumeMaxButtonElement> m_fullScreenMaxVolumeButton;

    HTMLMediaElement* m_mediaElement;
    Timer<MediaControls> m_opacityAnimationTimer;

    double m_opacityAnimationStartTime;
    double m_opacityAnimationDuration;
    float m_opacityAnimationFrom;
    float m_opacityAnimationTo;

    bool m_mouseOver;
};


}

#endif
#endif
