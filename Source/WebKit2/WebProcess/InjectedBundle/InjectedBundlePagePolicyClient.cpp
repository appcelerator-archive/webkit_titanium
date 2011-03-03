/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InjectedBundlePagePolicyClient.h"

#include "WKBundleAPICast.h"
#include "WebURLRequest.h"

using namespace WebCore;

namespace WebKit {

WKBundlePagePolicyAction InjectedBundlePagePolicyClient::decidePolicyForNavigationAction(WebPage* page, WebFrame* frame, InjectedBundleNavigationAction* action, const ResourceRequest& resourceRequest, RefPtr<APIObject>& userData)
{
    if (!m_client.decidePolicyForNavigationAction)
        return WKBundlePagePolicyActionPassThrough;

    RefPtr<WebURLRequest> request = WebURLRequest::create(resourceRequest);

    WKTypeRef userDataToPass = 0;
    WKBundlePagePolicyAction policy = m_client.decidePolicyForNavigationAction(toAPI(page), toAPI(frame), toAPI(action), toAPI(request.get()), &userDataToPass, m_client.clientInfo);
    userData = adoptRef(toImpl(userDataToPass));
    return policy;
}

WKBundlePagePolicyAction InjectedBundlePagePolicyClient::decidePolicyForNewWindowAction(WebPage* page, WebFrame* frame, InjectedBundleNavigationAction* action, const ResourceRequest& resourceRequest, const String& frameName, RefPtr<APIObject>& userData)
{
    if (!m_client.decidePolicyForNewWindowAction)
        return WKBundlePagePolicyActionPassThrough;

    RefPtr<WebURLRequest> request = WebURLRequest::create(resourceRequest);

    WKTypeRef userDataToPass = 0;
    WKBundlePagePolicyAction policy = m_client.decidePolicyForNewWindowAction(toAPI(page), toAPI(frame), toAPI(action), toAPI(request.get()), toAPI(frameName.impl()), &userDataToPass, m_client.clientInfo);
    userData = adoptRef(toImpl(userDataToPass));
    return policy;
}

WKBundlePagePolicyAction InjectedBundlePagePolicyClient::decidePolicyForMIMEType(WebPage* page, WebFrame* frame, const String& MIMEType, const ResourceRequest& resourceRequest, RefPtr<APIObject>& userData)
{
    if (!m_client.decidePolicyForMIMEType)
        return WKBundlePagePolicyActionPassThrough;

    RefPtr<WebURLRequest> request = WebURLRequest::create(resourceRequest);

    WKTypeRef userDataToPass = 0;
    WKBundlePagePolicyAction policy = m_client.decidePolicyForMIMEType(toAPI(page), toAPI(frame), toAPI(MIMEType.impl()), toAPI(request.get()), &userDataToPass, m_client.clientInfo);
    userData = adoptRef(toImpl(userDataToPass));
    return policy;
}

} // namespace WebKit
