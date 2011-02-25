/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CCLayerImpl_h
#define CCLayerImpl_h

#include "Color.h"
#include "FloatRect.h"
#include "IntRect.h"
#include "TransformationMatrix.h"
#include <wtf/OwnPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>


namespace WebCore {

class LayerChromium;
class LayerRendererChromium;
class RenderSurfaceChromium;

class CCLayerImpl : public RefCounted<CCLayerImpl> {
public:
    static PassRefPtr<CCLayerImpl> create(LayerChromium* owner)
    {
        return adoptRef(new CCLayerImpl(owner));
    }
    // When this class gets subclasses, remember to add 'virtual' here.
    ~CCLayerImpl();

    CCLayerImpl* superlayer() const;
    CCLayerImpl* maskLayer() const;
    CCLayerImpl* replicaLayer() const;

    void updateContentsIfDirty();
    void draw();
    bool drawsContent() const;
    void unreserveContentsTexture();
    void bindContentsTexture();

    void cleanupResources();

    // Debug layer border - visual effect only, do not change geometry/clipping/etc.
    void setDebugBorderColor(Color c) { m_debugBorderColor = c; }
    Color debugBorderColor() const { return m_debugBorderColor; }
    void setDebugBorderWidth(float width) { m_debugBorderWidth = width; }
    float debugBorderWidth() const { return m_debugBorderWidth; }

    void drawDebugBorder();

    void setLayerRenderer(LayerRendererChromium*);
    LayerRendererChromium* layerRenderer() const { return m_layerRenderer.get(); }

    RenderSurfaceChromium* createRenderSurface();

    RenderSurfaceChromium* renderSurface() const { return m_renderSurface.get(); }
    void clearRenderSurface() { m_renderSurface.clear(); }
    float drawDepth() const { return m_drawDepth; }
    void setDrawDepth(float depth) { m_drawDepth = depth; }
    float drawOpacity() const { return m_drawOpacity; }
    void setDrawOpacity(float opacity) { m_drawOpacity = opacity; }
    const IntRect& scissorRect() const { return m_scissorRect; }
    void setScissorRect(const IntRect& rect) { m_scissorRect = rect; }
    RenderSurfaceChromium* targetRenderSurface() const { return m_targetRenderSurface; }
    void setTargetRenderSurface(RenderSurfaceChromium* surface) { m_targetRenderSurface = surface; }

    bool doubleSided() const { return m_doubleSided; }
    void setDoubleSided(bool doubleSided) { m_doubleSided = doubleSided; }
    const IntSize& bounds() const { return m_bounds; }
    void setBounds(const IntSize& bounds) { m_bounds = bounds; }

    // Returns the rect containtaining this layer in the current view's coordinate system.
    const IntRect getDrawRect() const;

    const TransformationMatrix& drawTransform() const { return m_drawTransform; }
    void setDrawTransform(const TransformationMatrix& matrix) { m_drawTransform = matrix; }
    const IntRect& drawableContentRect() const { return m_drawableContentRect; }
    void setDrawableContentRect(const IntRect& rect) { m_drawableContentRect = rect; }

private:
    // For now, CCLayers are owned directly by a LayerChromium.
    LayerChromium* m_owner;
    explicit CCLayerImpl(LayerChromium*);

    // Render surface this layer draws into. This is a surface that can belong
    // either to this layer (if m_targetRenderSurface == m_renderSurface) or
    // to an ancestor of this layer. The target render surface determines the
    // coordinate system the layer's transforms are relative to.
    RenderSurfaceChromium* m_targetRenderSurface;

    // The global depth value of the center of the layer. This value is used
    // to sort layers from back to front.
    float m_drawDepth;
    float m_drawOpacity;

    // Whether the "back" of this layer should draw.
    bool m_doubleSided;

    // Debug borders.
    Color m_debugBorderColor;
    float m_debugBorderWidth;

    TransformationMatrix m_drawTransform;

    IntSize m_bounds;

    // The scissor rectangle that should be used when this layer is drawn.
    // Inherited by the parent layer and further restricted if this layer masks
    // to bounds.
    IntRect m_scissorRect;

    // Render surface associated with this layer. The layer and its descendants
    // will render to this surface.
    OwnPtr<RenderSurfaceChromium> m_renderSurface;

    // Hierarchical bounding rect containing the layer and its descendants.
    IntRect m_drawableContentRect;

    // Points to the layer renderer that updates and draws this layer.
    RefPtr<LayerRendererChromium> m_layerRenderer;
};

}

#endif // CCLayerImpl_h

