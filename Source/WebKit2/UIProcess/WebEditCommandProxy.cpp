/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include "WebEditCommandProxy.h"

#include "WebPageMessages.h"
#include "WebPageProxy.h"
#include "WebProcessProxy.h"

using namespace WebCore;

namespace WebKit {

WebEditCommandProxy::WebEditCommandProxy(uint64_t commandID, WebCore::EditAction editAction, WebPageProxy* page)
    : m_commandID(commandID)
    , m_editAction(editAction)
    , m_page(page)
{
    m_page->addEditCommand(this);
}

WebEditCommandProxy::~WebEditCommandProxy()
{
    if (m_page)
        m_page->removeEditCommand(this);
}

void WebEditCommandProxy::unapply()
{
    if (!m_page || !m_page->isValid())
        return;

    m_page->process()->send(Messages::WebPage::UnapplyEditCommand(m_commandID), m_page->pageID());
    m_page->registerEditCommand(this, WebPageProxy::Redo);
}

void WebEditCommandProxy::reapply()
{
    if (!m_page || !m_page->isValid())
        return;

    m_page->process()->send(Messages::WebPage::ReapplyEditCommand(m_commandID), m_page->pageID());
    m_page->registerEditCommand(this, WebPageProxy::Undo);
}

} // namespace WebKit
