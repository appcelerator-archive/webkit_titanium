/*
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
 *  Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "ScriptController.h"

#include "Frame.h"
#include "FrameLoaderClient.h"
#include "Page.h"
#include "ScriptEvaluator.h"
#include "ScriptSourceCode.h"
#include "ScriptValue.h"
#include "Settings.h"
#include "XSSAuditor.h"

namespace WebCore {

bool ScriptController::canExecuteScripts(ReasonForCallingCanExecuteScripts reason)
{
    // FIXME: We should get this information from the document instead of the frame.
    if (m_frame->loader()->isSandboxed(SandboxScripts))
        return false;

    Settings* settings = m_frame->settings();
    const bool allowed = m_frame->loader()->client()->allowJavaScript(settings && settings->isJavaScriptEnabled());
    if (!allowed && reason == AboutToExecuteScript)
        m_frame->loader()->client()->didNotAllowScript();
    return allowed;
}

ScriptValue ScriptController::executeScript(const String& script, bool forceUserGesture)
{
    return executeScript(ScriptSourceCode(script, forceUserGesture ? KURL() : m_frame->loader()->url()));
}

ScriptValue ScriptController::executeScript(const ScriptSourceCode& sourceCode)
{
    if (!canExecuteScripts(AboutToExecuteScript) || isPaused())
        return ScriptValue();

    bool wasInExecuteScript = m_inExecuteScript;
    m_inExecuteScript = true;

    ScriptValue result = evaluate(sourceCode);

    if (!wasInExecuteScript) {
        m_inExecuteScript = false;
        Document::updateStyleForAllDocuments();
    }

    return result;
}

ScriptValue ScriptController::executeScript(const ScriptSourceCode& sourceCode, const String& mimeType, ScriptEvaluator *evaluator)
{
    if (!canExecuteScripts(AboutToExecuteScript) || m_frame->script()->isPaused())
        return ScriptValue();

    if (!evaluator || mimeType.length() == 0)
        return executeScript(sourceCode);

    bool wasRunningScript = m_inExecuteScript;
    m_inExecuteScript = true;

    // FIXME, we should eventually pull this from the ScriptEvaluator
    evaluator->evaluate(mimeType, sourceCode,
       windowShell(mainThreadNormalWorld())->window()->globalExec());

    if (!wasRunningScript) {
        m_inExecuteScript = false;
        Document::updateStyleForAllDocuments();
    }

    return ScriptValue();
}

bool ScriptController::executeIfJavaScriptURL(const KURL& url, bool userGesture, bool replaceDocument)
{
    if (!protocolIsJavaScript(url))
        return false;

    if (m_frame->page() && !m_frame->page()->javaScriptURLsAreAllowed())
        return true;

    if (m_frame->inViewSourceMode())
        return true;

    const int javascriptSchemeLength = sizeof("javascript:") - 1;

    String script = decodeURLEscapeSequences(url.string().substring(javascriptSchemeLength));
    ScriptValue result;
    if (xssAuditor()->canEvaluateJavaScriptURL(script))
        result = executeScript(script, userGesture);

    String scriptResult;
#if USE(JSC)
    JSDOMWindowShell* shell = windowShell(mainThreadNormalWorld());
    JSC::ExecState* exec = shell->window()->globalExec();
    if (!result.getString(exec, scriptResult))
        return true;
#else
    if (!result.getString(scriptResult))
        return true;
#endif

    // FIXME: We should always replace the document, but doing so
    //        synchronously can cause crashes:
    //        http://bugs.webkit.org/show_bug.cgi?id=16782
    if (replaceDocument) 
        m_frame->loader()->replaceDocument(scriptResult);

    return true;
}

} // namespace WebCore
