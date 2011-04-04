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

#if USE(ACCELERATED_COMPOSITING)

#include "WebGLLayerChromium.h"

#include "GraphicsContext3D.h"
#include "LayerRendererChromium.h"

namespace WebCore {

PassRefPtr<WebGLLayerChromium> WebGLLayerChromium::create(GraphicsLayerChromium* owner)
{
    return adoptRef(new WebGLLayerChromium(owner));
}

WebGLLayerChromium::WebGLLayerChromium(GraphicsLayerChromium* owner)
    : CanvasLayerChromium(owner)
    , m_context(0)
    , m_textureUpdated(false)
{
}

void WebGLLayerChromium::updateCompositorResources()
{
    if (!m_contentsDirty)
        return;

    GraphicsContext3D* rendererContext = layerRendererContext();
    ASSERT(m_context);
    if (m_textureChanged) {
        rendererContext->bindTexture(GraphicsContext3D::TEXTURE_2D, m_textureId);
        // Set the min-mag filters to linear and wrap modes to GL_CLAMP_TO_EDGE
        // to get around NPOT texture limitations of GLES.
        rendererContext->texParameteri(GraphicsContext3D::TEXTURE_2D, GraphicsContext3D::TEXTURE_MIN_FILTER, GraphicsContext3D::LINEAR);
        rendererContext->texParameteri(GraphicsContext3D::TEXTURE_2D, GraphicsContext3D::TEXTURE_MAG_FILTER, GraphicsContext3D::LINEAR);
        rendererContext->texParameteri(GraphicsContext3D::TEXTURE_2D, GraphicsContext3D::TEXTURE_WRAP_S, GraphicsContext3D::CLAMP_TO_EDGE);
        rendererContext->texParameteri(GraphicsContext3D::TEXTURE_2D, GraphicsContext3D::TEXTURE_WRAP_T, GraphicsContext3D::CLAMP_TO_EDGE);
        m_textureChanged = false;
    }
    // Update the contents of the texture used by the compositor.
    if (m_contentsDirty && m_textureUpdated) {
        m_context->prepareTexture();
        m_context->markLayerComposited();
        m_contentsDirty = false;
        m_textureUpdated = false;
    }
}

void WebGLLayerChromium::setTextureUpdated()
{
    m_textureUpdated = true;
}

void WebGLLayerChromium::setContext(const GraphicsContext3D* context)
{
    m_context = const_cast<GraphicsContext3D*>(context);

    unsigned int textureId = m_context->platformTexture();
    if (textureId != m_textureId) {
        m_textureChanged = true;
        m_textureUpdated = true;
    }
    m_textureId = textureId;
    m_premultipliedAlpha = m_context->getContextAttributes().premultipliedAlpha;
}

}
#endif // USE(ACCELERATED_COMPOSITING)
