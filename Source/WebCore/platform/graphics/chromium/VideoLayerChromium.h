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


#ifndef VideoLayerChromium_h
#define VideoLayerChromium_h

#if USE(ACCELERATED_COMPOSITING)

#include "LayerChromium.h"
#include "VideoFrameProvider.h"

namespace WebCore {

// A Layer that contains a Video element.
class VideoLayerChromium : public LayerChromium {
public:
    static PassRefPtr<VideoLayerChromium> create(GraphicsLayerChromium* owner = 0,
                                                 VideoFrameProvider* = 0);
    virtual ~VideoLayerChromium();
    virtual void updateContentsIfDirty();
    virtual bool drawsContent() const { return true; }
    virtual void draw();

    // This function is called by VideoFrameProvider. When this method is called
    // putCurrentFrame() must be called to return the frame currently held.
    void releaseCurrentFrame();

    typedef ProgramBinding<VertexShaderPosTexTransform, FragmentShaderRGBATexFlipAlpha> RGBAProgram;
    typedef ProgramBinding<VertexShaderPosTexYUVStretch, FragmentShaderYUVVideo> YUVProgram;

protected:
    virtual void cleanupResources();
    virtual const char* layerTypeAsString() const { return "VideoLayer"; }

private:
    VideoLayerChromium(GraphicsLayerChromium* owner, VideoFrameProvider*);

    static unsigned determineTextureFormat(VideoFrameChromium*);
    bool allocateTexturesIfNeeded(GraphicsContext3D*, VideoFrameChromium*, unsigned textureFormat);
    void updateYUVContents(GraphicsContext3D*, const VideoFrameChromium*);
    void updateRGBAContents(GraphicsContext3D*, const VideoFrameChromium*);
    void allocateTexture(GraphicsContext3D*, unsigned textureId, const IntSize& dimensions, unsigned textureFormat);
    void updateTexture(GraphicsContext3D*, unsigned textureId, const IntSize& dimensions, unsigned textureFormat, const void* data);
    void drawYUV(const YUVProgram*);
    void drawRGBA(const RGBAProgram*);
    void resetFrameParameters();
    void saveCurrentFrame(VideoFrameChromium*);

    static const float yuv2RGB[9];

    bool m_skipsDraw;
    VideoFrameChromium::Format m_frameFormat;
    VideoFrameProvider* m_provider;
    VideoFrameChromium* m_currentFrame;

    unsigned m_textures[3];
    IntSize m_textureSizes[3];
    IntSize m_frameSizes[3];
};

}
#endif // USE(ACCELERATED_COMPOSITING)

#endif
