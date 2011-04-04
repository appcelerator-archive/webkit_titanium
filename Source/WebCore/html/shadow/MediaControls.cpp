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

#include "config.h"

#if ENABLE(VIDEO)
#include "MediaControls.h"

#include "EventNames.h"
#include "FloatConversion.h"
#include "HTMLNames.h"
#include "MediaControlElements.h"
#include "MouseEvent.h"
#include "Page.h"
#include "RenderLayer.h"
#include "RenderTheme.h"
#include <wtf/CurrentTime.h>
#include <wtf/MathExtras.h>


using namespace std;

namespace WebCore {

using namespace HTMLNames;

static const double cOpacityAnimationRepeatDelay = 0.05;

MediaControls::MediaControls(HTMLMediaElement* mediaElement)
    : m_mediaElement(mediaElement)
    , m_opacityAnimationTimer(this, &MediaControls::opacityAnimationTimerFired)
    , m_opacityAnimationStartTime(0)
    , m_opacityAnimationDuration(0)
    , m_opacityAnimationFrom(0)
    , m_opacityAnimationTo(1.0f)
    , m_mouseOver(false)
{
}

// FIXME: This will turn into the standard element factory method once shadow DOM conversion is complete.
// (see https://bugs.webkit.org/show_bug.cgi?id=53020)
PassRefPtr<MediaControlShadowRootElement> MediaControls::create(HTMLMediaElement* mediaElement)
{
    ASSERT(!m_panel);
    ASSERT(!m_muteButton);
    ASSERT(!m_playButton);
    ASSERT(!m_returnToRealtimeButton);
    ASSERT(!m_statusDisplay);
    ASSERT(!m_timelineContainer);
    ASSERT(!m_currentTimeDisplay);
    ASSERT(!m_timeline);
    ASSERT(!m_timeRemainingDisplay);
    ASSERT(!m_seekBackButton);
    ASSERT(!m_seekForwardButton);
    ASSERT(!m_toggleClosedCaptionsButton);
    ASSERT(!m_fullscreenButton);
    ASSERT(!m_muteButton);
    ASSERT(!m_volumeSliderContainer);
    ASSERT(!m_volumeSlider);
    ASSERT(!m_volumeSliderMuteButton);
    ASSERT(!m_fullScreenMinVolumeButton);
    ASSERT(!m_fullScreenMaxVolumeButton);
    ASSERT(!m_fullScreenVolumeSlider);

    RefPtr<MediaControlShadowRootElement> controls = MediaControlShadowRootElement::create(mediaElement);

    m_panel = MediaControlPanelElement::create(mediaElement);

    m_rewindButton = MediaControlRewindButtonElement::create(mediaElement);
    m_rewindButton->attachToParent(m_panel.get());

    m_playButton = MediaControlPlayButtonElement::create(mediaElement);
    m_playButton->attachToParent(m_panel.get());

    m_returnToRealtimeButton = MediaControlReturnToRealtimeButtonElement::create(mediaElement);
    m_returnToRealtimeButton->attachToParent(m_panel.get());

    m_statusDisplay = MediaControlStatusDisplayElement::create(mediaElement);
    m_statusDisplay->attachToParent(m_panel.get());

    m_timelineContainer = MediaControlTimelineContainerElement::create(mediaElement);

    m_currentTimeDisplay = MediaControlCurrentTimeDisplayElement::create(mediaElement);
    m_currentTimeDisplay->attachToParent(m_timelineContainer.get());

    m_timeline = MediaControlTimelineElement::create(mediaElement);
    m_timeline->attachToParent(m_timelineContainer.get());

    m_timeRemainingDisplay = MediaControlTimeRemainingDisplayElement::create(mediaElement);
    m_timeRemainingDisplay->attachToParent(m_timelineContainer.get());

    m_timelineContainer->attachToParent(m_panel.get());

    m_seekBackButton = MediaControlSeekBackButtonElement::create(mediaElement);
    m_seekBackButton->attachToParent(m_panel.get());

    m_seekForwardButton = MediaControlSeekForwardButtonElement::create(mediaElement);
    m_seekForwardButton->attachToParent(m_panel.get());

    m_toggleClosedCaptionsButton = MediaControlToggleClosedCaptionsButtonElement::create(mediaElement);
    m_toggleClosedCaptionsButton->attachToParent(m_panel.get());

    m_fullscreenButton = MediaControlFullscreenButtonElement::create(mediaElement);
    m_fullscreenButton->attachToParent(m_panel.get());

    m_muteButton = MediaControlPanelMuteButtonElement::create(mediaElement);
    m_muteButton->attachToParent(m_panel.get());

    m_volumeSliderContainer = MediaControlVolumeSliderContainerElement::create(mediaElement);

    m_volumeSlider = MediaControlVolumeSliderElement::create(mediaElement);
    m_volumeSlider->attachToParent(m_volumeSliderContainer.get());

    m_volumeSliderMuteButton = MediaControlVolumeSliderMuteButtonElement::create(mediaElement);
    m_volumeSliderMuteButton->attachToParent(m_volumeSliderContainer.get());

    m_volumeSliderContainer->attachToParent(m_panel.get());
    
    // FIXME: These controls, and others, should be created dynamically when needed, instead of 
    // always created.  <http://webkit.org/b/57163>
    m_fullScreenMinVolumeButton = MediaControlFullscreenVolumeMinButtonElement::create(mediaElement);
    m_fullScreenMinVolumeButton->attachToParent(m_panel.get());
    
    m_fullScreenVolumeSlider = MediaControlFullscreenVolumeSliderElement::create(mediaElement);
    m_fullScreenVolumeSlider->attachToParent(m_panel.get());
    
    m_fullScreenMaxVolumeButton = MediaControlFullscreenVolumeMaxButtonElement::create(mediaElement);
    m_fullScreenMaxVolumeButton->attachToParent(m_panel.get());
    
    m_panel->attachToParent(controls.get());
    return controls.release();
}

void MediaControls::reset()
{
    update();
}

void MediaControls::playbackProgressed()
{
    if (m_timeline)
        m_timeline->update(false);
    updateTimeDisplay();
}

void MediaControls::playbackStarted()
{
    playbackProgressed();
}

void MediaControls::playbackStopped()
{
    playbackProgressed();
}

void MediaControls::changedMute()
{
    update();
}

void MediaControls::changedVolume()
{
    update();
}

void MediaControls::changedClosedCaptionsVisibility()
{
    update();
}

void MediaControls::updateStyle()
{
    if (!m_controlsShadowRoot)
        return;

    if (m_panel)
        m_panel->updateStyle();
    if (m_muteButton)
        m_muteButton->updateStyle();
    if (m_playButton)
        m_playButton->updateStyle();
    if (m_seekBackButton)
        m_seekBackButton->updateStyle();
    if (m_seekForwardButton)
        m_seekForwardButton->updateStyle();
    if (m_rewindButton)
        m_rewindButton->updateStyle();
    if (m_returnToRealtimeButton)
        m_returnToRealtimeButton->updateStyle();
    if (m_toggleClosedCaptionsButton)
        m_toggleClosedCaptionsButton->updateStyle();
    if (m_statusDisplay)
        m_statusDisplay->updateStyle();
    if (m_timelineContainer)
        m_timelineContainer->updateStyle();
    if (m_timeline)
        m_timeline->updateStyle();
    if (m_fullscreenButton)
        m_fullscreenButton->updateStyle();
    if (m_currentTimeDisplay)
        m_currentTimeDisplay->updateStyle();
    if (m_timeRemainingDisplay)
        m_timeRemainingDisplay->updateStyle();
    if (m_volumeSliderContainer)
        m_volumeSliderContainer->updateStyle();
    if (m_volumeSliderMuteButton)
        m_volumeSliderMuteButton->updateStyle();
    if (m_volumeSlider)
        m_volumeSlider->updateStyle();
    if (m_fullScreenMinVolumeButton)
        m_fullScreenMinVolumeButton->updateStyle();
    if (m_fullScreenVolumeSlider)
        m_fullScreenVolumeSlider->updateStyle();
    if (m_fullScreenMaxVolumeButton)
        m_fullScreenMaxVolumeButton->updateStyle();
}

void MediaControls::destroy()
{
    ASSERT(m_mediaElement->renderer());

    if (m_controlsShadowRoot && m_controlsShadowRoot->renderer()) {

        // detach the panel before removing the shadow renderer to prevent a crash in m_controlsShadowRoot->detach() 
        //  when display: style changes
        m_panel->detach();

        m_mediaElement->renderer()->removeChild(m_controlsShadowRoot->renderer());
        m_controlsShadowRoot->detach();
        m_controlsShadowRoot = 0;
    }
}

void MediaControls::update()
{
    HTMLMediaElement* media = m_mediaElement;
    if (!media->controls() || !media->inActiveDocument()) {
        if (m_controlsShadowRoot) {
            m_controlsShadowRoot->detach();
            m_panel = 0;
            m_muteButton = 0;
            m_playButton = 0;
            m_statusDisplay = 0;
            m_timelineContainer = 0;
            m_timeline = 0;
            m_seekBackButton = 0;
            m_seekForwardButton = 0;
            m_rewindButton = 0;
            m_returnToRealtimeButton = 0;
            m_currentTimeDisplay = 0;
            m_timeRemainingDisplay = 0;
            m_fullscreenButton = 0;
            m_volumeSliderContainer = 0;
            m_volumeSlider = 0;
            m_volumeSliderMuteButton = 0;
            m_controlsShadowRoot = 0;
            m_toggleClosedCaptionsButton = 0;
            m_fullScreenMinVolumeButton = 0;
            m_fullScreenVolumeSlider = 0;
            m_fullScreenMaxVolumeButton = 0;
        }
        m_opacityAnimationTo = 1.0f;
        m_opacityAnimationTimer.stop();
        return;
    }

    if (!m_controlsShadowRoot) {
        m_controlsShadowRoot = create(m_mediaElement);
        m_mediaElement->renderer()->addChild(m_controlsShadowRoot->renderer());
        m_panel->attach();
    }

    if (m_panel) {
        // update() might alter the opacity of the element, especially if we are in the middle
        // of an animation. This is the only element concerned as we animate only this element.
        float opacityBeforeChangingStyle = m_panel->renderer() ? m_panel->renderer()->style()->opacity() : 0;
        m_panel->update();
        changeOpacity(m_panel.get(), opacityBeforeChangingStyle);
    }
    if (m_muteButton)
        m_muteButton->update();
    if (m_playButton)
        m_playButton->update();
    if (m_timelineContainer)
        m_timelineContainer->update();
    if (m_volumeSliderContainer)
        m_volumeSliderContainer->update();
    if (m_timeline)
        m_timeline->update();
    if (m_currentTimeDisplay)
        m_currentTimeDisplay->update();
    if (m_timeRemainingDisplay)
        m_timeRemainingDisplay->update();
    if (m_seekBackButton)
        m_seekBackButton->update();
    if (m_seekForwardButton)
        m_seekForwardButton->update();
    if (m_rewindButton)
        m_rewindButton->update();
    if (m_returnToRealtimeButton)
        m_returnToRealtimeButton->update();
    if (m_toggleClosedCaptionsButton)
        m_toggleClosedCaptionsButton->update();
    if (m_statusDisplay)
        m_statusDisplay->update();
    if (m_fullscreenButton)
        m_fullscreenButton->update();
    if (m_volumeSlider)
        m_volumeSlider->update();
    if (m_volumeSliderMuteButton)
        m_volumeSliderMuteButton->update();
    if (m_fullScreenMinVolumeButton)
        m_fullScreenMinVolumeButton->update();
    if (m_fullScreenVolumeSlider)
        m_fullScreenVolumeSlider->update();
    if (m_fullScreenMaxVolumeButton)
        m_fullScreenMaxVolumeButton->update();
    updateTimeDisplay();
    updateControlVisibility();
}

void MediaControls::updateTimeDisplay()
{
    ASSERT(m_mediaElement->renderer());

    if (!m_currentTimeDisplay || !m_currentTimeDisplay->renderer() || m_currentTimeDisplay->renderer()->style()->display() == NONE || m_mediaElement->renderer()->style()->visibility() != VISIBLE)
        return;

    float now = m_mediaElement->currentTime();
    float duration = m_mediaElement->duration();

    // Allow the theme to format the time
    ExceptionCode ec;
    m_currentTimeDisplay->setInnerText(m_mediaElement->renderer()->theme()->formatMediaControlsCurrentTime(now, duration), ec);
    m_currentTimeDisplay->setCurrentValue(now);
    m_timeRemainingDisplay->setInnerText(m_mediaElement->renderer()->theme()->formatMediaControlsRemainingTime(now, duration), ec);
    m_timeRemainingDisplay->setCurrentValue(now - duration);
}

RenderBox* MediaControls::renderBox()
{
    return m_controlsShadowRoot ? m_controlsShadowRoot->renderBox() : 0;
}

void MediaControls::updateControlVisibility()
{
    if (!m_panel || !m_panel->renderer())
        return;

    // Don't fade for audio controls.
    HTMLMediaElement* media = m_mediaElement;
    if (!media->hasVideo())
        return;

    ASSERT(media->renderer());

    // Don't fade if the media element is not visible
    if (media->renderer()->style()->visibility() != VISIBLE)
        return;
    
    bool shouldHideController = !m_mouseOver && !media->canPlay();

    // Do fading manually, css animations don't work with shadow trees

    float animateFrom = m_panel->renderer()->style()->opacity();
    float animateTo = shouldHideController ? 0.0f : 1.0f;

    if (animateFrom == animateTo)
        return;

    if (m_opacityAnimationTimer.isActive()) {
        if (m_opacityAnimationTo == animateTo)
            return;
        m_opacityAnimationTimer.stop();
    }

    if (animateFrom < animateTo)
        m_opacityAnimationDuration = m_panel->renderer()->theme()->mediaControlsFadeInDuration();
    else
        m_opacityAnimationDuration = m_panel->renderer()->theme()->mediaControlsFadeOutDuration();

    m_opacityAnimationFrom = animateFrom;
    m_opacityAnimationTo = animateTo;

    m_opacityAnimationStartTime = currentTime();
    m_opacityAnimationTimer.startRepeating(cOpacityAnimationRepeatDelay);
}

void MediaControls::changeOpacity(HTMLElement* e, float opacity)
{
    if (!e || !e->renderer() || !e->renderer()->style())
        return;
    RefPtr<RenderStyle> s = RenderStyle::clone(e->renderer()->style());
    s->setOpacity(opacity);
    // z-index can't be auto if opacity is used
    s->setZIndex(0);
    e->renderer()->setStyle(s.release());
}

void MediaControls::opacityAnimationTimerFired(Timer<MediaControls>*)
{
    double time = currentTime() - m_opacityAnimationStartTime;
    if (time >= m_opacityAnimationDuration) {
        time = m_opacityAnimationDuration;
        m_opacityAnimationTimer.stop();
    }
    float opacity = narrowPrecisionToFloat(m_opacityAnimationFrom + (m_opacityAnimationTo - m_opacityAnimationFrom) * time / m_opacityAnimationDuration);
    changeOpacity(m_panel.get(), opacity);
}

void MediaControls::updateVolumeSliderContainer(bool visible)
{
    if (!m_mediaElement->hasAudio() || !m_volumeSliderContainer || !m_volumeSlider)
        return;

    if (visible && !m_volumeSliderContainer->isVisible()) {
        if (!m_muteButton || !m_muteButton->renderer() || !m_muteButton->renderBox())
            return;

        RefPtr<RenderStyle> s = m_volumeSliderContainer->styleForElement();
        m_volumeSliderContainer->setVisible(true);
        m_volumeSliderContainer->update();
        m_volumeSlider->update();
    } else if (!visible && m_volumeSliderContainer->isVisible()) {
        m_volumeSliderContainer->setVisible(false);
        m_volumeSliderContainer->updateStyle();
    }
}

void MediaControls::forwardEvent(Event* event)
{
    ASSERT(m_mediaElement->renderer());

    if (event->isMouseEvent() && m_controlsShadowRoot) {
        MouseEvent* mouseEvent = static_cast<MouseEvent*>(event);
        IntPoint point(mouseEvent->absoluteLocation());

        bool defaultHandled = false;
        if (m_volumeSliderMuteButton && m_volumeSliderMuteButton->hitTest(point)) {
            m_volumeSliderMuteButton->defaultEventHandler(event);
            defaultHandled = event->defaultHandled();
        }

        bool showVolumeSlider = false;
        if (!defaultHandled && m_muteButton && m_muteButton->hitTest(point)) {
            m_muteButton->defaultEventHandler(event);
            if (event->type() != eventNames().mouseoutEvent)
                showVolumeSlider = true;
        }

        if (m_volumeSliderContainer && m_volumeSliderContainer->hitTest(point))
            showVolumeSlider = true;

        if (m_volumeSlider && m_volumeSlider->hitTest(point)) {
            m_volumeSlider->defaultEventHandler(event);
            showVolumeSlider = true;
        }

        updateVolumeSliderContainer(showVolumeSlider);

        if (m_playButton && m_playButton->hitTest(point))
            m_playButton->defaultEventHandler(event);

        if (m_seekBackButton && m_seekBackButton->hitTest(point))
            m_seekBackButton->defaultEventHandler(event);

        if (m_seekForwardButton && m_seekForwardButton->hitTest(point))
            m_seekForwardButton->defaultEventHandler(event);

        if (m_rewindButton && m_rewindButton->hitTest(point))
            m_rewindButton->defaultEventHandler(event);

        if (m_returnToRealtimeButton && m_returnToRealtimeButton->hitTest(point))
            m_returnToRealtimeButton->defaultEventHandler(event);

       if (m_toggleClosedCaptionsButton && m_toggleClosedCaptionsButton->hitTest(point))
            m_toggleClosedCaptionsButton->defaultEventHandler(event);

        if (m_timeline && m_timeline->hitTest(point))
            m_timeline->defaultEventHandler(event);

        if (m_fullscreenButton && m_fullscreenButton->hitTest(point))
            m_fullscreenButton->defaultEventHandler(event);

        if (event->type() == eventNames().mouseoverEvent) {
            m_mouseOver = true;
            updateControlVisibility();
        }
        if (event->type() == eventNames().mouseoutEvent) {
            // When the scrollbar thumb captures mouse events, we should treat the mouse as still being over our renderer if the new target is a descendant
            Node* mouseOverNode = mouseEvent->relatedTarget() ? mouseEvent->relatedTarget()->toNode() : 0;
            RenderObject* mouseOverRenderer = mouseOverNode ? mouseOverNode->renderer() : 0;
            m_mouseOver = mouseOverRenderer && mouseOverRenderer->isDescendantOf(m_mediaElement->renderer());
            updateControlVisibility();
        }
    }
}

}

#endif
