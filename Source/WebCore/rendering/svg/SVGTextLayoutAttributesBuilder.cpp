/*
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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
 */

#include "config.h"

#if ENABLE(SVG)
#include "SVGTextLayoutAttributesBuilder.h"

#include "RenderSVGInlineText.h"
#include "RenderSVGText.h"
#include "SVGTextPositioningElement.h"

// Set to a value > 0 to dump the text layout attributes
#define DUMP_TEXT_LAYOUT_ATTRIBUTES 0

namespace WebCore {

SVGTextLayoutAttributesBuilder::SVGTextLayoutAttributesBuilder()
{
}

void SVGTextLayoutAttributesBuilder::buildLayoutAttributesForTextSubtree(RenderSVGText* textRoot)
{
    ASSERT(textRoot);
    m_scopes.clear();

    // Build list of x/y/dx/dy/rotate values for each subtree element that may define these values (tspan/textPath etc).
    unsigned atCharacter = 0;
    UChar lastCharacter = '\0';
    buildLayoutScopes(textRoot, atCharacter, lastCharacter);

    if (!atCharacter)
        return;

    // Build list of x/y/dx/dy/rotate values for the outermost <text> element.
    buildOutermostLayoutScope(textRoot, atCharacter);

    // Propagate layout attributes to each RenderSVGInlineText object.
    Vector<SVGTextLayoutAttributes>& allAttributes = textRoot->layoutAttributes();
    allAttributes.clear();
    atCharacter = 0;
    lastCharacter = '\0';
    propagateLayoutAttributes(textRoot, allAttributes, atCharacter, lastCharacter);
}

static inline void extractFloatValuesFromSVGLengthList(SVGElement* lengthContext, const SVGLengthList& list, Vector<float>& floatValues, unsigned textContentLength)
{
    ASSERT(lengthContext);

    unsigned length = list.size();
    if (length > textContentLength)
        length = textContentLength;
    floatValues.reserveCapacity(length);

    for (unsigned i = 0; i < length; ++i) {
        const SVGLength& length = list.at(i);
        floatValues.append(length.value(lengthContext));
    }
}

static inline void extractFloatValuesFromSVGNumberList(const SVGNumberList& list, Vector<float>& floatValues, unsigned textContentLength)
{
    unsigned length = list.size();
    if (length > textContentLength)
        length = textContentLength;
    floatValues.reserveCapacity(length);

    for (unsigned i = 0; i < length; ++i)
        floatValues.append(list.at(i));
}

void SVGTextLayoutAttributesBuilder::buildLayoutScope(LayoutScope& scope, RenderObject* renderer, unsigned textContentStart, unsigned textContentLength) const
{
    ASSERT(renderer);
    ASSERT(renderer->style());

    scope.textContentStart = textContentStart;
    scope.textContentLength = textContentLength;

    SVGTextPositioningElement* element = SVGTextPositioningElement::elementFromRenderer(renderer);
    if (!element)
        return;

    SVGTextLayoutAttributes& attributes = scope.attributes;
    extractFloatValuesFromSVGLengthList(element, element->x(), attributes.xValues(), textContentLength);
    extractFloatValuesFromSVGLengthList(element, element->y(), attributes.yValues(), textContentLength);
    extractFloatValuesFromSVGLengthList(element, element->dx(), attributes.dxValues(), textContentLength);
    extractFloatValuesFromSVGLengthList(element, element->dy(), attributes.dyValues(), textContentLength);
    extractFloatValuesFromSVGNumberList(element->rotate(), attributes.rotateValues(), textContentLength);

    // The last rotation value spans the whole scope.
    Vector<float>& rotateValues = attributes.rotateValues();
    if (rotateValues.isEmpty())
        return;

    unsigned rotateValuesSize = rotateValues.size();
    if (rotateValuesSize == textContentLength)
        return;

    float lastRotation = rotateValues.last();

    rotateValues.resize(textContentLength);
    for (unsigned i = rotateValuesSize; i < textContentLength; ++i)
        rotateValues.at(i) = lastRotation;
}

static inline bool characterIsSpace(const UChar& character)
{
    return character == ' ';
}

static inline bool characterIsSpaceOrNull(const UChar& character)
{
    return character == ' ' || character == '\0';
}

static inline bool shouldPreserveAllWhiteSpace(RenderStyle* style)
{
    ASSERT(style);
    return style->whiteSpace() == PRE;
}
 
void SVGTextLayoutAttributesBuilder::buildLayoutScopes(RenderObject* start, unsigned& atCharacter, UChar& lastCharacter)
{
    for (RenderObject* child = start->firstChild(); child; child = child->nextSibling()) { 
        if (child->isSVGInlineText()) {
            RenderSVGInlineText* text = toRenderSVGInlineText(child);

            if (!shouldPreserveAllWhiteSpace(text->style())) {
                const UChar* characters = text->characters();
                unsigned textLength = text->textLength();    
                for (unsigned textPosition = 0; textPosition < textLength; ++textPosition) {
                    const UChar& currentCharacter = characters[textPosition];
                    if (characterIsSpace(currentCharacter) && characterIsSpaceOrNull(lastCharacter))
                        continue;

                    lastCharacter = currentCharacter;
                    ++atCharacter;
                }
            } else
                atCharacter += text->textLength();

            continue;
        }

        if (!child->isSVGInline())
            continue;

        unsigned textContentStart = atCharacter;
        buildLayoutScopes(child, atCharacter, lastCharacter);

        LayoutScope scope;
        buildLayoutScope(scope, child, textContentStart, atCharacter - textContentStart);
        m_scopes.append(scope);
    }
}

void SVGTextLayoutAttributesBuilder::buildOutermostLayoutScope(RenderSVGText* textRoot, unsigned textLength)
{
    LayoutScope scope;
    buildLayoutScope(scope, textRoot, 0, textLength);

    // Handle <text> x/y default attributes.
    Vector<float>& xValues = scope.attributes.xValues();
    if (xValues.isEmpty())
        xValues.append(0);

    Vector<float>& yValues = scope.attributes.yValues();
    if (yValues.isEmpty())
        yValues.append(0);

    m_scopes.prepend(scope);
}

void SVGTextLayoutAttributesBuilder::propagateLayoutAttributes(RenderObject* start, Vector<SVGTextLayoutAttributes>& allAttributes, unsigned& atCharacter, UChar& lastCharacter) const
{
    for (RenderObject* child = start->firstChild(); child; child = child->nextSibling()) { 
        if (child->isSVGInlineText()) {
            RenderSVGInlineText* text = toRenderSVGInlineText(child);
            const UChar* characters = text->characters();
            unsigned textLength = text->textLength();
            bool preserveWhiteSpace = shouldPreserveAllWhiteSpace(text->style());

            SVGTextLayoutAttributes attributes(text);
            attributes.reserveCapacity(textLength);
    
            unsigned valueListPosition = atCharacter;
            unsigned metricsLength = 1;
            SVGTextMetrics lastMetrics = SVGTextMetrics::emptyMetrics();

            for (unsigned textPosition = 0; textPosition < textLength; textPosition += metricsLength) {
                const UChar& currentCharacter = characters[textPosition];

                SVGTextMetrics startToCurrentMetrics = SVGTextMetrics::measureCharacterRange(text, 0, textPosition + 1);
                SVGTextMetrics currentMetrics = SVGTextMetrics::measureCharacterRange(text, textPosition, 1);

                // Frequent case for Arabic text: when measuring a single character the arabic isolated form is taken
                // when rendering the glyph "in context" (with it's surrounding characters) it changes due to shaping.
                // So whenever runWidthAdvance != currentMetrics.width(), we are processing a text run whose length is
                // not equal to the sum of the individual lengths of the glyphs, when measuring them isolated.
                float runWidthAdvance = startToCurrentMetrics.width() - lastMetrics.width();
                if (runWidthAdvance != currentMetrics.width())
                    currentMetrics.setWidth(runWidthAdvance);

                lastMetrics = startToCurrentMetrics;
                metricsLength = currentMetrics.length();

                if (!preserveWhiteSpace && characterIsSpace(currentCharacter) && characterIsSpaceOrNull(lastCharacter)) {
                    assignEmptyLayoutAttributesForCharacter(attributes);
                    attributes.textMetricsValues().append(SVGTextMetrics::emptyMetrics());
                    continue;
                }

                assignLayoutAttributesForCharacter(attributes, currentMetrics, valueListPosition);

                if (metricsLength > 1) {
                    for (unsigned i = 0; i < metricsLength - 1; ++i)
                        assignEmptyLayoutAttributesForCharacter(attributes);
                }

                lastCharacter = currentCharacter;
                valueListPosition += metricsLength;
            }

#if DUMP_TEXT_LAYOUT_ATTRIBUTES > 0
            fprintf(stderr, "\nDumping layout attributes for RenderSVGInlineText, renderer=%p, node=%p (atCharacter: %i)\n", text, text->node(), atCharacter);
            fprintf(stderr, "BiDi properties: unicode-bidi=%i, block direction=%i\n", text->style()->unicodeBidi(), text->style()->direction());
            attributes.dump();
#endif

            text->storeLayoutAttributes(attributes);
            allAttributes.append(attributes);
            atCharacter = valueListPosition;
            continue;
        }

        if (!child->isSVGInline())
            continue;

        propagateLayoutAttributes(child, allAttributes, atCharacter, lastCharacter);
    }
}

float SVGTextLayoutAttributesBuilder::nextLayoutValue(LayoutValueType type, unsigned atCharacter) const
{
    for (int i = m_scopes.size() - 1; i >= 0; --i) {
        const LayoutScope& scope = m_scopes.at(i);
        if (scope.textContentStart > atCharacter || scope.textContentStart + scope.textContentLength < atCharacter)
            continue;

        const Vector<float>* valuesPointer = 0;
        switch (type) {
        case XValueAttribute:
            valuesPointer = &scope.attributes.xValues();
            break;
        case YValueAttribute:
            valuesPointer = &scope.attributes.yValues();
            break;
        case DxValueAttribute:
            valuesPointer = &scope.attributes.dxValues();
            break;
        case DyValueAttribute:
            valuesPointer = &scope.attributes.dyValues();
            break;
        case RotateValueAttribute:
            valuesPointer = &scope.attributes.rotateValues();
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        ASSERT(valuesPointer);
        const Vector<float>& values = *valuesPointer;
        if (values.isEmpty())
            continue;

        unsigned position = atCharacter - scope.textContentStart;
        if (position >= values.size())
            continue;

        return values.at(position);
    }

    return SVGTextLayoutAttributes::emptyValue();
}

void SVGTextLayoutAttributesBuilder::assignLayoutAttributesForCharacter(SVGTextLayoutAttributes& attributes, SVGTextMetrics& metrics, unsigned valueListPosition) const
{
    attributes.xValues().append(nextLayoutValue(XValueAttribute, valueListPosition));
    attributes.yValues().append(nextLayoutValue(YValueAttribute, valueListPosition));
    attributes.dxValues().append(nextLayoutValue(DxValueAttribute, valueListPosition));
    attributes.dyValues().append(nextLayoutValue(DyValueAttribute, valueListPosition));
    attributes.rotateValues().append(nextLayoutValue(RotateValueAttribute, valueListPosition));
    attributes.textMetricsValues().append(metrics);
}

void SVGTextLayoutAttributesBuilder::assignEmptyLayoutAttributesForCharacter(SVGTextLayoutAttributes& attributes) const
{
    attributes.xValues().append(SVGTextLayoutAttributes::emptyValue());
    attributes.yValues().append(SVGTextLayoutAttributes::emptyValue());
    attributes.dxValues().append(SVGTextLayoutAttributes::emptyValue());
    attributes.dyValues().append(SVGTextLayoutAttributes::emptyValue());
    attributes.rotateValues().append(SVGTextLayoutAttributes::emptyValue());
    // This doesn't add an empty value to textMetricsValues() on purpose!
}

}

#endif // ENABLE(SVG)
