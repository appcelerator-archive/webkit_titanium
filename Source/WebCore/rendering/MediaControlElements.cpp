/*
 * Copyright (C) 2008, 2009, 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(VIDEO)

#include "MediaControlElements.h"

#include "CSSStyleSelector.h"
#include "EventNames.h"
#include "FloatConversion.h"
#include "Frame.h"
#include "HTMLNames.h"
#include "LocalizedStrings.h"
#include "MediaControls.h"
#include "MouseEvent.h"
#include "Page.h"
#include "RenderMedia.h"
#include "RenderSlider.h"
#include "RenderTheme.h"
#include "Settings.h"

namespace WebCore {

using namespace HTMLNames;

HTMLMediaElement* toParentMediaElement(RenderObject* o)
{
    Node* node = o->node();
    Node* mediaNode = node ? node->shadowAncestorNode() : 0;
    if (!mediaNode || (!mediaNode->hasTagName(HTMLNames::videoTag) && !mediaNode->hasTagName(HTMLNames::audioTag)))
        return 0;

    return static_cast<HTMLMediaElement*>(mediaNode);
}

// FIXME: These constants may need to be tweaked to better match the seeking in the QuickTime plug-in.
static const float cSeekRepeatDelay = 0.1f;
static const float cStepTime = 0.07f;
static const float cSeekTime = 0.2f;

inline MediaControlShadowRootElement::MediaControlShadowRootElement(HTMLMediaElement* mediaElement)
    : HTMLDivElement(divTag, mediaElement->document())
{
    setShadowHost(mediaElement);
}

PassRefPtr<MediaControlShadowRootElement> MediaControlShadowRootElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlShadowRootElement> element = adoptRef(new MediaControlShadowRootElement(mediaElement));

    RefPtr<RenderStyle> rootStyle = RenderStyle::create();
    rootStyle->inheritFrom(mediaElement->renderer()->style());
    rootStyle->setDisplay(BLOCK);
    rootStyle->setPosition(RelativePosition);

    RenderMediaControlShadowRoot* renderer = new (mediaElement->renderer()->renderArena()) RenderMediaControlShadowRoot(element.get());
    renderer->setStyle(rootStyle.release());

    element->setRenderer(renderer);
    element->setAttached();
    element->setInDocument();

    return element.release();
}

void MediaControlShadowRootElement::detach()
{
    HTMLDivElement::detach();
    // FIXME: Remove once shadow DOM uses Element::setShadowRoot().
    setShadowHost(0);
}

// ----------------------------

MediaControlElement::MediaControlElement(HTMLMediaElement* mediaElement)
    : HTMLDivElement(divTag, mediaElement->document())
    , m_mediaElement(mediaElement)
{
    setInDocument();
}

void MediaControlElement::attachToParent(Element* parent)
{
    // FIXME: This code seems very wrong.  Why are we magically adding |this| to the DOM here?
    //        We shouldn't be calling parser API methods outside of the parser!
    parent->parserAddChild(this);
}

void MediaControlElement::update()
{
    if (renderer())
        renderer()->updateFromElement();
    updateStyle();
}

PassRefPtr<RenderStyle> MediaControlElement::styleForElement()
{
    ASSERT(m_mediaElement->renderer());
    RefPtr<RenderStyle> style = document()->styleSelector()->styleForElement(this, m_mediaElement->renderer()->style(), true);
    if (!style)
        return 0;
    
    // text-decoration can't be overrided from CSS. So we do it here.
    // See https://bugs.webkit.org/show_bug.cgi?id=27015
    style->setTextDecoration(TDNONE);
    style->setTextDecorationsInEffect(TDNONE);

    return style;
}

bool MediaControlElement::rendererIsNeeded(RenderStyle* style)
{
    ASSERT(document()->page());

    return HTMLDivElement::rendererIsNeeded(style) && parentNode() && parentNode()->renderer()
        && (!style->hasAppearance() || document()->page()->theme()->shouldRenderMediaControlPart(style->appearance(), m_mediaElement));
}
    
void MediaControlElement::attach()
{
    RefPtr<RenderStyle> style = styleForElement();
    if (!style)
        return;
    bool needsRenderer = rendererIsNeeded(style.get());
    if (!needsRenderer)
        return;
    RenderObject* renderer = createRenderer(m_mediaElement->renderer()->renderArena(), style.get());
    if (!renderer)
        return;
    renderer->setStyle(style.get());
    setRenderer(renderer);
    if (parentNode() && parentNode()->renderer()) {
        // Find next sibling with a renderer to determine where to insert.
        Node* sibling = nextSibling();
        while (sibling && !sibling->renderer())
            sibling = sibling->nextSibling();
        parentNode()->renderer()->addChild(renderer, sibling ? sibling->renderer() : 0);
    }
    ContainerNode::attach();
}

void MediaControlElement::updateStyle()
{
    if (!m_mediaElement || !m_mediaElement->renderer())
        return;

    RefPtr<RenderStyle> style = styleForElement();
    if (!style)
        return;

    bool needsRenderer = rendererIsNeeded(style.get()) && parentNode() && parentNode()->renderer();
    if (renderer() && !needsRenderer)
        detach();
    else if (!renderer() && needsRenderer)
        attach();
    else if (renderer()) {
        renderer()->setStyle(style.get());

        // Make sure that if there is any innerText renderer, it is updated as well.
        if (firstChild() && firstChild()->renderer())
            firstChild()->renderer()->setStyle(style.get());
    }
}

// ----------------------------

inline MediaControlPanelElement::MediaControlPanelElement(HTMLMediaElement* mediaElement)
    : MediaControlElement(mediaElement)
{
}

PassRefPtr<MediaControlPanelElement> MediaControlPanelElement::create(HTMLMediaElement* mediaElement)
{
    return adoptRef(new MediaControlPanelElement(mediaElement));
}

MediaControlElementType MediaControlPanelElement::displayType() const
{
    return MediaControlsPanel;
}

const AtomicString& MediaControlPanelElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-panel"));
    return id;
}

// ----------------------------

inline MediaControlTimelineContainerElement::MediaControlTimelineContainerElement(HTMLMediaElement* mediaElement)
    : MediaControlElement(mediaElement)
{
}

PassRefPtr<MediaControlTimelineContainerElement> MediaControlTimelineContainerElement::create(HTMLMediaElement* mediaElement)
{
    return adoptRef(new MediaControlTimelineContainerElement(mediaElement));
}

bool MediaControlTimelineContainerElement::rendererIsNeeded(RenderStyle* style)
{
    if (!MediaControlElement::rendererIsNeeded(style))
        return false;

    // Always show the timeline if the theme doesn't use status display (MediaControllerThemeClassic, for instance).
    if (!document()->page()->theme()->usesMediaControlStatusDisplay())
        return true;

    float duration = mediaElement()->duration();
    return !isnan(duration) && !isinf(duration);
}

MediaControlElementType MediaControlTimelineContainerElement::displayType() const
{
    return MediaTimelineContainer;
}

const AtomicString& MediaControlTimelineContainerElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-timeline-container"));
    return id;
}

// ----------------------------

inline MediaControlVolumeSliderContainerElement::MediaControlVolumeSliderContainerElement(HTMLMediaElement* mediaElement)
    : MediaControlElement(mediaElement)
    , m_isVisible(false)
    , m_x(0)
    , m_y(0)
{
}

PassRefPtr<MediaControlVolumeSliderContainerElement> MediaControlVolumeSliderContainerElement::create(HTMLMediaElement* mediaElement)
{
    return adoptRef(new MediaControlVolumeSliderContainerElement(mediaElement));
}

PassRefPtr<RenderStyle> MediaControlVolumeSliderContainerElement::styleForElement()
{
    RefPtr<RenderStyle> style = MediaControlElement::styleForElement();
    style->setPosition(AbsolutePosition);
    style->setLeft(Length(m_x, Fixed));
    style->setTop(Length(m_y, Fixed));
    style->setDisplay(m_isVisible ? BLOCK : NONE);
    return style;
}

void MediaControlVolumeSliderContainerElement::setVisible(bool visible)
{
    if (visible == m_isVisible)
        return;
    m_isVisible = visible;
}

void MediaControlVolumeSliderContainerElement::setPosition(int x, int y)
{
    if (x == m_x && y == m_y)
        return;
    m_x = x;
    m_y = y;
}

bool MediaControlVolumeSliderContainerElement::hitTest(const IntPoint& absPoint)
{
    if (renderer() && renderer()->style()->hasAppearance())
        return renderer()->theme()->hitTestMediaControlPart(renderer(), absPoint);

    return false;
}

MediaControlElementType MediaControlVolumeSliderContainerElement::displayType() const
{
    return MediaVolumeSliderContainer;
}

const AtomicString& MediaControlVolumeSliderContainerElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-volume-slider-container"));
    return id;
}

// ----------------------------

inline MediaControlStatusDisplayElement::MediaControlStatusDisplayElement(HTMLMediaElement* mediaElement)
    : MediaControlElement(mediaElement)
    , m_stateBeingDisplayed(Nothing)
{
}

PassRefPtr<MediaControlStatusDisplayElement> MediaControlStatusDisplayElement::create(HTMLMediaElement* mediaElement)
{
    return adoptRef(new MediaControlStatusDisplayElement(mediaElement));
}

void MediaControlStatusDisplayElement::update()
{
    MediaControlElement::update();

    // Get the new state that we'll have to display.
    StateBeingDisplayed newStateToDisplay = Nothing;

    if (mediaElement()->readyState() != HTMLMediaElement::HAVE_ENOUGH_DATA && !mediaElement()->currentSrc().isEmpty())
        newStateToDisplay = Loading;
    else if (mediaElement()->movieLoadType() == MediaPlayer::LiveStream)
        newStateToDisplay = LiveBroadcast;

    // Propagate only if needed.
    if (newStateToDisplay == m_stateBeingDisplayed)
        return;
    m_stateBeingDisplayed = newStateToDisplay;

    ExceptionCode e;
    switch (m_stateBeingDisplayed) {
    case Nothing:
        setInnerText("", e);
        break;
    case Loading:
        setInnerText(mediaElementLoadingStateText(), e);
        break;
    case LiveBroadcast:
        setInnerText(mediaElementLiveBroadcastStateText(), e);
        break;
    }
}

bool MediaControlStatusDisplayElement::rendererIsNeeded(RenderStyle* style)
{
    if (!MediaControlElement::rendererIsNeeded(style) || !document()->page()->theme()->usesMediaControlStatusDisplay())
        return false;
    float duration = mediaElement()->duration();
    return (isnan(duration) || isinf(duration));
}

MediaControlElementType MediaControlStatusDisplayElement::displayType() const
{
    return MediaStatusDisplay;
}

const AtomicString& MediaControlStatusDisplayElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-status-display"));
    return id;
}

// ----------------------------
    
MediaControlInputElement::MediaControlInputElement(HTMLMediaElement* mediaElement, MediaControlElementType displayType)
    : HTMLInputElement(inputTag, mediaElement->document(), 0, false)
    , m_mediaElement(mediaElement)
    , m_displayType(displayType)
{
}

void MediaControlInputElement::attachToParent(Element* parent)
{
    // FIXME: This code seems very wrong.  Why are we magically adding |this| to the DOM here?
    //        We shouldn't be calling parser API methods outside of the parser!
    parent->parserAddChild(this);
}

void MediaControlInputElement::update()
{
    updateDisplayType();
    if (renderer())
        renderer()->updateFromElement();
    updateStyle();
}

PassRefPtr<RenderStyle> MediaControlInputElement::styleForElement()
{
    return document()->styleSelector()->styleForElement(this, 0, true);
}

bool MediaControlInputElement::rendererIsNeeded(RenderStyle* style)
{
    ASSERT(document()->page());

    return HTMLInputElement::rendererIsNeeded(style) && parentNode() && parentNode()->renderer()
        && (!style->hasAppearance() || document()->page()->theme()->shouldRenderMediaControlPart(style->appearance(), mediaElement()));
}

void MediaControlInputElement::attach()
{
    RefPtr<RenderStyle> style = styleForElement();
    if (!style)
        return;
    
    bool needsRenderer = rendererIsNeeded(style.get());
    if (!needsRenderer)
        return;
    RenderObject* renderer = createRenderer(mediaElement()->renderer()->renderArena(), style.get());
    if (!renderer)
        return;
    renderer->setStyle(style.get());
    setRenderer(renderer);
    if (parentNode() && parentNode()->renderer()) {
        // Find next sibling with a renderer to determine where to insert.
        Node* sibling = nextSibling();
        while (sibling && !sibling->renderer())
            sibling = sibling->nextSibling();
        parentNode()->renderer()->addChild(renderer, sibling ? sibling->renderer() : 0);
    }  
    ContainerNode::attach();
    // FIXME: Currently, MeidaControlInput circumvents the normal attachment
    // and style recalc cycle and thus we need to add extra logic to be aware of
    // the shadow DOM. Remove this once all media controls are transitioned to use the regular
    // style calculation.
    if (Node* shadowNode = shadowRoot())
        shadowNode->attach();
}

void MediaControlInputElement::updateStyle()
{
    if (!mediaElement() || !mediaElement()->renderer())
        return;
    
    RefPtr<RenderStyle> style = styleForElement();
    if (!style)
        return;
    
    bool needsRenderer = rendererIsNeeded(style.get()) && parentNode() && parentNode()->renderer();
    if (renderer() && !needsRenderer)
        detach();
    else if (!renderer() && needsRenderer)
        attach();
    else if (renderer())
        renderer()->setStyle(style.get());

    // FIXME: Currently, MeidaControlInput circumvents the normal attachment
    // and style recalc cycle and thus we need to add extra logic to be aware of
    // the shadow DOM. Remove this once all media controls are transitioned to use
    // the new shadow DOM.
    if (Node* shadowNode = shadowRoot())
        shadowNode->recalcStyle(Node::Force);
}

bool MediaControlInputElement::hitTest(const IntPoint& absPoint)
{
    if (renderer() && renderer()->style()->hasAppearance())
        return renderer()->theme()->hitTestMediaControlPart(renderer(), absPoint);

    return false;
}

void MediaControlInputElement::setDisplayType(MediaControlElementType displayType)
{
    if (displayType == m_displayType)
        return;

    m_displayType = displayType;
    if (RenderObject* object = renderer())
        object->repaint();
}

// ----------------------------

inline MediaControlMuteButtonElement::MediaControlMuteButtonElement(HTMLMediaElement* mediaElement, MediaControlElementType displayType)
    : MediaControlInputElement(mediaElement, displayType)
{
}

void MediaControlMuteButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().clickEvent) {
        mediaElement()->setMuted(!mediaElement()->muted());
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlMuteButtonElement::updateDisplayType()
{
    setDisplayType(mediaElement()->muted() ? MediaUnMuteButton : MediaMuteButton);
}

// ----------------------------

inline MediaControlPanelMuteButtonElement::MediaControlPanelMuteButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlMuteButtonElement(mediaElement, MediaMuteButton)
{
}

PassRefPtr<MediaControlPanelMuteButtonElement> MediaControlPanelMuteButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlPanelMuteButtonElement> button = adoptRef(new MediaControlPanelMuteButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

const AtomicString& MediaControlPanelMuteButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-mute-button"));
    return id;
}

// ----------------------------

inline MediaControlVolumeSliderMuteButtonElement::MediaControlVolumeSliderMuteButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlMuteButtonElement(mediaElement, MediaVolumeSliderMuteButton)
{
}

PassRefPtr<MediaControlVolumeSliderMuteButtonElement> MediaControlVolumeSliderMuteButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlVolumeSliderMuteButtonElement> button = adoptRef(new MediaControlVolumeSliderMuteButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

const AtomicString& MediaControlVolumeSliderMuteButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-volume-slider-mute-button"));
    return id;
}

// ----------------------------

inline MediaControlPlayButtonElement::MediaControlPlayButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlInputElement(mediaElement, MediaPlayButton)
{
}

PassRefPtr<MediaControlPlayButtonElement> MediaControlPlayButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlPlayButtonElement> button = adoptRef(new MediaControlPlayButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

void MediaControlPlayButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().clickEvent) {
        mediaElement()->togglePlayState();
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlPlayButtonElement::updateDisplayType()
{
    setDisplayType(mediaElement()->canPlay() ? MediaPlayButton : MediaPauseButton);
}

const AtomicString& MediaControlPlayButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-play-button"));
    return id;
}

// ----------------------------

inline MediaControlSeekButtonElement::MediaControlSeekButtonElement(HTMLMediaElement* mediaElement, MediaControlElementType displayType)
    : MediaControlInputElement(mediaElement, displayType)
    , m_seeking(false)
    , m_capturing(false)
    , m_seekTimer(this, &MediaControlSeekButtonElement::seekTimerFired)
{
}

void MediaControlSeekButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().mousedownEvent) {
        if (Frame* frame = document()->frame()) {
            m_capturing = true;
            frame->eventHandler()->setCapturingMouseEventsNode(this);
        }
        mediaElement()->pause(event->fromUserGesture());
        m_seekTimer.startRepeating(cSeekRepeatDelay);
        event->setDefaultHandled();
    } else if (event->type() == eventNames().mouseupEvent) {
        if (m_capturing)
            if (Frame* frame = document()->frame()) {
                m_capturing = false;
                frame->eventHandler()->setCapturingMouseEventsNode(0);
            }
        ExceptionCode ec;
        if (m_seeking || m_seekTimer.isActive()) {
            if (!m_seeking) {
                float stepTime = isForwardButton() ? cStepTime : -cStepTime;
                mediaElement()->setCurrentTime(mediaElement()->currentTime() + stepTime, ec);
            }
            m_seekTimer.stop();
            m_seeking = false;
            event->setDefaultHandled();
        }
    }
    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlSeekButtonElement::seekTimerFired(Timer<MediaControlSeekButtonElement>*)
{
    ExceptionCode ec;
    m_seeking = true;
    float seekTime = isForwardButton() ? cSeekTime : -cSeekTime;
    mediaElement()->setCurrentTime(mediaElement()->currentTime() + seekTime, ec);
}

void MediaControlSeekButtonElement::detach()
{
    if (m_capturing) {
        if (Frame* frame = document()->frame())
            frame->eventHandler()->setCapturingMouseEventsNode(0);      
    }
    MediaControlInputElement::detach();
}

// ----------------------------

inline MediaControlSeekForwardButtonElement::MediaControlSeekForwardButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlSeekButtonElement(mediaElement, MediaSeekForwardButton)
{
}

PassRefPtr<MediaControlSeekForwardButtonElement> MediaControlSeekForwardButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlSeekForwardButtonElement> button = adoptRef(new MediaControlSeekForwardButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

const AtomicString& MediaControlSeekForwardButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-seek-forward-button"));
    return id;
}

// ----------------------------

inline MediaControlSeekBackButtonElement::MediaControlSeekBackButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlSeekButtonElement(mediaElement, MediaSeekBackButton)
{
}

PassRefPtr<MediaControlSeekBackButtonElement> MediaControlSeekBackButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlSeekBackButtonElement> button = adoptRef(new MediaControlSeekBackButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

const AtomicString& MediaControlSeekBackButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-seek-back-button"));
    return id;
}

// ----------------------------

inline MediaControlRewindButtonElement::MediaControlRewindButtonElement(HTMLMediaElement* element)
    : MediaControlInputElement(element, MediaRewindButton)
{
}

PassRefPtr<MediaControlRewindButtonElement> MediaControlRewindButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlRewindButtonElement> button = adoptRef(new MediaControlRewindButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

void MediaControlRewindButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().clickEvent) {
        mediaElement()->rewind(30);
        event->setDefaultHandled();
    }    
    HTMLInputElement::defaultEventHandler(event);
}

const AtomicString& MediaControlRewindButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-rewind-button"));
    return id;
}

// ----------------------------

inline MediaControlReturnToRealtimeButtonElement::MediaControlReturnToRealtimeButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlInputElement(mediaElement, MediaReturnToRealtimeButton)
{
}

PassRefPtr<MediaControlReturnToRealtimeButtonElement> MediaControlReturnToRealtimeButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlReturnToRealtimeButtonElement> button = adoptRef(new MediaControlReturnToRealtimeButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

void MediaControlReturnToRealtimeButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().clickEvent) {
        mediaElement()->returnToRealtime();
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

const AtomicString& MediaControlReturnToRealtimeButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-return-to-realtime-button"));
    return id;
}

// ----------------------------

inline MediaControlToggleClosedCaptionsButtonElement::MediaControlToggleClosedCaptionsButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlInputElement(mediaElement, MediaShowClosedCaptionsButton)
{
}

PassRefPtr<MediaControlToggleClosedCaptionsButtonElement> MediaControlToggleClosedCaptionsButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlToggleClosedCaptionsButtonElement> button = adoptRef(new MediaControlToggleClosedCaptionsButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

void MediaControlToggleClosedCaptionsButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().clickEvent) {
        mediaElement()->setClosedCaptionsVisible(!mediaElement()->closedCaptionsVisible());
        setChecked(mediaElement()->closedCaptionsVisible());
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlToggleClosedCaptionsButtonElement::updateDisplayType()
{
    setDisplayType(mediaElement()->closedCaptionsVisible() ? MediaHideClosedCaptionsButton : MediaShowClosedCaptionsButton);
}

const AtomicString& MediaControlToggleClosedCaptionsButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-toggle-closed-captions-button"));
    return id;
}

// ----------------------------

MediaControlTimelineElement::MediaControlTimelineElement(HTMLMediaElement* mediaElement)
    : MediaControlInputElement(mediaElement, MediaSlider)
{
}

PassRefPtr<MediaControlTimelineElement> MediaControlTimelineElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlTimelineElement> timeline = adoptRef(new MediaControlTimelineElement(mediaElement));
    timeline->setType("range");
    return timeline.release();
}

void MediaControlTimelineElement::defaultEventHandler(Event* event)
{
    // Left button is 0. Rejects mouse events not from left button.
    if (event->isMouseEvent() && static_cast<MouseEvent*>(event)->button())
        return;

    if (!attached())
        return;

    if (event->type() == eventNames().mousedownEvent)
        mediaElement()->beginScrubbing();

    MediaControlInputElement::defaultEventHandler(event);

    if (event->type() == eventNames().mouseoverEvent || event->type() == eventNames().mouseoutEvent || event->type() == eventNames().mousemoveEvent)
        return;

    float time = narrowPrecisionToFloat(value().toDouble());
    if (time != mediaElement()->currentTime()) {
        ExceptionCode ec;
        mediaElement()->setCurrentTime(time, ec);
    }

    RenderSlider* slider = toRenderSlider(renderer());
    if (slider && slider->inDragMode())
        toRenderMedia(mediaElement()->renderer())->controls()->updateTimeDisplay();

    if (event->type() == eventNames().mouseupEvent)
        mediaElement()->endScrubbing();
}

void MediaControlTimelineElement::update(bool updateDuration) 
{
    if (updateDuration) {
        float duration = mediaElement()->duration();
        setAttribute(maxAttr, String::number(isfinite(duration) ? duration : 0));
    }
    setValue(String::number(mediaElement()->currentTime()));
    MediaControlInputElement::update();
}

const AtomicString& MediaControlTimelineElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-timeline"));
    return id;
}

// ----------------------------

inline MediaControlVolumeSliderElement::MediaControlVolumeSliderElement(HTMLMediaElement* mediaElement)
    : MediaControlInputElement(mediaElement, MediaVolumeSlider)
{
}

PassRefPtr<MediaControlVolumeSliderElement> MediaControlVolumeSliderElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlVolumeSliderElement> slider = adoptRef(new MediaControlVolumeSliderElement(mediaElement));
    slider->setType("range");
    return slider.release();
}

void MediaControlVolumeSliderElement::defaultEventHandler(Event* event)
{
    // Left button is 0. Rejects mouse events not from left button.
    if (event->isMouseEvent() && static_cast<MouseEvent*>(event)->button())
        return;

    if (!attached())
        return;

    MediaControlInputElement::defaultEventHandler(event);

    if (event->type() == eventNames().mouseoverEvent || event->type() == eventNames().mouseoutEvent || event->type() == eventNames().mousemoveEvent)
        return;

    float volume = narrowPrecisionToFloat(value().toDouble());
    if (volume != mediaElement()->volume()) {
        ExceptionCode ec = 0;
        mediaElement()->setVolume(volume, ec);
        ASSERT(!ec);
    }
}

void MediaControlVolumeSliderElement::update()
{
    float volume = mediaElement()->volume();
    if (value().toFloat() != volume)
        setValue(String::number(volume));
    MediaControlInputElement::update();
}

const AtomicString& MediaControlVolumeSliderElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-volume-slider"));
    return id;
}

// ----------------------------

inline MediaControlFullscreenButtonElement::MediaControlFullscreenButtonElement(HTMLMediaElement* mediaElement)
    : MediaControlInputElement(mediaElement, MediaFullscreenButton)
{
}

PassRefPtr<MediaControlFullscreenButtonElement> MediaControlFullscreenButtonElement::create(HTMLMediaElement* mediaElement)
{
    RefPtr<MediaControlFullscreenButtonElement> button = adoptRef(new MediaControlFullscreenButtonElement(mediaElement));
    button->setType("button");
    return button.release();
}

void MediaControlFullscreenButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == eventNames().clickEvent) {
#if ENABLE(FULLSCREEN_API)
        // Only use the new full screen API if the fullScreenEnabled setting has 
        // been explicitly enabled.  Otherwise, use the old fullscreen API.  This
        // allows apps which embed a WebView to retain the existing full screen
        // video implementation without requiring them to implement their own full 
        // screen behavior.
        if (document()->settings() && document()->settings()->fullScreenEnabled()) {
            if (document()->webkitIsFullScreen() && document()->webkitCurrentFullScreenElement() == mediaElement())
                document()->webkitCancelFullScreen();
            else
                mediaElement()->webkitRequestFullScreen(0);
        } else
#endif
            mediaElement()->enterFullscreen();
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

const AtomicString& MediaControlFullscreenButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-fullscreen-button"));
    return id;
}

// ----------------------------

inline MediaControlTimeDisplayElement::MediaControlTimeDisplayElement(HTMLMediaElement* mediaElement)
    : MediaControlElement(mediaElement)
    , m_currentValue(0)
    , m_isVisible(true)
{
}

PassRefPtr<RenderStyle> MediaControlTimeDisplayElement::styleForElement()
{
    RefPtr<RenderStyle> style = MediaControlElement::styleForElement();
    if (!m_isVisible) {
        style = RenderStyle::clone(style.get());
        style->setWidth(Length(0, Fixed));
    }
    return style;
}

void MediaControlTimeDisplayElement::setVisible(bool visible)
{
    if (visible == m_isVisible)
        return;
    m_isVisible = visible;

    // This function is used during the RenderMedia::layout()
    // call, where we cannot change the renderer at this time.
    if (!renderer() || !renderer()->style())
        return;

    RefPtr<RenderStyle> style = styleForElement();
    renderer()->setStyle(style.get());
}

void MediaControlTimeDisplayElement::setCurrentValue(float time)
{
    m_currentValue = time;
}

// ----------------------------

PassRefPtr<MediaControlTimeRemainingDisplayElement> MediaControlTimeRemainingDisplayElement::create(HTMLMediaElement* mediaElement)
{
    return adoptRef(new MediaControlTimeRemainingDisplayElement(mediaElement));
}

MediaControlTimeRemainingDisplayElement::MediaControlTimeRemainingDisplayElement(HTMLMediaElement* mediaElement)
    : MediaControlTimeDisplayElement(mediaElement)
{
}

MediaControlElementType MediaControlTimeRemainingDisplayElement::displayType() const
{
    return MediaTimeRemainingDisplay;
}

const AtomicString& MediaControlTimeRemainingDisplayElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-time-remaining-display"));
    return id;
}

// ----------------------------

PassRefPtr<MediaControlCurrentTimeDisplayElement> MediaControlCurrentTimeDisplayElement::create(HTMLMediaElement* mediaElement)
{
    return adoptRef(new MediaControlCurrentTimeDisplayElement(mediaElement));
}

MediaControlCurrentTimeDisplayElement::MediaControlCurrentTimeDisplayElement(HTMLMediaElement* mediaElement)
    : MediaControlTimeDisplayElement(mediaElement)
{
}

MediaControlElementType MediaControlCurrentTimeDisplayElement::displayType() const
{
    return MediaCurrentTimeDisplay;
}

const AtomicString& MediaControlCurrentTimeDisplayElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id, ("-webkit-media-controls-current-time-display"));
    return id;
}

} // namespace WebCore

#endif // ENABLE(VIDEO)
