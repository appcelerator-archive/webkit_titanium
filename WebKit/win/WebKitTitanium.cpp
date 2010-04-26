/**
 * Appcelerator WebKit - licensed under the Apache Public License 2
 * see LICENSE in the root folder for details on the license.
 * Copyright (c) 2009 Appcelerator, Inc. All Rights Reserved.
 */

#include "config.h"
#include "WebKit.h"
#include <WebCore/StringHash.h>
#include <wtf/Vector.h>
#include <WebCore/ScriptElement.h>
#include "WebCoreSupport/WebFrameLoaderClient.h"
#include "TitaniumProtocols.h"
#include "WebKitTitanium.h"
#include "WebMutableURLRequest.h"

void setNormalizeURLCallback(NormalizeURLCallback cb)
{
    WebCore::TitaniumProtocols::NormalizeCallback = cb;
}

void setURLToFileURLCallback(URLToFileURLCallback cb)
{
    WebCore::TitaniumProtocols::URLCallback = cb;
}

void setCanPreprocessCallback(CanPreprocessURLCallback cb)
{
    WebCore::TitaniumProtocols::CanPreprocessCallback = cb;
}

void setPreprocessCallback(PreprocessURLCallback cb)
{
    WebCore::TitaniumProtocols::PreprocessCallback = cb;
}

void setProxyCallback(ProxyForURLCallback cb)
{
    WebCore::TitaniumProtocols::ProxyCallback = cb;
}

