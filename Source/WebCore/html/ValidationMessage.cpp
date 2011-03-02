/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ValidationMessage.h"

#include "CSSStyleSelector.h"
#include "FormAssociatedElement.h"
#include "HTMLBRElement.h"
#include "HTMLNames.h"
#include "RenderObject.h"
#include "Text.h"
#include <wtf/PassOwnPtr.h>

namespace WebCore {

using namespace HTMLNames;

ALWAYS_INLINE ValidationMessage::ValidationMessage(FormAssociatedElement* element)
    : m_element(element)
{
}

ValidationMessage::~ValidationMessage()
{
    deleteBubbleTree();
}

PassOwnPtr<ValidationMessage> ValidationMessage::create(FormAssociatedElement* element)
{
    return adoptPtr(new ValidationMessage(element));
}

void ValidationMessage::setMessage(const String& message)
{
    // Don't modify the DOM tree in this context.
    // If so, an assertion in Node::isFocusable() fails.
    ASSERT(!message.isEmpty());
    m_message = message;
    if (!m_bubble)
        m_timer.set(new Timer<ValidationMessage>(this, &ValidationMessage::buildBubbleTree));
    else
        m_timer.set(new Timer<ValidationMessage>(this, &ValidationMessage::setMessageDOMAndStartTimer));
    m_timer->startOneShot(0);
}

void ValidationMessage::setMessageDOMAndStartTimer(Timer<ValidationMessage>*)
{
    ASSERT(m_bubbleMessage);
    m_bubbleMessage->removeAllChildren();
    Vector<String> lines;
    m_message.split('\n', lines);
    Document* doc = m_bubbleMessage->document();
    ExceptionCode ec = 0;
    for (unsigned i = 0; i < lines.size(); ++i) {
        if (i) {
            m_bubbleMessage->appendChild(HTMLBRElement::create(doc), ec);
            m_bubbleMessage->appendChild(Text::create(doc, lines[i]), ec);
        } else {
            RefPtr<HTMLElement> bold = HTMLElement::create(bTag, doc);
            bold->setInnerText(lines[i], ec);
            m_bubbleMessage->appendChild(bold.release(), ec);
        }
    }

    m_timer.set(new Timer<ValidationMessage>(this, &ValidationMessage::deleteBubbleTree));
    m_timer->startOneShot(max(5.0, m_message.length() / 20.0));
}

class ElementWithPseudoId : public HTMLElement {
public:
    static PassRefPtr<HTMLElement> create(Document* doc, const AtomicString& pseudoName)
    {
        return adoptRef(new ElementWithPseudoId(doc, pseudoName));
    }

protected:
    ElementWithPseudoId(Document* doc, const AtomicString& pseudoName)
        : HTMLElement(divTag, doc)
        , m_pseudoName(pseudoName) { };
    virtual const AtomicString& shadowPseudoId() const { return m_pseudoName; }

private:
    AtomicString m_pseudoName;
};

void ValidationMessage::buildBubbleTree(Timer<ValidationMessage>*)
{
    HTMLElement* host = toHTMLElement(m_element);
    Document* doc = host->document();
    m_bubble = ElementWithPseudoId::create(doc, "-webkit-validation-bubble");
    ExceptionCode ec = 0;
    // FIXME: We need a way to host multiple shadow roots in a single node, or
    // to inherit an existing shadow tree.
    if (host->shadowRoot())
        host->shadowRoot()->appendChild(m_bubble.get(), ec);
    else
        host->setShadowRoot(m_bubble);

    m_bubble->appendChild(ElementWithPseudoId::create(doc, "-webkit-validation-bubble-top-outer-arrow"), ec);
    m_bubble->appendChild(ElementWithPseudoId::create(doc, "-webkit-validation-bubble-top-inner-arrow"), ec);
    m_bubbleMessage = ElementWithPseudoId::create(doc, "-webkit-validation-bubble-message");
    m_bubble->appendChild(m_bubbleMessage, ec);

    setMessageDOMAndStartTimer();

    // FIXME: Use transition to show the bubble.

    // We don't need to adjust the bubble location. The default position is enough.
}

void ValidationMessage::requestToHideMessage()
{
    // We must not modify the DOM tree in this context by the same reason as setMessage().
    m_timer.set(new Timer<ValidationMessage>(this, &ValidationMessage::deleteBubbleTree));
    m_timer->startOneShot(0);
}

void ValidationMessage::deleteBubbleTree(Timer<ValidationMessage>*)
{
    if (m_bubble) {
        m_bubbleMessage = 0;
        HTMLElement* host = toHTMLElement(m_element);
        if (m_bubble->isShadowRoot())
            host->setShadowRoot(0);
        else {
            ExceptionCode ec;
            host->shadowRoot()->removeChild(m_bubble.get(), ec);
        }
        m_bubble = 0;
    }
    m_message = String();
}

} // namespace WebCore
