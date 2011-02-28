/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2010 Renata Hodovan <reni@inf.u-szeged.hu>
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

#ifndef FETurbulence_h
#define FETurbulence_h

#if ENABLE(FILTERS)
#include "FilterEffect.h"
#include "Filter.h"

namespace WebCore {

enum TurbulenceType {
    FETURBULENCE_TYPE_UNKNOWN = 0,
    FETURBULENCE_TYPE_FRACTALNOISE = 1,
    FETURBULENCE_TYPE_TURBULENCE = 2
};

class FETurbulence : public FilterEffect {
public:
    static PassRefPtr<FETurbulence> create(Filter*, TurbulenceType, float, float, int, float, bool);

    TurbulenceType type() const;
    bool setType(TurbulenceType);

    float baseFrequencyY() const;
    bool setBaseFrequencyY(float);

    float baseFrequencyX() const;
    bool setBaseFrequencyX(float);

    float seed() const;
    bool setSeed(float);

    int numOctaves() const;
    bool setNumOctaves(int);

    bool stitchTiles() const;
    bool setStitchTiles(bool);

    virtual void apply();
    virtual void dump();
    
    virtual void determineAbsolutePaintRect() { setAbsolutePaintRect(maxEffectRect()); }

    virtual TextStream& externalRepresentation(TextStream&, int indention) const;

private:
    static const int s_blockSize = 256;
    static const int s_blockMask = s_blockSize - 1;

    struct PaintingData {
        long seed;
        int latticeSelector[2 * s_blockSize + 2];
        float gradient[4][2 * s_blockSize + 2][2];
        int width; // How much to subtract to wrap for stitching.
        int height;
        int wrapX; // Minimum value to wrap.
        int wrapY;
        int channel;
        IntSize filterSize;

        PaintingData(long paintingSeed, const IntSize& paintingSize);
        inline long random();
    };

    FETurbulence(Filter*, TurbulenceType, float, float, int, float, bool);

    inline void initPaint(PaintingData&);
    float noise2D(PaintingData&, const FloatPoint&);
    unsigned char calculateTurbulenceValueForPoint(PaintingData&, const FloatPoint&);

    TurbulenceType m_type;
    float m_baseFrequencyX;
    float m_baseFrequencyY;
    int m_numOctaves;
    float m_seed;
    bool m_stitchTiles;
};

} // namespace WebCore

#endif // ENABLE(FILTERS)

#endif // FETurbulence_h
