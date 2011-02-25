/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#include "config.h"

#if USE(ACCELERATED_COMPOSITING)

#include "PluginLayerChromium.h"

#include "cc/CCLayerImpl.h"
#include "GraphicsContext3D.h"
#include "LayerRendererChromium.h"
#include <GLES2/gl2.h>

namespace WebCore {

PassRefPtr<PluginLayerChromium> PluginLayerChromium::create(GraphicsLayerChromium* owner)
{
    return adoptRef(new PluginLayerChromium(owner));
}

PluginLayerChromium::PluginLayerChromium(GraphicsLayerChromium* owner)
    : LayerChromium(owner)
{
}

void PluginLayerChromium::setTextureId(unsigned id)
{
    m_textureId = id;
}

void PluginLayerChromium::updateContentsIfDirty()
{
}

void PluginLayerChromium::draw()
{
    ASSERT(layerRenderer());
    const PluginLayerChromium::Program* program = layerRenderer()->pluginLayerProgram();
    ASSERT(program && program->initialized());
    GraphicsContext3D* context = layerRendererContext();
    GLC(context, context->activeTexture(GL_TEXTURE0));
    GLC(context, context->bindTexture(GL_TEXTURE_2D, m_textureId));
    
    // FIXME: setting the texture parameters every time is redundant. Move this code somewhere
    // where it will only happen once per texture.
    GLC(context, context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLC(context, context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GLC(context, context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GLC(context, context->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    
    layerRenderer()->useShader(program->program());
    GLC(context, context->uniform1i(program->fragmentShader().samplerLocation(), 0));
    drawTexturedQuad(context, layerRenderer()->projectionMatrix(), ccLayerImpl()->drawTransform(),
                     bounds().width(), bounds().height(), ccLayerImpl()->drawOpacity(),
                     program->vertexShader().matrixLocation(),
                     program->fragmentShader().alphaLocation());
}

}
#endif // USE(ACCELERATED_COMPOSITING)
