/**
 * Appcelerator WebKit - licensed under the Apache Public License 2
 * see LICENSE in the root folder for details on the license.
 * Copyright (c) 2009 Appcelerator, Inc. All Rights Reserved.
 */

#ifndef WEBKIT_TITANIUM_H
#define WEBKIT_TITANIUM_H

#include <webkit/webkitdefines.h>

G_BEGIN_DECLS

#ifndef KEYVALUESTRUCT
typedef struct {
    char* key;
    char* value;
} KeyValuePair;
#define KEYVALUESTRUCT 1
#endif

typedef void(*NormalizeURLCallback)(const char*, char*, int);
typedef void(*URLToFileURLCallback)(const char*, char*, int);
typedef int(*CanPreprocessURLCallback)(const char*);
typedef char*(*PreprocessURLCallback)(const char* uri, KeyValuePair* headers, char** mimeType);

class WEBKIT_API WebKitWebScriptEvaluator {
    public:
        virtual bool matchesMimeType(const gchar * mimeType) = 0;
        virtual void evaluate(const gchar *mimeType, const gchar *sourceCode, void*) = 0;
};

WEBKIT_API void
webkit_titanium_add_script_evaluator                     (WebKitWebScriptEvaluator *evaluator);

WEBKIT_API void
webkit_titanium_set_normalize_url_cb                     (NormalizeURLCallback cb);

WEBKIT_API void
webkit_titanium_set_url_to_file_url_cb                   (URLToFileURLCallback cb);

WEBKIT_API void
webkit_titanium_set_url_to_file_url_cb                   (URLToFileURLCallback cb);

WEBKIT_API void
webkit_titanium_set_can_preprocess_cb                    (CanPreprocessURLCallback cb);

WEBKIT_API void
webkit_titanium_set_preprocess_cb                        (PreprocessURLCallback cb);

WEBKIT_API void
webkit_titanium_set_inspector_url                        (const gchar* url);

G_END_DECLS

#endif

