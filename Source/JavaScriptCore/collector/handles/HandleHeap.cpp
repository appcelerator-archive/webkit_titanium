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

#include "HandleHeap.h"

#include "JSObject.h"

namespace JSC {

HandleHeap::HandleHeap(JSGlobalData* globalData)
    : m_globalData(globalData)
    , m_nextToFinalize(0)
#if !ASSERT_DISABLED
    , m_handlingFinalizers(false)
#endif
{
    grow();
}

void HandleHeap::grow()
{
    Node* block = m_blockStack.grow();
    for (int i = m_blockStack.blockLength - 1; i >= 0; --i) {
        Node* node = &block[i];
        new (node) Node(this);
        m_freeList.push(node);
    }
}

void HandleHeap::markStrongHandles(HeapRootMarker& heapRootMarker)
{
    Node* end = m_strongList.end();
    for (Node* node = m_strongList.begin(); node != end; node = node->next())
        heapRootMarker.mark(node->slot());
}

void HandleHeap::updateAfterMark()
{
    clearWeakPointers();
}

void HandleHeap::clearWeakPointers()
{
#if !ASSERT_DISABLED
    m_handlingFinalizers = true;
#endif

    Node* end = m_weakList.end();
    for (Node* node = m_weakList.begin(); node != end; node = m_nextToFinalize) {
        m_nextToFinalize = node->next();

        JSValue value = *node->slot();
        if (!value || !value.isCell())
            continue;
        
        JSCell* cell = value.asCell();
        ASSERT(!cell || cell->structure());
        
#if ENABLE(JSC_ZOMBIES)
        ASSERT(!cell->isZombie());
#endif
        if (Heap::isMarked(cell))
            continue;

        m_nextToFinalize = node->next();
        if (Finalizer* finalizer = node->finalizer())
            finalizer->finalize(Handle<Unknown>::wrapSlot(node->slot()), node->finalizerContext());

        if (m_nextToFinalize != node->next()) // Finalizer deallocated node.
            continue;

        *node->slot() = JSValue();
        SentinelLinkedList<Node>::remove(node);
        m_immediateList.push(node);
    }

#if !ASSERT_DISABLED
    m_handlingFinalizers = false;
#endif
}

void HandleHeap::writeBarrier(HandleSlot slot, const JSValue& value)
{
    ASSERT(!m_handlingFinalizers);
    if (slot->isCell() == value.isCell() && !value == !*slot)
        return;
    Node* node = toNode(slot);
    SentinelLinkedList<Node>::remove(node);
    if (!value.isCell() || !value) {
        m_immediateList.push(node);
        return;
    }
    if (node->isWeak())
        m_weakList.push(node);
    else
        m_strongList.push(node);
}

unsigned HandleHeap::protectedGlobalObjectCount()
{
    unsigned count = 0;
    Node* end = m_strongList.end();
    for (Node* node = m_strongList.begin(); node != end; node = node->next()) {
        JSValue value = *node->slot();
        if (value.isObject() && asObject(value.asCell())->isGlobalObject())
            count++;
    }
    return count;
}

} // namespace JSC
