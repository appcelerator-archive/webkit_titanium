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

#include "config.h"
#include "InspectorController.h"

#if ENABLE(INSPECTOR)

#include "Frame.h"
#include "GraphicsContext.h"
#include "InjectedScriptHost.h"
#include "InspectorAgent.h"
#include "InspectorBackendDispatcher.h"
#include "InspectorBrowserDebuggerAgent.h"
#include "InspectorDebuggerAgent.h"
#include "InspectorClient.h"
#include "InspectorFrontend.h"
#include "InspectorFrontendClient.h"
#include "InspectorInstrumentation.h"
#include "InspectorTimelineAgent.h"
#include "Page.h"
#include "ScriptObject.h"
#include "Settings.h"

namespace WebCore {

InspectorController::InspectorController(Page* page, InspectorClient* inspectorClient)
    : m_inspectorAgent(new InspectorAgent(page, inspectorClient))
    , m_inspectorBackendDispatcher(new InspectorBackendDispatcher(m_inspectorAgent.get()))
    , m_inspectorClient(inspectorClient)
    , m_openingFrontend(false)
{
}

InspectorController::~InspectorController()
{
}

void InspectorController::setInspectorFrontendClient(PassOwnPtr<InspectorFrontendClient> inspectorFrontendClient)
{
    m_inspectorFrontendClient = inspectorFrontendClient;
}

bool InspectorController::hasInspectorFrontendClient() const
{
    return m_inspectorFrontendClient;
}

void InspectorController::didClearWindowObjectInWorld(Frame* frame, DOMWrapperWorld* world)
{
    if (world != mainThreadNormalWorld())
        return;

    // If the page is supposed to serve as InspectorFrontend notify inspector frontend
    // client that it's cleared so that the client can expose inspector bindings.
    if (m_inspectorFrontendClient && frame == m_inspectorAgent->inspectedPage()->mainFrame())
        m_inspectorFrontendClient->windowObjectCleared();
}

void InspectorController::startTimelineProfiler()
{
    ErrorString error;
    m_inspectorAgent->timelineAgent()->start(&error);
}

void InspectorController::stopTimelineProfiler()
{
    ErrorString error;
    m_inspectorAgent->timelineAgent()->stop(&error);
}

void InspectorController::connectFrontend()
{
    m_openingFrontend = false;
    m_inspectorFrontend = new InspectorFrontend(m_inspectorClient);
    m_inspectorAgent->setFrontend(m_inspectorFrontend.get());

    if (!InspectorInstrumentation::hasFrontends())
        ScriptController::setCaptureCallStackForUncaughtExceptions(true);
    InspectorInstrumentation::frontendCreated();
}

void InspectorController::disconnectFrontend()
{
    if (!m_inspectorFrontend)
        return;

    m_inspectorAgent->disconnectFrontend();

    m_inspectorFrontend.clear();

    InspectorInstrumentation::frontendDeleted();
    if (!InspectorInstrumentation::hasFrontends())
        ScriptController::setCaptureCallStackForUncaughtExceptions(false);

}

void InspectorController::show()
{
    if (!enabled())
        return;

    if (m_openingFrontend)
        return;

    if (m_inspectorFrontend)
        m_inspectorFrontend->inspector()->bringToFront();
    else {
        m_openingFrontend = true;
        m_inspectorClient->openInspectorFrontend(this);
    }
}

void InspectorController::close()
{
    if (!m_inspectorFrontend)
        return;
    m_inspectorFrontend->inspector()->disconnectFromBackend();
    disconnectFrontend();
}

void InspectorController::restoreInspectorStateFromCookie(const String& inspectorStateCookie)
{
    ASSERT(!m_inspectorFrontend);
    connectFrontend();
    m_inspectorAgent->restoreInspectorStateFromCookie(inspectorStateCookie);
}

void InspectorController::evaluateForTestInFrontend(long callId, const String& script)
{
    m_inspectorAgent->evaluateForTestInFrontend(callId, script);
}

void InspectorController::drawNodeHighlight(GraphicsContext& context) const
{
    m_inspectorAgent->drawNodeHighlight(context);
}

void InspectorController::showConsole()
{
    if (!enabled())
        return;
    show();
    m_inspectorAgent->showConsole();
}

void InspectorController::inspect(Node* node)
{
    if (!enabled())
        return;

    show();

    m_inspectorAgent->inspect(node);
}

bool InspectorController::enabled() const
{
    return m_inspectorAgent->enabled();
}

Page* InspectorController::inspectedPage() const
{
    return m_inspectorAgent->inspectedPage();
}

bool InspectorController::timelineProfilerEnabled()
{
    return m_inspectorAgent->timelineAgent()->started();
}

void InspectorController::setInspectorExtensionAPI(const String& source)
{
    m_inspectorAgent->setInspectorExtensionAPI(source);
}

void InspectorController::dispatchMessageFromFrontend(const String& message)
{
    m_inspectorBackendDispatcher->dispatch(message);
}

void InspectorController::hideHighlight()
{
    ErrorString error;
    m_inspectorAgent->hideHighlight(&error);
}

#if ENABLE(JAVASCRIPT_DEBUGGER)
void InspectorController::enableProfiler()
{
    ErrorString error;
    m_inspectorAgent->enableProfiler(&error);
}

void InspectorController::disableProfiler()
{
    ErrorString error;
    m_inspectorAgent->disableProfiler(&error);
}

bool InspectorController::profilerEnabled()
{
    return m_inspectorAgent->profilerEnabled();
}

bool InspectorController::debuggerEnabled()
{
    return m_inspectorAgent->debuggerAgent()->enabled();
}

void InspectorController::showAndEnableDebugger()
{
    if (!enabled())
        return;
    show();
    m_inspectorAgent->showScriptsPanel();
    m_inspectorAgent->debuggerAgent()->startUserInitiatedDebugging();
}

void InspectorController::disableDebugger()
{
    m_inspectorAgent->debuggerAgent()->disable();
}

void InspectorController::startUserInitiatedProfiling()
{
    m_inspectorAgent->startUserInitiatedProfiling();
}

void InspectorController::stopUserInitiatedProfiling()
{
    if (!enabled())
        return;
    show();
    m_inspectorAgent->stopUserInitiatedProfiling();
}

bool InspectorController::isRecordingUserInitiatedProfile() const
{
    return m_inspectorAgent->isRecordingUserInitiatedProfile();
}

void InspectorController::resume()
{
    if (InspectorDebuggerAgent* debuggerAgent = m_inspectorAgent->debuggerAgent()) {
        ErrorString error;
        debuggerAgent->resume(&error);
    }
}

#endif

} // namespace WebCore

#endif // ENABLE(INSPECTOR)
