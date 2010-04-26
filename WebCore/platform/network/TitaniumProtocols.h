/**
 * Appcelerator WebKit - licensed under the Apache Public License 2
 * see LICENSE in the root folder for details on the license.
 * Copyright (c) 2009 Appcelerator, Inc. All Rights Reserved.
 */

#ifndef TitaniumProtocols_h
#define TitaniumProtocols_h

#include "KURL.h"

#ifndef KEYVALUESTRUCT
typedef struct {
    char* key;
    char* value;
} KeyValuePair;
#define KEYVALUESTRUCT 1
#endif

typedef void(*NormalizeURLCallback)(const char*, char*, int);
typedef void(*URLToPathCallback)(const char*, char*, int);
typedef int(*CanPreprocessURLCallback)(const char*);
typedef char*(*PreprocessURLCallback)(const char* url, KeyValuePair* headers, char** mimeType);
typedef void(*ProxyForURLCallback)(const char*, char*, int);

namespace WebCore {

    class TitaniumProtocols {
        public:
        static KURL NormalizeURL(KURL url);
        static KURL URLToFileURL(KURL url);
        static bool CanPreprocess(const ResourceRequest& request);
        static String Preprocess(const ResourceRequest& request, String& mimeType);
        static String ProxyForURL(String& url);

        static NormalizeURLCallback NormalizeCallback;
        static URLToPathCallback URLCallback;
        static CanPreprocessURLCallback CanPreprocessCallback;
        static PreprocessURLCallback PreprocessCallback;
        static ProxyForURLCallback ProxyCallback;
    };

} // namespace WebCore

#endif // TitaniumProtocols_h
