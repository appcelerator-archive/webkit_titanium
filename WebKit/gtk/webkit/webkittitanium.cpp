/**
 * Appcelerator WebKit - licensed under the Apache Public License 2
 * see LICENSE in the root folder for details on the license.
 * Copyright (c) 2009 Appcelerator, Inc. All Rights Reserved.
 */

#include "config.h"
#include "webkittitanium.h"

#include "webkitprivate.h"

#include "StringHash.h"
#include <wtf/Vector.h>
#include "ScriptSourceCode.h"
#include "ScriptEvaluator.h"
#include "ScriptElement.h"
#include <wtf/text/CString.h>
#include "FrameLoaderClientGtk.h"
#include "InspectorClientGtk.h"
#include "TitaniumProtocols.h"

namespace WebCore {
    class String;
    class ScriptSourceCode;
}

void webkit_titanium_set_normalize_url_cb(NormalizeURLCallback cb) {
    WebCore::TitaniumProtocols::NormalizeCallback = cb;
}

void webkit_titanium_set_url_to_file_url_cb(URLToFileURLCallback cb) {
    WebCore::TitaniumProtocols::URLCallback = cb;
}

void webkit_titanium_set_can_preprocess_cb(CanPreprocessURLCallback cb) {
    WebCore::TitaniumProtocols::CanPreprocessCallback = cb;
}

void webkit_titanium_set_preprocess_cb(PreprocessURLCallback cb) {
    WebCore::TitaniumProtocols::PreprocessCallback = cb;
}

void webkit_titanium_set_inspector_url(const gchar* url) {
   if (WebKit::CustomGtkWebInspectorPath)
       free(WebKit::CustomGtkWebInspectorPath);
   WebKit::CustomGtkWebInspectorPath = strdup(url);
}

class EvaluatorAdapter : public WebCore::ScriptEvaluator {
    protected:
        WebKitWebScriptEvaluator *evaluator;

    public:
        EvaluatorAdapter(WebKitWebScriptEvaluator *evaluator) 
        : evaluator(evaluator)
        {
        }

        bool matchesMimeType(const WebCore::String &mimeType) {
            return evaluator->matchesMimeType(mimeType.utf8().data());
        }

        void evaluate(const WebCore::String &mimeType, const WebCore::ScriptSourceCode& sourceCode, void *context) {
            evaluator->evaluate(mimeType.utf8().data(), sourceCode.jsSourceCode().toString().ascii(), context);

        }
};

void webkit_titanium_add_script_evaluator(WebKitWebScriptEvaluator *evaluator) {
    WebCore::ScriptElement::addScriptEvaluator(new EvaluatorAdapter(evaluator));
}


