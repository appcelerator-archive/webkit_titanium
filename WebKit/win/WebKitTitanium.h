#ifndef WebKitTitanium_h
#define WebKitTitanium_h
#include <WebKit/WebKit.h>

#ifdef WEBKIT_EXPORTS
#define WEBKIT_API __declspec(dllexport)
#else
#define WEBKIT_API __declspec(dllimport)
#endif

#ifndef KEYVALUESTRUCT
typedef struct {
    char* key;
    char* value;
} KeyValuePair;
#define KEYVALUESTRUCT 1
#endif

#ifdef __cplusplus
extern "C" {
#endif
    WEBKIT_API typedef void(*NormalizeURLCallback)(const char*, char*, int);
    WEBKIT_API typedef void(*URLToFileURLCallback)(const char*, char*, int);
    WEBKIT_API typedef int(*CanPreprocessURLCallback)(const char*);
    WEBKIT_API typedef char*(*PreprocessURLCallback)(const char* uri, KeyValuePair* headers, char** mimeType);
    WEBKIT_API typedef void(*ProxyForURLCallback)(const char*, char*, int);
    WEBKIT_API void setNormalizeURLCallback(NormalizeURLCallback cb);
    WEBKIT_API void setURLToFileURLCallback(URLToFileURLCallback cb);
    WEBKIT_API void setCanPreprocessCallback(CanPreprocessURLCallback cb);
    WEBKIT_API void setPreprocessCallback(PreprocessURLCallback cb);
    WEBKIT_API void setProxyCallback(ProxyForURLCallback cb);
#ifdef __cplusplus
}
#endif

class IWebScriptEvaluator;
void STDMETHODCALLTYPE addScriptEvaluator(IWebScriptEvaluator *evaluator);
#endif
