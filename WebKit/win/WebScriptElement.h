#ifndef WebScriptElement_h
#define WebScriptElement_h

#include "WebKit.h"
#include <WebCore/ScriptEvaluator.h>
#include <WebCore/ScriptElement.h>
#include <WebCore/ScriptSourceCode.h>
#include <WebCore/BString.h>
#include <JavaScriptCore/APICast.h>
#include <JavaScriptCore/JSContextRef.h>
#include "WebKitTitanium.h"

class EvaluatorAdapter : public WebCore::ScriptEvaluator {
public:
    EvaluatorAdapter(IWebScriptEvaluator *evaluator) : evaluator(evaluator) {
    }

    virtual bool matchesMimeType(const WebCore::String &mimeType) {
        BOOL returnVal;

        HRESULT hr = evaluator->matchesMimeType(WebCore::BString(mimeType), &returnVal);
        if (SUCCEEDED(hr)) {
            return returnVal;
        }
        return false;
    }

    virtual void evaluate(const WebCore::String &mimeType, const WebCore::ScriptSourceCode& sourceCode, void* context) {

        // a JSContextRef is just a masked JSC::ExecState
        JSC::ExecState *execState = static_cast<JSC::ExecState*>(context);
        JSContextRef contextRef = toRef(execState);
    
        // we use int* in Windows because void* isn't allowed in COM/IDL
        evaluator->evaluate(WebCore::BString(mimeType), WebCore::BString(sourceCode.jsSourceCode().toString()), (int*)contextRef);
    }

protected:
    IWebScriptEvaluator *evaluator;
};

#endif

