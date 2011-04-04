/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2006, 2007, 2008, 2009, 2010 Apple Inc. All right reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef InlineIterator_h
#define InlineIterator_h

#include "BidiRun.h"
#include "RenderBlock.h"
#include "RenderText.h"
#include <wtf/AlwaysInline.h>
#include <wtf/StdLibExtras.h>

namespace WebCore {

class InlineIterator {
public:
    InlineIterator()
        : m_root(0)
        , m_obj(0)
        , m_pos(0)
        , m_nextBreakablePosition(-1)
    {
    }

    InlineIterator(RenderObject* root, RenderObject* o, unsigned p)
        : m_root(root)
        , m_obj(o)
        , m_pos(p)
        , m_nextBreakablePosition(-1)
    {
    }

    void clear() { moveTo(0, 0); }

    void moveToStartOf(RenderObject* object)
    {
        moveTo(object, 0);
    }

    void moveTo(RenderObject* object, unsigned offset, int nextBreak = -1)
    {
        m_obj = object;
        m_pos = offset;
        m_nextBreakablePosition = nextBreak;
    }

    RenderObject* root() const { return m_root; }

    void increment(InlineBidiResolver* = 0);
    bool atEnd() const;

    UChar current() const;
    ALWAYS_INLINE WTF::Unicode::Direction direction() const;

private:
    RenderObject* m_root;

