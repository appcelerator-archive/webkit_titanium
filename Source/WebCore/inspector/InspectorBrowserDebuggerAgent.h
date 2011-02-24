/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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

#ifndef InspectorBrowserDebuggerAgent_h
#define InspectorBrowserDebuggerAgent_h

#if ENABLE(JAVASCRIPT_DEBUGGER) && ENABLE(INSPECTOR)

#include "PlatformString.h"

#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

class Element;
class InspectorAgent;
class InspectorObject;
class Node;

typedef String ErrorString;

class InspectorBrowserDebuggerAgent {
    WTF_MAKE_NONCOPYABLE(InspectorBrowserDebuggerAgent);
public:
    static PassOwnPtr<InspectorBrowserDebuggerAgent> create(InspectorAgent*, bool eraseStickyBreakpoints);

    virtual ~InspectorBrowserDebuggerAgent();

    void setAllBrowserBreakpoints(ErrorString* error, PassRefPtr<InspectorObject>);
    void inspectedURLChanged(const String& url);

    // BrowserDebugger API for InspectorFrontend
    void setXHRBreakpoint(ErrorString* error, const String& url);
    void removeXHRBreakpoint(ErrorString* error, const String& url);
    void setEventListenerBreakpoint(ErrorString* error, const String& eventName);
    void removeEventListenerBreakpoint(ErrorString* error, const String& eventName);
    void setDOMBreakpoint(ErrorString* error, long nodeId, long type);
    void removeDOMBreakpoint(ErrorString* error, long nodeId, long type);

    // InspectorInstrumentation API
    void willInsertDOMNode(Node*, Node* parent);
    void didInsertDOMNode(Node*);
    void willRemoveDOMNode(Node*);
    void didRemoveDOMNode(Node*);
    void willModifyDOMAttr(Element*);
    void willSendXMLHttpRequest(const String& url);
    void pauseOnNativeEventIfNeeded(const String& categoryType, const String& eventName, bool synchronous);

private:
    InspectorBrowserDebuggerAgent(InspectorAgent*, bool eraseStickyBreakpoints);

    void restoreStickyBreakpoint(PassRefPtr<InspectorObject> breakpoint);

    void descriptionForDOMEvent(Node* target, long breakpointType, bool insertion, InspectorObject* description);
    void updateSubtreeBreakpoints(Node*, uint32_t rootMask, bool set);
    bool hasBreakpoint(Node*, long type);
    void discardBindings();

    InspectorAgent* m_inspectorAgent;
    HashMap<Node*, uint32_t> m_domBreakpoints;
    HashSet<String> m_eventListenerBreakpoints;
    HashSet<String> m_XHRBreakpoints;
    bool m_hasXHRBreakpointWithEmptyURL;
};

} // namespace WebCore

#endif // ENABLE(JAVASCRIPT_DEBUGGER) && ENABLE(INSPECTOR)

#endif // !defined(InspectorBrowserDebuggerAgent_h)
