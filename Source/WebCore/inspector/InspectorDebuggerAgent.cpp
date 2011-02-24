/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
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
#include "InspectorDebuggerAgent.h"

#if ENABLE(JAVASCRIPT_DEBUGGER) && ENABLE(INSPECTOR)
#include "InjectedScript.h"
#include "InjectedScriptHost.h"
#include "InspectorFrontend.h"
#include "InspectorState.h"
#include "InspectorValues.h"
#include "PlatformString.h"
#include "ScriptDebugServer.h"
#include <wtf/text/StringConcatenate.h>

namespace WebCore {

namespace DebuggerAgentState {
static const char javaScriptBreakpoints[] = "javaScriptBreakopints";
};

PassOwnPtr<InspectorDebuggerAgent> InspectorDebuggerAgent::create(InspectorAgent* inspectorAgent, InspectorFrontend* frontend, bool eraseStickyBreakpoints)
{
    OwnPtr<InspectorDebuggerAgent> agent = adoptPtr(new InspectorDebuggerAgent(inspectorAgent, frontend, eraseStickyBreakpoints));
    ScriptDebugServer::shared().clearBreakpoints();
    // FIXME(WK44513): breakpoints activated flag should be synchronized between all front-ends
    ScriptDebugServer::shared().setBreakpointsActivated(true);
    ScriptDebugServer::shared().addListener(agent.get(), inspectorAgent->inspectedPage());
    return agent.release();
}

InspectorDebuggerAgent::InspectorDebuggerAgent(InspectorAgent* inspectorAgent, InspectorFrontend* frontend, bool eraseStickyBreakpoints)
    : m_inspectorAgent(inspectorAgent)
    , m_frontend(frontend)
    , m_pausedScriptState(0)
    , m_javaScriptPauseScheduled(false)
{
    if (eraseStickyBreakpoints)
        inspectorAgent->state()->setObject(DebuggerAgentState::javaScriptBreakpoints, InspectorObject::create());
}

InspectorDebuggerAgent::~InspectorDebuggerAgent()
{
    ScriptDebugServer::shared().removeListener(this, m_inspectorAgent->inspectedPage());
    m_pausedScriptState = 0;
}

void InspectorDebuggerAgent::activateBreakpoints(ErrorString*)
{
    ScriptDebugServer::shared().activateBreakpoints();
}

void InspectorDebuggerAgent::deactivateBreakpoints(ErrorString*)
{
    ScriptDebugServer::shared().deactivateBreakpoints();
}

void InspectorDebuggerAgent::inspectedURLChanged(const String&)
{
    m_scripts.clear();
    m_breakpointIdToDebugServerBreakpointIds.clear();
}

void InspectorDebuggerAgent::setJavaScriptBreakpoint(ErrorString*, const String& url, int lineNumber, int columnNumber, const String& condition, bool enabled, String* outBreakpointId, RefPtr<InspectorArray>* locations)
{
    String breakpointId = makeString(url, ":", String::number(lineNumber), ":", String::number(columnNumber));
    RefPtr<InspectorObject> breakpointsCookie = m_inspectorAgent->state()->getObject(DebuggerAgentState::javaScriptBreakpoints);
    if (breakpointsCookie->find(breakpointId) != breakpointsCookie->end())
        return;
    RefPtr<InspectorObject> breakpointObject = InspectorObject::create();
    breakpointObject->setString("url", url);
    breakpointObject->setNumber("lineNumber", lineNumber);
    breakpointObject->setNumber("columnNumber", columnNumber);
    breakpointObject->setString("condition", condition);
    breakpointObject->setBoolean("enabled", enabled);
    breakpointsCookie->setObject(breakpointId, breakpointObject);
    m_inspectorAgent->state()->setObject(DebuggerAgentState::javaScriptBreakpoints, breakpointsCookie);

    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition, enabled);
    for (ScriptsMap::iterator it = m_scripts.begin(); it != m_scripts.end(); ++it) {
        if (it->second.url != url)
            continue;
        int actualLineNumber = 0, actualColumnNumber = 0;
        if (!resolveBreakpoint(breakpointId, it->first, breakpoint, &actualLineNumber, &actualColumnNumber))
            continue;
        RefPtr<InspectorObject> location = InspectorObject::create();
        location->setString("sourceID", it->first);
        location->setNumber("lineNumber", actualLineNumber);
        location->setNumber("columnNumber", actualColumnNumber);
        locations->get()->pushObject(location);
    }
    *outBreakpointId = breakpointId;
}