    // FIXME: These should be private.
public:
    RenderObject* m_obj;
    unsigned m_pos;
    int m_nextBreakablePosition;
};

inline bool operator==(const InlineIterator& it1, const InlineIterator& it2)
{
    return it1.m_pos == it2.m_pos && it1.m_obj == it2.m_obj;
}

inline bool operator!=(const InlineIterator& it1, const InlineIterator& it2)
{
    return it1.m_pos != it2.m_pos || it1.m_obj != it2.m_obj;
}

static inline WTF::Unicode::Direction embedCharFromDirection(TextDirection dir, EUnicodeBidi unicodeBidi)
{
    using namespace WTF::Unicode;
    if (unicodeBidi == Embed)
        return dir == RTL ? RightToLeftEmbedding : LeftToRightEmbedding;
    return dir == RTL ? RightToLeftOverride : LeftToRightOverride;
}

static inline void notifyResolverEnteredObject(InlineBidiResolver* resolver, RenderObject* object)
{
    if (!resolver || !object || !object->isRenderInline())
        return;

    RenderStyle* style = object->style();
    EUnicodeBidi unicodeBidi = style->unicodeBidi();
    if (unicodeBidi == UBNormal)
        return;
    resolver->embed(embedCharFromDirection(style->direction(), unicodeBidi), FromStyleOrDOM);
}

static inline void notifyResolverWillExitObject(InlineBidiResolver* resolver, RenderObject* object)
{
    if (!resolver || !object || !object->isRenderInline())
        return;
    if (object->style()->unicodeBidi() == UBNormal)
        return;
    resolver->embed(WTF::Unicode::PopDirectionalFormat, FromStyleOrDOM);
}

// FIXME: This function is misleadingly named. It has little to do with bidi.
// This function will iterate over inlines within a block, optionally notifying
// a bidi resolver as it enters/exits inlines (so it can push/pop embedding levels).
static inline RenderObject* bidiNext(RenderObject* root, RenderObject* current, InlineBidiResolver* resolver = 0, bool skipInlines = true, bool* endOfInlinePtr = 0)
{
    RenderObject* next = 0;
    bool oldEndOfInline = endOfInlinePtr ? *endOfInlinePtr : false;
    bool endOfInline = false;

    while (current) {
        next = 0;
        if (!oldEndOfInline && !current->isFloating() && !current->isReplaced() && !current->isPositioned() && !current->isText()) {
            next = current->firstChild();
            notifyResolverEnteredObject(resolver, next);
        }

        if (!next) {
            if (!skipInlines && !oldEndOfInline && current->isRenderInline()) {
                next = current;
                endOfInline = true;
                break;
            }

            while (current && current != root) {
                notifyResolverWillExitObject(resolver, current);

                next = current->nextSibling();
                if (next) {
                    notifyResolverEnteredObject(resolver, next);
                    break;
                }

                current = current->parent();
                if (!skipInlines && current && current != root && current->isRenderInline()) {
                    next = current;
                    endOfInline = true;
                    break;
                }
            }
        }

        if (!next)
            break;

        if (next->isText() || next->isFloating() || next->isReplaced() || next->isPositioned()
            || ((!skipInlines || !next->firstChild()) // Always return EMPTY inlines.
                && next->isRenderInline()))
            break;
        current = next;
    }

    if (endOfInlinePtr)
        *endOfInlinePtr = endOfInline;

    return next;
}

static inline RenderObject* bidiFirst(RenderObject* root, InlineBidiResolver* resolver, bool skipInlines = true)
{
    if (!root->firstChild())
        return 0;

    RenderObject* o = root->firstChild();
    if (o->isRenderInline()) {
        notifyResolverEnteredObject(resolver, o);
        if (skipInlines && o->firstChild())
            o = bidiNext(root, o, resolver, skipInlines);
        else {
            // Never skip empty inlines.
            if (resolver)
                resolver->commitExplicitEmbedding();
            return o; 
        }
    }

    if (o && !o->isText() && !o->isReplaced() && !o->isFloating() && !o->isPositioned())
        o = bidiNext(root, o, resolver, skipInlines);

    if (resolver)
        resolver->commitExplicitEmbedding();
    return o;
}

inline void InlineIterator::increment(InlineBidiResolver* resolver)
{
    if (!m_obj)
        return;
    if (m_obj->isText()) {
        m_pos++;
        if (m_pos < toRenderText(m_obj)->textLength())
            return;
    }
    // bidiNext can return 0, so use moveTo instead of moveToStartOf
    moveTo(bidiNext(m_root, m_obj, resolver), 0);
}

inline bool InlineIterator::atEnd() const
{
    return !m_obj;
}

inline UChar InlineIterator::current() const
{
    if (!m_obj || !m_obj->isText())
        return 0;

    RenderText* text = toRenderText(m_obj);
    if (m_pos >= text->textLength())
        return 0;

    return text->characters()[m_pos];
}

ALWAYS_INLINE WTF::Unicode::Direction InlineIterator::direction() const
{
    if (UChar c = current())
        return WTF::Unicode::direction(c);

    if (m_obj && m_obj->isListMarker())
        return m_obj->style()->isLeftToRightDirection() ? WTF::Unicode::LeftToRight : WTF::Unicode::RightToLeft;

    return WTF::Unicode::OtherNeutral;
}

template<>
inline void InlineBidiResolver::increment()
{
    m_current.increment(this);
}

template <>
inline void InlineBidiResolver::appendRun()
{
    if (!m_emptyRun && !m_eor.atEnd()) {
        int start = m_sor.m_pos;
        RenderObject* obj = m_sor.m_obj;
        while (obj && obj != m_eor.m_obj && obj != endOfLine.m_obj) {
            RenderBlock::appendRunsForObject(start, obj->length(), obj, *this);        
            start = 0;
            obj = bidiNext(m_sor.root(), obj);
        }
        if (obj) {
            unsigned pos = obj == m_eor.m_obj ? m_eor.m_pos : UINT_MAX;
            if (obj == endOfLine.m_obj && endOfLine.m_pos <= pos) {
                m_reachedEndOfLine = true;
                pos = endOfLine.m_pos;
            }
            // It's OK to add runs for zero-length RenderObjects, just don't make the run larger than it should be
            int end = obj->length() ? pos + 1 : 0;
            RenderBlock::appendRunsForObject(start, end, obj, *this);
        }
        
        m_eor.increment();
        m_sor = m_eor;
    }

    m_direction = WTF::Unicode::OtherNeutral;
    m_status.eor = WTF::Unicode::OtherNeutral;
}

}

#endif // InlineIterator_h
