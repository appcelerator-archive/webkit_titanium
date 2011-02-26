/*
    Copyright (C) 1999 Lars Knoll (knoll@kde.org)
    Copyright (C) 2006, 2008 Apple Inc. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef Length_h
#define Length_h

#include <wtf/Assertions.h>
#include <wtf/FastAllocBase.h>
#include <wtf/Forward.h>
#include <wtf/MathExtras.h>
#include <wtf/PassOwnArrayPtr.h>

namespace WebCore {

const int undefinedLength = -1;
const int percentScaleFactor = 128;
const int intMaxForLength = 0x7ffffff; // max value for a 28-bit int
const int intMinForLength = (-0x7ffffff - 1); // min value for a 28-bit int

enum LengthType { Auto, Relative, Percent, Fixed, Intrinsic, MinIntrinsic };

struct Length {
    WTF_MAKE_FAST_ALLOCATED;
public:
    Length()
        : m_intValue(0), m_quirk(0), m_type(Auto), m_isFloat(0)
    {
    }

    Length(LengthType t)
        : m_intValue(0), m_quirk(0), m_type(t), m_isFloat(0)
    {
    }

    Length(int v, LengthType t, bool q = false)
        : m_intValue(v), m_quirk(q), m_type(t), m_isFloat(0)
    {
        ASSERT(t != Percent);
    }

    Length(double v, LengthType t, bool q = false)
        : m_quirk(q), m_type(t), m_isFloat(0)
    {
        if (m_type == Percent)
            m_intValue = static_cast<int>(v*percentScaleFactor);
        else {
            m_isFloat = 1;            
            m_floatValue = static_cast<float>(v);
        }
    }
    
    bool operator==(const Length& o) const { return (getFloatValue() == o.getFloatValue()) && (m_type == o.m_type) && (m_quirk == o.m_quirk); }
    bool operator!=(const Length& o) const { return (getFloatValue() != o.getFloatValue()) || (m_type != o.m_type) || (m_quirk != o.m_quirk); }

    
    int value() const {
        ASSERT(type() != Percent);
        return rawValue();
    }

    int rawValue() const { return getIntValue(); }

    double percent() const
    {
        ASSERT(type() == Percent);
        return static_cast<double>(rawValue()) / percentScaleFactor;
    }

    LengthType type() const { return static_cast<LengthType>(m_type); }
    bool quirk() const { return m_quirk; }

    void setValue(LengthType t, int value)
    {
        ASSERT(t != Percent);
        *this = Length(value, t);
    }

    void setValue(int value)
    {
        ASSERT(!value || type() != Percent);
        *this = Length(value, Fixed);
    }

    void setValue(LengthType t, double value)
    {
        *this = Length(value, t);
    }

    void setValue(double value)
    {
        *this = Length(value, Fixed);
    }

    // note: works only for certain types, returns undefinedLength otherwise
    int calcValue(int maxValue, bool roundPercentages = false) const
    {
        switch (type()) {
            case Fixed:
                return value();
            case Percent:
                if (roundPercentages)
                    return static_cast<int>(round(maxValue * percent() / 100.0));
                return maxValue * rawValue() / (100 * percentScaleFactor);
            case Auto:
                return maxValue;
            default:
                return undefinedLength;
        }
    }

    int calcMinValue(int maxValue, bool roundPercentages = false) const
    {
        switch (type()) {
            case Fixed:
                return value();
            case Percent:
                if (roundPercentages)
                    return static_cast<int>(round(maxValue * percent() / 100.0));
                return maxValue * rawValue() / (100 * percentScaleFactor);
            case Auto:
            default:
                return 0;
        }
    }

    float calcFloatValue(int maxValue) const
    {
        switch (type()) {
            case Fixed:
                return getFloatValue();
            case Percent:
                return static_cast<float>(maxValue * percent() / 100.0);
            case Auto:
                return static_cast<float>(maxValue);
            default:
                return static_cast<float>(undefinedLength);
        }
    }

    bool isUndefined() const { return rawValue() == undefinedLength; }
    bool isZero() const { return !getIntValue(); }
    bool isPositive() const { return rawValue() > 0; }
    bool isNegative() const { return rawValue() < 0; }

    bool isAuto() const { return type() == Auto; }
    bool isRelative() const { return type() == Relative; }
    bool isPercent() const { return type() == Percent; }
    bool isFixed() const { return type() == Fixed; }
    bool isIntrinsicOrAuto() const { return type() == Auto || type() == MinIntrinsic || type() == Intrinsic; }

    Length blend(const Length& from, double progress) const
    {
        // Blend two lengths to produce a new length that is in between them.  Used for animation.
        if (!from.isZero() && !isZero() && from.type() != type())
            return *this;

        if (from.isZero() && isZero())
            return *this;
        
        LengthType resultType = type();
        if (isZero())
            resultType = from.type();
        
        if (resultType == Percent) {
            double fromPercent = from.isZero() ? 0. : from.percent();
            double toPercent = isZero() ? 0. : percent();
            return Length(fromPercent + (toPercent - fromPercent) * progress, Percent);
        } 
            
        double fromValue = from.isZero() ? 0 : from.getFloatValue();
        double toValue = isZero() ? 0 : getFloatValue();
        return Length(fromValue + (toValue - fromValue) * progress, resultType);
    }

private:
    int getIntValue() const
    {
        if (m_isFloat)
            return static_cast<int>(m_floatValue);
        return m_intValue;
    }
    
    float getFloatValue() const
    {
        if (m_isFloat)
            return m_floatValue;
        return m_intValue;
    }
    
    union {
        int m_intValue;
        float m_floatValue;
    };
    bool m_quirk;
    unsigned char m_type;
    bool m_isFloat;
};

PassOwnArrayPtr<Length> newCoordsArray(const String&, int& len);
PassOwnArrayPtr<Length> newLengthArray(const String&, int& len);

} // namespace WebCore

#endif // Length_h