void InspectorDebuggerAgent::setJavaScriptBreakpointBySourceId(ErrorString*, const String& sourceId, int lineNumber, int columnNumber, const String& condition, bool enabled, String* outBreakpointId, int* actualLineNumber, int* actualColumnNumber)
{
    String breakpointId = makeString(sourceId, ":", String::number(lineNumber), ":", String::number(columnNumber));
    if (m_breakpointIdToDebugServerBreakpointIds.find(breakpointId) != m_breakpointIdToDebugServerBreakpointIds.end())
        return;
    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition, enabled);
    if (!resolveBreakpoint(breakpointId, sourceId, breakpoint, actualLineNumber, actualColumnNumber))
        return;
    *outBreakpointId = breakpointId;
}

void InspectorDebuggerAgent::removeJavaScriptBreakpoint(ErrorString*, const String& breakpointId)
{
    RefPtr<InspectorObject> breakpointsCookie = m_inspectorAgent->state()->getObject(DebuggerAgentState::javaScriptBreakpoints);
    breakpointsCookie->remove(breakpointId);
    m_inspectorAgent->state()->setObject(DebuggerAgentState::javaScriptBreakpoints, breakpointsCookie);

    BreakpointIdToDebugServerBreakpointIdsMap::iterator debugServerBreakpointIdsIterator = m_breakpointIdToDebugServerBreakpointIds.find(breakpointId);
    if (debugServerBreakpointIdsIterator == m_breakpointIdToDebugServerBreakpointIds.end())
        return;
    for (size_t i = 0; i < debugServerBreakpointIdsIterator->second.size(); ++i)
        ScriptDebugServer::shared().removeBreakpoint(debugServerBreakpointIdsIterator->second[i]);
    m_breakpointIdToDebugServerBreakpointIds.remove(debugServerBreakpointIdsIterator);
}

void InspectorDebuggerAgent::continueToLocation(ErrorString* error, const String& sourceId, int lineNumber, int columnNumber)
{
    if (!m_continueToLocationBreakpointId.isEmpty()) {
        ScriptDebugServer::shared().removeBreakpoint(m_continueToLocationBreakpointId);
        m_continueToLocationBreakpointId = "";
    }
    ScriptBreakpoint breakpoint(lineNumber, columnNumber, "", true);
    m_continueToLocationBreakpointId = ScriptDebugServer::shared().setBreakpoint(sourceId, breakpoint, &lineNumber, &columnNumber);
    resume(error);
}

bool InspectorDebuggerAgent::resolveBreakpoint(const String& breakpointId, const String& sourceId, const ScriptBreakpoint& breakpoint, int* actualLineNumber, int* actualColumnNumber)
{
    ScriptsMap::iterator scriptIterator = m_scripts.find(sourceId);
    if (scriptIterator == m_scripts.end())
        return false;
    Script& script = scriptIterator->second;
    if (breakpoint.lineNumber < script.lineOffset)
        return false;
    if (!script.linesCount) {
        script.linesCount = 1;
        for (size_t i = 0; i < script.data.length(); ++i) {
            if (script.data[i] == '\n')
                script.linesCount += 1;
        }
    }
    if (breakpoint.lineNumber >= script.lineOffset + script.linesCount)
        return false;

    String debugServerBreakpointId = ScriptDebugServer::shared().setBreakpoint(sourceId, breakpoint, actualLineNumber, actualColumnNumber);
    if (debugServerBreakpointId.isEmpty())
        return false;

    BreakpointIdToDebugServerBreakpointIdsMap::iterator debugServerBreakpointIdsIterator = m_breakpointIdToDebugServerBreakpointIds.find(breakpointId);
    if (debugServerBreakpointIdsIterator == m_breakpointIdToDebugServerBreakpointIds.end())
        debugServerBreakpointIdsIterator = m_breakpointIdToDebugServerBreakpointIds.set(breakpointId, Vector<String>()).first;
    debugServerBreakpointIdsIterator->second.append(debugServerBreakpointId);

    return true;
}

