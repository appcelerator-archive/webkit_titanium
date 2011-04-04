/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 * Copyright (C) 2011 Google Inc. All Rights Reserved.
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
 *
 */

#include "config.h"

#if ENABLE(WORKERS)

#include "WorkerScriptController.h"

#include "JSDedicatedWorkerContext.h"
#include "JSSharedWorkerContext.h"
#include "ScriptSourceCode.h"
#include "ScriptValue.h"
#include "WebCoreJSClientData.h"
#include "WorkerContext.h"
#include "WorkerObjectProxy.h"
#include "WorkerThread.h"
#include <interpreter/Interpreter.h>
#include <runtime/Completion.h>
#include <runtime/Completion.h>
#include <runtime/Error.h>
#include <runtime/JSLock.h>

using namespace JSC;

namespace WebCore {

WorkerScriptController::WorkerScriptController(WorkerContext* workerContext)
    : m_globalData(JSGlobalData::create(ThreadStackTypeSmall))
    , m_workerContext(workerContext)
    , m_workerContextWrapper(*m_globalData)
    , m_executionForbidden(false)
{
    initNormalWorldClientData(m_globalData.get());
}

WorkerScriptController::~WorkerScriptController()
{
    m_workerContextWrapper.clear(); // Unprotect the global object.
    m_globalData->heap.destroy();
}

void WorkerScriptController::initScript()
{
    ASSERT(!m_workerContextWrapper);

    JSLock lock(SilenceAssertionsOnly);

    // Explicitly protect the global object's prototype so it isn't collected
    // when we allocate the global object. (Once the global object is fully
    // constructed, it can mark its own prototype.)
    RefPtr<Structure> workerContextPrototypeStructure = JSWorkerContextPrototype::createStructure(*m_globalData, jsNull());
    Global<JSWorkerContextPrototype> workerContextPrototype(*m_globalData, new (m_globalData.get()) JSWorkerContextPrototype(0, workerContextPrototypeStructure.release()));

    if (m_workerContext->isDedicatedWorkerContext()) {
        RefPtr<Structure> dedicatedContextPrototypeStructure = JSDedicatedWorkerContextPrototype::createStructure(*m_globalData, workerContextPrototype.get());
        Global<JSDedicatedWorkerContextPrototype> dedicatedContextPrototype(*m_globalData, new (m_globalData.get()) JSDedicatedWorkerContextPrototype(0, dedicatedContextPrototypeStructure.release()));
        RefPtr<Structure> structure = JSDedicatedWorkerContext::createStructure(*m_globalData, dedicatedContextPrototype.get());

        m_workerContextWrapper.set(*m_globalData, new (m_globalData.get()) JSDedicatedWorkerContext(structure.release(), m_workerContext->toDedicatedWorkerContext()));
        workerContextPrototype->putAnonymousValue(*m_globalData, 0, m_workerContextWrapper.get());
        dedicatedContextPrototype->putAnonymousValue(*m_globalData, 0, m_workerContextWrapper.get());
#if ENABLE(SHARED_WORKERS)
    } else {
        ASSERT(m_workerContext->isSharedWorkerContext());
        RefPtr<Structure> sharedContextPrototypeStructure = JSSharedWorkerContextPrototype::createStructure(*m_globalData, workerContextPrototype.get());
        Global<JSSharedWorkerContextPrototype> sharedContextPrototype(*m_globalData, new (m_globalData.get()) JSSharedWorkerContextPrototype(0, sharedContextPrototypeStructure.release()));
        RefPtr<Structure> structure = JSSharedWorkerContext::createStructure(*m_globalData, sharedContextPrototype.get());

        m_workerContextWrapper.set(*m_globalData, new (m_globalData.get()) JSSharedWorkerContext(structure.release(), m_workerContext->toSharedWorkerContext()));
        workerContextPrototype->putAnonymousValue(*m_globalData, 0, m_workerContextWrapper.get());
        sharedContextPrototype->putAnonymousValue(*m_globalData, 0, m_workerContextWrapper.get());
#endif
    }
}

ScriptValue WorkerScriptController::evaluate(const ScriptSourceCode& sourceCode)
{
    {
        MutexLocker lock(m_sharedDataMutex);
        if (m_executionForbidden)
            return ScriptValue();
    }
    ScriptValue exception;
    ScriptValue result(evaluate(sourceCode, &exception));
    if (exception.jsValue()) {
        JSLock lock(SilenceAssertionsOnly);
        reportException(m_workerContextWrapper->globalExec(), exception.jsValue());
    }
    return result;
}

ScriptValue WorkerScriptController::evaluate(const ScriptSourceCode& sourceCode, ScriptValue* exception)
{
    {
        MutexLocker lock(m_sharedDataMutex);
        if (m_executionForbidden)
            return ScriptValue();
    }

    initScriptIfNeeded();
    JSLock lock(SilenceAssertionsOnly);

    ExecState* exec = m_workerContextWrapper->globalExec();
    m_workerContextWrapper->globalData().timeoutChecker.start();
    Completion comp = JSC::evaluate(exec, exec->dynamicGlobalObject()->globalScopeChain(), sourceCode.jsSourceCode(), m_workerContextWrapper.get());
    m_workerContextWrapper->globalData().timeoutChecker.stop();

    if (comp.complType() == Normal || comp.complType() == ReturnValue)
        return ScriptValue(*m_globalData, comp.value());

    if (comp.complType() == Throw) {
        String errorMessage;
        int lineNumber = 0;
        String sourceURL = sourceCode.url().string();
        if (m_workerContext->sanitizeScriptError(errorMessage, lineNumber, sourceURL))
            *exception = ScriptValue(*m_globalData, throwError(exec, createError(exec, errorMessage.impl())));
        else
            *exception = ScriptValue(*m_globalData, comp.value());
    }
    return ScriptValue();
}

void WorkerScriptController::setException(ScriptValue exception)
{
    throwError(m_workerContextWrapper->globalExec(), exception.jsValue());
}

void WorkerScriptController::forbidExecution(ForbidExecutionOption option)
{
    // This function may be called from another thread.
    // Mutex protection for m_executionForbidden is needed to guarantee that the value is synchronized between processors, because
    // if it were not, the worker could re-enter JSC::evaluate(), but with timeout already reset.
    // It is not critical for Terminator::m_shouldTerminate to be synchronized, we just rely on it reaching the worker thread's processor sooner or later.
    MutexLocker lock(m_sharedDataMutex);
    m_executionForbidden = true;
    if (option == TerminateRunningScript)
        m_globalData->terminator.terminateSoon();
}

} // namespace WebCore

#endif // ENABLE(WORKERS)
