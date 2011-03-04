/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#ifndef InjectedScript_h
#define InjectedScript_h

#include "InjectedScriptHost.h"
#include "ScriptObject.h"
#include <wtf/Forward.h>
#include <wtf/Noncopyable.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class InspectorValue;
class Node;
class ScriptFunctionCall;

class InjectedScript {
public:
    InjectedScript() { }
    ~InjectedScript() { }

    bool hasNoValue() const { return m_injectedScriptObject.hasNoValue(); }

    void evaluate(const String& expression, const String& objectGroup, bool includeCommandLineAPI, RefPtr<InspectorValue>* result);
    void evaluateOn(PassRefPtr<InspectorObject> objectId, const String& expression, RefPtr<InspectorValue>* result);
    void evaluateOnCallFrame(PassRefPtr<InspectorObject> callFrameId, const String& expression, const String& objectGroup, bool includeCommandLineAPI, RefPtr<InspectorValue>* result);
    void getProperties(PassRefPtr<InspectorObject> objectId, bool ignoreHasOwnProperty, bool abbreviate, RefPtr<InspectorValue>* result);
    Node* nodeForObjectId(PassRefPtr<InspectorObject> objectId);
    void resolveNode(long nodeId, RefPtr<InspectorValue>* result);
    void setPropertyValue(PassRefPtr<InspectorObject> objectId, const String& propertyName, const String& expression, RefPtr<InspectorValue>* result);
    void releaseObject(PassRefPtr<InspectorObject> objectId);
    
#if ENABLE(JAVASCRIPT_DEBUGGER)
    PassRefPtr<InspectorValue> callFrames();
#endif

    PassRefPtr<InspectorObject> wrapObject(ScriptValue, const String& groupName);
    PassRefPtr<InspectorObject> wrapNode(Node*, const String& groupName);
    void inspectNode(Node*);
    void releaseObjectGroup(const String&);
    ScriptState* scriptState() const { return m_injectedScriptObject.scriptState(); }

private:
    friend InjectedScript InjectedScriptHost::injectedScriptFor(ScriptState*);
    explicit InjectedScript(ScriptObject);

    bool canAccessInspectedWindow();
    void makeCall(ScriptFunctionCall&, RefPtr<InspectorValue>* result);
    ScriptValue nodeAsScriptValue(Node*);

    ScriptObject m_injectedScriptObject;
};

} // namespace WebCore

#endif
