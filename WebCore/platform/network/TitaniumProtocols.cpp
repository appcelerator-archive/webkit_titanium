/**
 * Appcelerator WebKit - licensed under the Apache Public License 2
 * see LICENSE in the root folder for details on the license.
 * Copyright (c) 2009 Appcelerator, Inc. All Rights Reserved.
 */

#include "config.h"
#include "Base64.h"
#include <wtf/text/CString.h>
#include "HTTPParsers.h"
#include "MIMETypeRegistry.h"
#include "NotImplemented.h"
#include "ResourceError.h"
#include "ResourceHandle.h"
#include "ResourceHandleInternal.h"
#include "TextEncoding.h"
#include "TitaniumProtocols.h"
#include <assert.h>

namespace WebCore {

NormalizeURLCallback TitaniumProtocols::NormalizeCallback = 0;
URLToPathCallback TitaniumProtocols::URLCallback = 0;
CanPreprocessURLCallback TitaniumProtocols::CanPreprocessCallback = 0;
PreprocessURLCallback TitaniumProtocols::PreprocessCallback = 0;
ProxyForURLCallback TitaniumProtocols::ProxyCallback = 0;

/*static*/
KURL TitaniumProtocols::NormalizeURL(KURL url)
{
    assert(NormalizeCallback != 0);

    // If you are using a URL in a Titanium application that is longer
    // than 4KB, either you are from the future or you are doing something
    // artistic or just wrong.
    char* buffer = new char[4096];
    NormalizeCallback(url.string().utf8().data(), buffer, 4096);
    KURL normalizedURL(KURL(), String::fromUTF8(buffer));
    delete [] buffer;

    return normalizedURL;
}

/*static*/
KURL TitaniumProtocols::URLToFileURL(KURL url)
{
    assert(URLCallback != 0);

    // If you are using a URL in a Titanium application that is longer
    // than 4KB, either you are from the future or you are doing something
    // artistic or just wrong.
    char* buffer = new char[4096];
    URLCallback(url.string().utf8().data(), buffer, 4096);
    KURL fileURL(KURL(), String::fromUTF8(buffer));
    delete [] buffer;

    return fileURL;
}

/*static*/
bool TitaniumProtocols::CanPreprocess(const ResourceRequest& request)
{
    return CanPreprocessCallback(request.url().string().utf8().data());
}

/*static*/
String TitaniumProtocols::Preprocess(const ResourceRequest& request, String& mimeType)
{
    HTTPHeaderMap headerMap = request.httpHeaderFields();
    KeyValuePair* headers = new KeyValuePair[headerMap.size() + 1];
    KeyValuePair* headerPointer = headers;

    HTTPHeaderMap::const_iterator end = headerMap.end();
    for (HTTPHeaderMap::const_iterator it = headerMap.begin(); it != end; ++it) {
        headerPointer->key = strdup(it->first.string().utf8().data());
        headerPointer->value = strdup(it->second.utf8().data());
        headerPointer++;
    }
    headerPointer->key = headerPointer->value = 0;

    char* cmimeType = 0;
    char* data = PreprocessCallback(request.url().string().utf8().data(), headers, &cmimeType);

    headerPointer = headers;
    while (headerPointer->key)
    {
        free(headerPointer->key);
        free(headerPointer->value);
        headerPointer++;
    }

    delete [] headers;

    mimeType = String::fromUTF8(cmimeType);
    String result(String::fromUTF8(data));

    free(data);
    free(cmimeType);

    return result;
}

/*static*/
String TitaniumProtocols::ProxyForURL(String& url)
{
    if (!ProxyCallback)
        return String("direct://");

    char* buffer = new char[4096];
    ProxyCallback(url.utf8().data(), buffer, 4096);
    String proxy = String::fromUTF8(buffer);
    delete [] buffer;

    return proxy;
}

static String cookieJarFilename;

/*static*/
void TitaniumProtocols::SetCookieJarFilename(const char* filename)
{
    cookieJarFilename = String::fromUTF8(filename);
}

/*static*/
String TitaniumProtocols::GetCookieJarFilename()
{
    return cookieJarFilename;
}

}

