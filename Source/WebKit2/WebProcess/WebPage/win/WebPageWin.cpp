/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include "WebPage.h"

#include "FontSmoothingLevel.h"
#include "WebEvent.h"
#include "WebPreferencesStore.h"
#include <WebCore/FocusController.h>
#include <WebCore/FontRenderingMode.h>
#include <WebCore/Frame.h>
#include <WebCore/FrameView.h>
#include <WebCore/KeyboardEvent.h>
#include <WebCore/Page.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/ResourceHandle.h>
#include <WebCore/Settings.h>
#if PLATFORM(CG)
#include <WebKitSystemInterface/WebKitSystemInterface.h>
#endif
#include <WinUser.h>

#if USE(CFNETWORK)
#include <CFNetwork/CFURLCachePriv.h>
#include <CFNetwork/CFURLProtocolPriv.h>
#include <CFNetwork/CFURLRequestPriv.h>
#endif

using namespace WebCore;

namespace WebKit {

void WebPage::platformInitialize()
{
    m_page->settings()->setFontRenderingMode(AlternateRenderingMode);
}

void WebPage::platformPreferencesDidChange(const WebPreferencesStore& store)
{
    FontSmoothingLevel fontSmoothingLevel = static_cast<FontSmoothingLevel>(store.getUInt32ValueForKey(WebPreferencesKey::fontSmoothingLevelKey()));

#if PLATFORM(CG)
    FontSmoothingLevel adjustedLevel = fontSmoothingLevel;
    if (adjustedLevel == FontSmoothingLevelWindows)
        adjustedLevel = FontSmoothingLevelMedium;
    wkSetFontSmoothingLevel(adjustedLevel);
#endif

    m_page->settings()->setFontRenderingMode(fontSmoothingLevel == FontSmoothingLevelWindows ? AlternateRenderingMode : NormalRenderingMode);
}

static const unsigned CtrlKey = 1 << 0;
static const unsigned AltKey = 1 << 1;
static const unsigned ShiftKey = 1 << 2;

struct KeyDownEntry {
    unsigned virtualKey;
    unsigned modifiers;
    const char* name;
};

struct KeyPressEntry {
    unsigned charCode;
    unsigned modifiers;
    const char* name;
};

static const KeyDownEntry keyDownEntries[] = {
    { VK_LEFT,   0,                  "MoveLeft"                                    },
    { VK_LEFT,   ShiftKey,           "MoveLeftAndModifySelection"                  },
    { VK_LEFT,   CtrlKey,            "MoveWordLeft"                                },
    { VK_LEFT,   CtrlKey | ShiftKey, "MoveWordLeftAndModifySelection"              },
    { VK_RIGHT,  0,                  "MoveRight"                                   },
    { VK_RIGHT,  ShiftKey,           "MoveRightAndModifySelection"                 },
    { VK_RIGHT,  CtrlKey,            "MoveWordRight"                               },
    { VK_RIGHT,  CtrlKey | ShiftKey, "MoveWordRightAndModifySelection"             },
    { VK_UP,     0,                  "MoveUp"                                      },
    { VK_UP,     ShiftKey,           "MoveUpAndModifySelection"                    },
    { VK_PRIOR,  ShiftKey,           "MovePageUpAndModifySelection"                },
    { VK_DOWN,   0,                  "MoveDown"                                    },
    { VK_DOWN,   ShiftKey,           "MoveDownAndModifySelection"                  },
    { VK_NEXT,   ShiftKey,           "MovePageDownAndModifySelection"              },
    { VK_PRIOR,  0,                  "MovePageUp"                                  },
    { VK_NEXT,   0,                  "MovePageDown"                                },
    { VK_HOME,   0,                  "MoveToBeginningOfLine"                       },
    { VK_HOME,   ShiftKey,           "MoveToBeginningOfLineAndModifySelection"     },
    { VK_HOME,   CtrlKey,            "MoveToBeginningOfDocument"                   },
    { VK_HOME,   CtrlKey | ShiftKey, "MoveToBeginningOfDocumentAndModifySelection" },

    { VK_END,    0,                  "MoveToEndOfLine"                             },
    { VK_END,    ShiftKey,           "MoveToEndOfLineAndModifySelection"           },
    { VK_END,    CtrlKey,            "MoveToEndOfDocument"                         },
    { VK_END,    CtrlKey | ShiftKey, "MoveToEndOfDocumentAndModifySelection"       },

    { VK_BACK,   0,                  "DeleteBackward"                              },
    { VK_BACK,   ShiftKey,           "DeleteBackward"                              },
    { VK_DELETE, 0,                  "DeleteForward"                               },
    { VK_BACK,   CtrlKey,            "DeleteWordBackward"                          },
    { VK_DELETE, CtrlKey,            "DeleteWordForward"                           },
    
    { 'B',       CtrlKey,            "ToggleBold"                                  },
    { 'I',       CtrlKey,            "ToggleItalic"                                },

    { VK_ESCAPE, 0,                  "Cancel"                                      },
    { VK_OEM_PERIOD, CtrlKey,        "Cancel"                                      },
    { VK_TAB,    0,                  "InsertTab"                                   },
    { VK_TAB,    ShiftKey,           "InsertBacktab"                               },
    { VK_RETURN, 0,                  "InsertNewline"                               },
    { VK_RETURN, CtrlKey,            "InsertNewline"                               },
    { VK_RETURN, AltKey,             "InsertNewline"                               },
    { VK_RETURN, ShiftKey,           "InsertNewline"                               },
    { VK_RETURN, AltKey | ShiftKey,  "InsertNewline"                               },

    // It's not quite clear whether clipboard shortcuts and Undo/Redo should be handled
    // in the application or in WebKit. We chose WebKit.
    { 'C',       CtrlKey,            "Copy"                                        },
    { 'V',       CtrlKey,            "Paste"                                       },
    { 'X',       CtrlKey,            "Cut"                                         },
    { 'A',       CtrlKey,            "SelectAll"                                   },
    { VK_INSERT, CtrlKey,            "Copy"                                        },
    { VK_DELETE, ShiftKey,           "Cut"                                         },
    { VK_INSERT, ShiftKey,           "Paste"                                       },
    { 'Z',       CtrlKey,            "Undo"                                        },
    { 'Z',       CtrlKey | ShiftKey, "Redo"                                        },
};

static const KeyPressEntry keyPressEntries[] = {
    { '\t',   0,                  "InsertTab"                                   },
    { '\t',   ShiftKey,           "InsertBacktab"                               },
    { '\r',   0,                  "InsertNewline"                               },
    { '\r',   CtrlKey,            "InsertNewline"                               },
    { '\r',   AltKey,             "InsertNewline"                               },
    { '\r',   ShiftKey,           "InsertNewline"                               },
    { '\r',   AltKey | ShiftKey,  "InsertNewline"                               },
};

const char* WebPage::interpretKeyEvent(const KeyboardEvent* evt)
{
    ASSERT(evt->type() == eventNames().keydownEvent || evt->type() == eventNames().keypressEvent);

    static HashMap<int, const char*>* keyDownCommandsMap = 0;
    static HashMap<int, const char*>* keyPressCommandsMap = 0;

    if (!keyDownCommandsMap) {
        keyDownCommandsMap = new HashMap<int, const char*>;
        keyPressCommandsMap = new HashMap<int, const char*>;

        for (size_t i = 0; i < WTF_ARRAY_LENGTH(keyDownEntries); ++i)
            keyDownCommandsMap->set(keyDownEntries[i].modifiers << 16 | keyDownEntries[i].virtualKey, keyDownEntries[i].name);

        for (size_t i = 0; i < WTF_ARRAY_LENGTH(keyPressEntries); ++i)
            keyPressCommandsMap->set(keyPressEntries[i].modifiers << 16 | keyPressEntries[i].charCode, keyPressEntries[i].name);
    }

    unsigned modifiers = 0;
    if (evt->shiftKey())
        modifiers |= ShiftKey;
    if (evt->altKey())
        modifiers |= AltKey;
    if (evt->ctrlKey())
        modifiers |= CtrlKey;

    if (evt->type() == eventNames().keydownEvent) {
        int mapKey = modifiers << 16 | evt->keyCode();
        return mapKey ? keyDownCommandsMap->get(mapKey) : 0;
    }

    int mapKey = modifiers << 16 | evt->charCode();
    return mapKey ? keyPressCommandsMap->get(mapKey) : 0;
}

static inline void scroll(Page* page, ScrollDirection direction, ScrollGranularity granularity)
{
    page->focusController()->focusedOrMainFrame()->eventHandler()->scrollRecursively(direction, granularity);
}

static inline void logicalScroll(Page* page, ScrollLogicalDirection direction, ScrollGranularity granularity)
{
    page->focusController()->focusedOrMainFrame()->eventHandler()->logicalScrollRecursively(direction, granularity);
}

bool WebPage::performDefaultBehaviorForKeyEvent(const WebKeyboardEvent& keyboardEvent)
{
    if (keyboardEvent.type() != WebEvent::KeyDown && keyboardEvent.type() != WebEvent::RawKeyDown)
        return false;

    switch (keyboardEvent.windowsVirtualKeyCode()) {
    case VK_BACK:
        if (keyboardEvent.shiftKey())
            m_page->goForward();
        else
            m_page->goBack();
        break;
    case VK_LEFT:
        scroll(m_page.get(), ScrollLeft, ScrollByLine);
        break;
    case VK_RIGHT:
        scroll(m_page.get(), ScrollRight, ScrollByLine);
        break;
    case VK_UP:
        scroll(m_page.get(), ScrollUp, ScrollByLine);
        break;
    case VK_DOWN:
        scroll(m_page.get(), ScrollDown, ScrollByLine);
        break;
    case VK_HOME:
        logicalScroll(m_page.get(), ScrollBlockDirectionBackward, ScrollByDocument);
        break;
    case VK_END:
        logicalScroll(m_page.get(), ScrollBlockDirectionForward, ScrollByDocument);
        break;
    case VK_PRIOR:
        logicalScroll(m_page.get(), ScrollBlockDirectionBackward, ScrollByPage);
        break;
    case VK_NEXT:
        logicalScroll(m_page.get(), ScrollBlockDirectionForward, ScrollByPage);
        break;
    default:
        return false;
    }

    return true;
}

bool WebPage::platformHasLocalDataForURL(const WebCore::KURL& url)
{
#if USE(CFNETWORK)
    RetainPtr<CFURLRef> cfURL(AdoptCF, url.createCFURL());
    RetainPtr<CFMutableURLRequestRef> request(AdoptCF, CFURLRequestCreateMutable(0, cfURL.get(), kCFURLRequestCachePolicyReloadIgnoringCache, 60, 0));
    
    RetainPtr<CFStringRef> userAgent(AdoptCF, userAgent().createCFString());
    CFURLRequestSetHTTPHeaderFieldValue(request.get(), CFSTR("User-Agent"), userAgent.get());

    RetainPtr<CFURLCacheRef> cache;
#if USE(CFURLSTORAGESESSIONS)
    if (CFURLStorageSessionRef storageSession = ResourceHandle::privateBrowsingStorageSession())
        cache.adoptCF(wkCopyURLCache(storageSession));
    else
#endif
        cache.adoptCF(CFURLCacheCopySharedURLCache());

    RetainPtr<CFCachedURLResponseRef> response(AdoptCF, CFURLCacheCopyResponseForRequest(cache.get(), request.get()));    
    return response;
#else
    return false;
#endif
}

String WebPage::cachedResponseMIMETypeForURL(const WebCore::KURL& url)
{
#if USE(CFNETWORK)
    RetainPtr<CFURLRef> cfURL(AdoptCF, url.createCFURL());
    RetainPtr<CFMutableURLRequestRef> request(AdoptCF, CFURLRequestCreateMutable(0, cfURL.get(), kCFURLRequestCachePolicyReloadIgnoringCache, 60, 0));
    
    RetainPtr<CFStringRef> userAgent(AdoptCF, userAgent().createCFString());
    CFURLRequestSetHTTPHeaderFieldValue(request.get(), CFSTR("User-Agent"), userAgent.get());

    RetainPtr<CFURLCacheRef> cache;
#if USE(CFURLSTORAGESESSIONS)
    if (CFURLStorageSessionRef storageSession = ResourceHandle::privateBrowsingStorageSession())
        cache.adoptCF(wkCopyURLCache(storageSession));
    else
#endif
        cache.adoptCF(CFURLCacheCopySharedURLCache());

    RetainPtr<CFCachedURLResponseRef> cachedResponse(AdoptCF, CFURLCacheCopyResponseForRequest(cache.get(), request.get()));
    
    CFURLResponseRef response = CFCachedURLResponseGetWrappedResponse(cachedResponse.get());
    
    return response ? CFURLResponseGetMIMEType(response) : String();
#else
    return String();
#endif
}

bool WebPage::canHandleRequest(const WebCore::ResourceRequest& request)
{
#if USE(CFNETWORK)
     // FIXME: Are there other requests we need to be able to handle? WebKit1's WebView.cpp has a FIXME here as well.
    return CFURLProtocolCanHandleRequest(request.cfURLRequest());
#else
    return true;
#endif
}

void WebPage::confirmComposition(const String& compositionString)
{
    Frame* frame = m_page->focusController()->focusedOrMainFrame();
    if (!frame || !frame->editor()->canEdit())
        return;
    frame->editor()->confirmComposition(compositionString);
}

void WebPage::setComposition(const String& compositionString, const Vector<WebCore::CompositionUnderline>& underlines, uint64_t cursorPosition)
{
    Frame* frame = m_page->focusController()->focusedOrMainFrame();
    if (!frame || !frame->editor()->canEdit())
        return;
    frame->editor()->setComposition(compositionString, underlines, cursorPosition, 0);
}

void WebPage::firstRectForCharacterInSelectedRange(const uint64_t characterPosition, WebCore::IntRect& resultRect)
{
    Frame* frame = m_page->focusController()->focusedOrMainFrame();
    IntRect rect;
    if (RefPtr<Range> range = frame->editor()->hasComposition() ? frame->editor()->compositionRange() : frame->selection()->selection().toNormalizedRange()) {
        ExceptionCode ec = 0;
        RefPtr<Range> tempRange = range->cloneRange(ec);
        tempRange->setStart(tempRange->startContainer(ec), tempRange->startOffset(ec) + characterPosition, ec);
        rect = frame->editor()->firstRectForRange(tempRange.get());
    }
    resultRect = frame->view()->contentsToWindow(rect);
}

void WebPage::getSelectedText(String& text)
{
    Frame* frame = m_page->focusController()->focusedOrMainFrame();
    RefPtr<Range> selectedRange = frame->selection()->toNormalizedRange();
    text = selectedRange->text();
}

} // namespace WebKit
