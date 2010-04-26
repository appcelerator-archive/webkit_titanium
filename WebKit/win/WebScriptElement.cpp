#include "config.h"
#include <WebCore/StringHash.h>
#include <wtf/Vector.h>
#include <WebCore/ScriptElement.h>
#include "WebScriptElement.h"

void STDMETHODCALLTYPE addScriptEvaluator(IWebScriptEvaluator *evaluator) {
    WebCore::ScriptElement::addScriptEvaluator(new EvaluatorAdapter(evaluator));
}