void InspectorDebuggerAgent::editScriptSource(ErrorString*, const String& sourceID, const String& newContent, bool* success, String* result, RefPtr<InspectorValue>* newCallFrames)
{
    if ((*success = ScriptDebugServer::shared().editScriptSource(sourceID, newContent, *result)))
        *newCallFrames = currentCallFrames();
}

void InspectorDebuggerAgent::getScriptSource(ErrorString*, const String& sourceID, String* scriptSource)
{
    *scriptSource = m_scripts.get(sourceID).data;
}

void InspectorDebuggerAgent::schedulePauseOnNextStatement(DebuggerEventType type, PassRefPtr<InspectorValue> data)
{
    if (m_javaScriptPauseScheduled)
        return;
    m_breakProgramDetails = InspectorObject::create();
    m_breakProgramDetails->setNumber("eventType", type);
    m_breakProgramDetails->setValue("eventData", data);
    ScriptDebugServer::shared().setPauseOnNextStatement(true);
}

void InspectorDebuggerAgent::cancelPauseOnNextStatement()
{
    if (m_javaScriptPauseScheduled)
        return;
    m_breakProgramDetails = 0;
    ScriptDebugServer::shared().setPauseOnNextStatement(false);
}

void InspectorDebuggerAgent::pause(ErrorString*)
{
    schedulePauseOnNextStatement(JavaScriptPauseEventType, InspectorObject::create());
    m_javaScriptPauseScheduled = true;
}

void InspectorDebuggerAgent::resume(ErrorString*)
{
    ScriptDebugServer::shared().continueProgram();
}

void InspectorDebuggerAgent::stepOver(ErrorString*)
{
    ScriptDebugServer::shared().stepOverStatement();
}

void InspectorDebuggerAgent::stepInto(ErrorString*)
{
    ScriptDebugServer::shared().stepIntoStatement();
}

void InspectorDebuggerAgent::stepOut(ErrorString*)
{
    ScriptDebugServer::shared().stepOutOfFunction();
}

void InspectorDebuggerAgent::setPauseOnExceptionsState(ErrorString*, long pauseState, long* newState)
{
    ScriptDebugServer::shared().setPauseOnExceptionsState(static_cast<ScriptDebugServer::PauseOnExceptionsState>(pauseState));
    *newState = ScriptDebugServer::shared().pauseOnExceptionsState();
}

void InspectorDebuggerAgent::evaluateOnCallFrame(ErrorString*, PassRefPtr<InspectorObject> callFrameId, const String& expression, const String& objectGroup, bool includeCommandLineAPI, RefPtr<InspectorValue>* result)
{
    InjectedScript injectedScript = m_inspectorAgent->injectedScriptHost()->injectedScriptForObjectId(callFrameId.get());
    if (!injectedScript.hasNoValue())
        injectedScript.evaluateOnCallFrame(callFrameId, expression, objectGroup, includeCommandLineAPI, result);
}

void InspectorDebuggerAgent::getCompletionsOnCallFrame(ErrorString*, PassRefPtr<InspectorObject> callFrameId, const String& expression, bool includeCommandLineAPI, RefPtr<InspectorValue>* result)
{
    InjectedScript injectedScript = m_inspectorAgent->injectedScriptHost()->injectedScriptForObjectId(callFrameId.get());
    if (!injectedScript.hasNoValue())
        injectedScript.getCompletionsOnCallFrame(callFrameId, expression, includeCommandLineAPI, result);
}

