/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Matt Lilek <webkit@mattlilek.com>
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

#include "config.h"
#include "JSInjectedScriptHost.h"

#if ENABLE(INSPECTOR)

#include "Console.h"
#include "JSMainThreadExecState.h"
#if ENABLE(DATABASE)
#include "Database.h"
#include "JSDatabase.h"
#endif
#include "ExceptionCode.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "InjectedScript.h"
#include "InjectedScriptHost.h"
#include "InspectorAgent.h"
#include "InspectorValues.h"
#include "JSDOMWindow.h"
#include "JSDOMWindowCustom.h"
#include "JSNode.h"
#include "JSRange.h"
#include "Node.h"
#include "Page.h"
#include "ScriptValue.h"
#if ENABLE(DOM_STORAGE)
#include "Storage.h"
#include "JSStorage.h"
#endif
#include "TextIterator.h"
#include "VisiblePosition.h"
#include <parser/SourceCode.h>
#include <runtime/JSArray.h>
#include <runtime/JSLock.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>

#if ENABLE(JAVASCRIPT_DEBUGGER)
#include "JavaScriptCallFrame.h"
#include "JSJavaScriptCallFrame.h"
#include "ScriptDebugServer.h"
#endif

using namespace JSC;

namespace WebCore {

Node* InjectedScriptHost::scriptValueAsNode(ScriptValue value)
{
    if (!value.isObject() || value.isNull())
        return 0;
    return toNode(value.jsValue());
}

ScriptValue InjectedScriptHost::nodeAsScriptValue(ScriptState* state, Node* node)
{
    return ScriptValue(state->globalData(), toJS(state, node));
}

ScriptObject InjectedScriptHost::createInjectedScript(const String& source, ScriptState* scriptState, long id)
{
    SourceCode sourceCode = makeSource(stringToUString(source));
    JSLock lock(SilenceAssertionsOnly);
    JSDOMGlobalObject* globalObject = static_cast<JSDOMGlobalObject*>(scriptState->lexicalGlobalObject());
    JSValue globalThisValue = scriptState->globalThisValue();
    Completion comp = JSMainThreadExecState::evaluate(scriptState, globalObject->globalScopeChain(), sourceCode, globalThisValue);
    if (comp.complType() != JSC::Normal && comp.complType() != JSC::ReturnValue)
        return ScriptObject();
    JSValue functionValue = comp.value();
    CallData callData;
    CallType callType = getCallData(functionValue, callData);
    if (callType == CallTypeNone)
        return ScriptObject();

    MarkedArgumentBuffer args;
    args.append(toJS(scriptState, globalObject, this));
    args.append(globalThisValue);
    args.append(jsNumber(id));
    JSValue result = JSC::call(scriptState, functionValue, callType, callData, globalThisValue, args);
    if (result.isObject())
        return ScriptObject(scriptState, result.getObject());
    return ScriptObject();
}

void InjectedScriptHost::discardInjectedScript(ScriptState* scriptState)
{
    JSDOMGlobalObject* globalObject = static_cast<JSDOMGlobalObject*>(scriptState->lexicalGlobalObject());
    globalObject->setInjectedScript(0);
}

JSValue JSInjectedScriptHost::currentCallFrame(ExecState* exec)
{
#if ENABLE(JAVASCRIPT_DEBUGGER)
    JavaScriptCallFrame* callFrame = ScriptDebugServer::shared().currentCallFrame();
    if (!callFrame || !callFrame->isValid())
        return jsUndefined();

    JSLock lock(SilenceAssertionsOnly);
    return toJS(exec, callFrame);
#else
    UNUSED_PARAM(exec);
    return jsUndefined();
#endif
}

JSValue JSInjectedScriptHost::inspectedNode(ExecState* exec)
{
    if (exec->argumentCount() < 1)
        return jsUndefined();

    Node* node = impl()->inspectedNode(exec->argument(0).toInt32(exec));
    if (!node)
        return jsUndefined();

    JSLock lock(SilenceAssertionsOnly);
    return toJS(exec, node);
}

JSValue JSInjectedScriptHost::internalConstructorName(ExecState* exec)
{
    if (exec->argumentCount() < 1)
        return jsUndefined();

    UString result = exec->argument(0).toThisObject(exec)->className();
    return jsString(exec, result);
}

JSValue JSInjectedScriptHost::inspect(ExecState* exec)
{
    if (exec->argumentCount() >= 2) {
        ScriptValue objectId(exec->globalData(), exec->argument(0));
        ScriptValue hints(exec->globalData(), exec->argument(1));
        impl()->inspectImpl(objectId.toInspectorValue(exec), hints.toInspectorValue(exec));
    }
    return jsUndefined();
}

JSValue JSInjectedScriptHost::databaseId(ExecState* exec)
{
    if (exec->argumentCount() < 1)
        return jsUndefined();
#if ENABLE(DATABASE)
    Database* database = toDatabase(exec->argument(0));
    if (database)
        return jsNumber(impl()->databaseIdImpl(database));
#endif
    return jsUndefined();
}

JSValue JSInjectedScriptHost::storageId(ExecState* exec)
{
    if (exec->argumentCount() < 1)
        return jsUndefined();
#if ENABLE(DOM_STORAGE)
    Storage* storage = toStorage(exec->argument(0));
    if (storage)
        return jsNumber(impl()->storageIdImpl(storage));
#endif
    return jsUndefined();
}

InjectedScript InjectedScriptHost::injectedScriptFor(ScriptState* scriptState)
{
    JSLock lock(SilenceAssertionsOnly);
    JSDOMGlobalObject* globalObject = static_cast<JSDOMGlobalObject*>(scriptState->lexicalGlobalObject());
    JSObject* injectedScript = globalObject->injectedScript();
    if (injectedScript)
        return InjectedScript(ScriptObject(scriptState, injectedScript));

    if (!canAccessInspectedWindow(scriptState))
        return InjectedScript();

    pair<long, ScriptObject> injectedScriptObject = injectScript(injectedScriptSource(), scriptState);
    globalObject->setInjectedScript(injectedScriptObject.second.jsObject());
    InjectedScript result(injectedScriptObject.second);
    m_idToInjectedScript.set(injectedScriptObject.first, result);
    return result;
}

bool InjectedScriptHost::canAccessInspectedWindow(ScriptState* scriptState)
{
    JSLock lock(SilenceAssertionsOnly);
    JSDOMWindow* inspectedWindow = toJSDOMWindow(scriptState->lexicalGlobalObject());
    if (!inspectedWindow)
        return false;
    return inspectedWindow->allowsAccessFromNoErrorMessage(scriptState);
}

} // namespace WebCore

#endif // ENABLE(INSPECTOR)