PassRefPtr<InspectorValue> InspectorDebuggerAgent::currentCallFrames()
{
    if (!m_pausedScriptState)
        return InspectorValue::null();
    InjectedScript injectedScript = m_inspectorAgent->injectedScriptHost()->injectedScriptFor(m_pausedScriptState);
    if (injectedScript.hasNoValue()) {
        ASSERT_NOT_REACHED();
        return InspectorValue::null();
    }
    return injectedScript.callFrames();
}

// JavaScriptDebugListener functions

void InspectorDebuggerAgent::didParseSource(const String& sourceID, const String& url, const String& data, int lineOffset, int columnOffset, ScriptWorldType worldType)
{
    // Don't send script content to the front end until it's really needed.
    m_frontend->parsedScriptSource(sourceID, url, lineOffset, columnOffset, data.length(), worldType);

    m_scripts.set(sourceID, Script(url, data, lineOffset, columnOffset));

    if (url.isEmpty())
        return;

    RefPtr<InspectorObject> breakpointsCookie = m_inspectorAgent->state()->getObject(DebuggerAgentState::javaScriptBreakpoints);
    for (InspectorObject::iterator it = breakpointsCookie->begin(); it != breakpointsCookie->end(); ++it) {
        RefPtr<InspectorObject> breakpointObject = it->second->asObject();
        String breakpointURL;
        breakpointObject->getString("url", &breakpointURL);
        if (breakpointURL != url)
            continue;
        ScriptBreakpoint breakpoint;
        breakpointObject->getNumber("lineNumber", &breakpoint.lineNumber);
        breakpointObject->getNumber("columnNumber", &breakpoint.columnNumber);
        breakpointObject->getString("condition", &breakpoint.condition);
        breakpointObject->getBoolean("enabled", &breakpoint.enabled);
        int actualLineNumber = 0, actualColumnNumber = 0;
        if (resolveBreakpoint(it->first, sourceID, breakpoint, &actualLineNumber, &actualColumnNumber))
            m_frontend->breakpointResolved(it->first, sourceID, actualLineNumber, actualColumnNumber);
    }
}

void InspectorDebuggerAgent::failedToParseSource(const String& url, const String& data, int firstLine, int errorLine, const String& errorMessage)
{
    m_frontend->failedToParseScriptSource(url, data, firstLine, errorLine, errorMessage);
}

void InspectorDebuggerAgent::didPause(ScriptState* scriptState)
{
    ASSERT(scriptState && !m_pausedScriptState);
    m_pausedScriptState = scriptState;

    if (!m_breakProgramDetails)
        m_breakProgramDetails = InspectorObject::create();
    m_breakProgramDetails->setValue("callFrames", currentCallFrames());

    m_frontend->pausedScript(m_breakProgramDetails);
    m_javaScriptPauseScheduled = false;

    if (!m_continueToLocationBreakpointId.isEmpty()) {
        ScriptDebugServer::shared().removeBreakpoint(m_continueToLocationBreakpointId);
        m_continueToLocationBreakpointId = "";
    }
}

void InspectorDebuggerAgent::didContinue()
{
    m_pausedScriptState = 0;
    m_breakProgramDetails = 0;
    m_frontend->resumedScript();
}

void InspectorDebuggerAgent::breakProgram(DebuggerEventType type, PassRefPtr<InspectorValue> data)
{
    m_breakProgramDetails = InspectorObject::create();
    m_breakProgramDetails->setNumber("eventType", type);
    m_breakProgramDetails->setValue("eventData", data);
    ScriptDebugServer::shared().breakProgram();
}

} // namespace WebCore

#endif // ENABLE(JAVASCRIPT_DEBUGGER) && ENABLE(INSPECTOR)
