/*
 * Copyright (c) 2010, Google Inc. All rights reserved.
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

#include "GLES2Canvas.h"

#include "DrawingBuffer.h"
#include "FloatRect.h"
#include "FloatSize.h"
#include "GraphicsContext3D.h"
#include "internal_glu.h"
#include "IntRect.h"
#include "LoopBlinnPathProcessor.h"
#include "LoopBlinnSolidFillShader.h"
#include "Path.h"
#include "PlatformString.h"
#include "SharedGraphicsContext3D.h"
#if USE(SKIA)
#include "SkPath.h"
#endif
#include "Texture.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include <wtf/OwnArrayPtr.h>
#include <wtf/text/CString.h>

namespace WebCore {

// Number of line segments used to approximate bezier curves.
const int pathTesselation = 30;
typedef void (GLAPIENTRY *TESSCB)();
typedef WTF::Vector<float> FloatVector;
typedef WTF::Vector<double> DoubleVector;

struct GLES2Canvas::State {
    State()
        : m_fillColor(0, 0, 0, 255)
        , m_alpha(1.0f)
        , m_compositeOp(CompositeSourceOver)
        , m_clippingEnabled(false)
    {
    }
    State(const State& other)
        : m_fillColor(other.m_fillColor)
        , m_alpha(other.m_alpha)
        , m_compositeOp(other.m_compositeOp)
        , m_ctm(other.m_ctm)
        , m_clippingPaths() // Don't copy; clipping paths are tracked per-state.
        , m_clippingEnabled(other.m_clippingEnabled)
    {
    }
    Color m_fillColor;
    float m_alpha;
    CompositeOperator m_compositeOp;
    AffineTransform m_ctm;
    WTF::Vector<Path> m_clippingPaths;
    bool m_clippingEnabled;

    // Helper function for applying the state's alpha value to the given input
    // color to produce a new output color. The logic is the same as
    // PlatformContextSkia::State::applyAlpha(), but the type is different.
    Color applyAlpha(const Color& c)
    {
        int s = roundf(m_alpha * 256);
        if (s >= 256)
            return c;
        if (s < 0)
            return Color();

        int a = (c.alpha() * s) >> 8;
        return Color(c.red(), c.green(), c.blue(), a);
    }

};

static inline FloatPoint operator*(const FloatPoint& f, float scale)
{
    return FloatPoint(f.x() * scale, f.y() * scale);
}

static inline FloatPoint operator*(float scale, const FloatPoint& f)
{
    return FloatPoint(f.x() * scale, f.y() * scale);
}

static inline FloatSize operator*(const FloatSize& f, float scale)
{
    return FloatSize(f.width() * scale, f.height() * scale);
}

static inline FloatSize operator*(float scale, const FloatSize& f)
{
    return FloatSize(f.width() * scale, f.height() * scale);
}

class Quadratic {
  public:
    Quadratic(FloatPoint a, FloatPoint b, FloatPoint c) :
        m_a(a), m_b(b), m_c(c)
    {
    }
    static Quadratic fromBezier(FloatPoint p0, FloatPoint p1, FloatPoint p2)
    {
        FloatSize p1s(p1.x(), p1.y());
        FloatSize p2s(p2.x(), p2.y());
        FloatPoint b = -2.0f * p0 + 2.0f * p1s;
        FloatPoint c =         p0 - 2.0f * p1s + p2s;
        return Quadratic(p0, b, c);
    }
    inline FloatPoint evaluate(float t)
    {
        return m_a + t * (m_b + t * m_c);
    }
    FloatPoint m_a, m_b, m_c, m_d;
};

class Cubic {
  public:
    Cubic(FloatPoint a, FloatPoint b, FloatPoint c, FloatPoint d) :
        m_a(a), m_b(b), m_c(c), m_d(d) 
    {
    }
    static Cubic fromBezier(FloatPoint p0, FloatPoint p1, FloatPoint p2, FloatPoint p3)
    {
        FloatSize p1s(p1.x(), p1.y());
        FloatSize p2s(p2.x(), p2.y());
        FloatSize p3s(p3.x(), p3.y());
        FloatPoint b = -3.0f * p0 + 3.0f * p1s;
        FloatPoint c =  3.0f * p0 - 6.0f * p1s + 3.0f * p2s;
        FloatPoint d = -1.0f * p0 + 3.0f * p1s - 3.0f * p2s + p3s;
        return Cubic(p0, b, c, d);
    }
    FloatPoint evaluate(float t)
    {
        return m_a + t * (m_b + t * (m_c + t * m_d));
    }
    FloatPoint m_a, m_b, m_c, m_d;
};

GLES2Canvas::GLES2Canvas(SharedGraphicsContext3D* context, DrawingBuffer* drawingBuffer, const IntSize& size)
    : m_size(size)
    , m_context(context)
    , m_drawingBuffer(drawingBuffer)
    , m_state(0)
    , m_pathVertexBuffer(0)
{
    m_flipMatrix.translate(-1.0f, 1.0f);
    m_flipMatrix.scale(2.0f / size.width(), -2.0f / size.height());

    m_stateStack.append(State());
    m_state = &m_stateStack.last();
}

GLES2Canvas::~GLES2Canvas()
{
}

void GLES2Canvas::bindFramebuffer()
{
    m_drawingBuffer->bind();
}

void GLES2Canvas::clearRect(const FloatRect& rect)
{
    bindFramebuffer();
    if (m_state->m_ctm.isIdentity() && !m_state->m_clippingEnabled) {
        m_context->scissor(rect.x(), m_size.height() - rect.height() - rect.y(), rect.width(), rect.height());
        m_context->enable(GraphicsContext3D::SCISSOR_TEST);
        m_context->clearColor(Color(RGBA32(0)));
        m_context->clear(GraphicsContext3D::COLOR_BUFFER_BIT);
        m_context->disable(GraphicsContext3D::SCISSOR_TEST);
    } else {
        save();
        setCompositeOperation(CompositeClear);
        fillRect(rect, Color(RGBA32(0)), ColorSpaceDeviceRGB);
        restore();
    }
}

void GLES2Canvas::fillPath(const Path& path)
{
    m_context->applyCompositeOperator(m_state->m_compositeOp);
    applyClipping(m_state->m_clippingEnabled);
    fillPath(path, m_state->applyAlpha(m_state->m_fillColor));
}

void GLES2Canvas::fillRect(const FloatRect& rect, const Color& color, ColorSpace colorSpace)
{
    m_context->applyCompositeOperator(m_state->m_compositeOp);
    applyClipping(m_state->m_clippingEnabled);
    m_context->useQuadVertices();

    AffineTransform matrix(m_flipMatrix);
    matrix *= m_state->m_ctm;
    matrix.translate(rect.x(), rect.y());
    matrix.scale(rect.width(), rect.height());

    m_context->useFillSolidProgram(matrix, color);

    bindFramebuffer();
    m_context->drawArrays(GraphicsContext3D::TRIANGLE_STRIP, 0, 4);
}

void GLES2Canvas::fillRect(const FloatRect& rect)
{
    fillRect(rect, m_state->applyAlpha(m_state->m_fillColor), ColorSpaceDeviceRGB);
}

void GLES2Canvas::setFillColor(const Color& color, ColorSpace colorSpace)
{
    m_state->m_fillColor = color;
}

void GLES2Canvas::setAlpha(float alpha)
{
    m_state->m_alpha = alpha;
}

void GLES2Canvas::translate(float x, float y)
{
    m_state->m_ctm.translate(x, y);
}

void GLES2Canvas::rotate(float angleInRadians)
{
    m_state->m_ctm.rotate(angleInRadians * (180.0f / M_PI));
}

void GLES2Canvas::scale(const FloatSize& size)
{
    m_state->m_ctm.scale(size.width(), size.height());
}

void GLES2Canvas::concatCTM(const AffineTransform& affine)
{
    m_state->m_ctm *= affine;
}

void GLES2Canvas::setCTM(const AffineTransform& affine)
{
    m_state->m_ctm = affine;
}

void GLES2Canvas::clipPath(const Path& path)
{
    bindFramebuffer();
    checkGLError("bindFramebuffer");
    beginStencilDraw();
    // Red is used so we can see it if it ends up in the color buffer.
    Color red(255, 0, 0, 255);
    fillPath(path, red);
    m_state->m_clippingPaths.append(path);
    m_state->m_clippingEnabled = true;
}

void GLES2Canvas::clipOut(const Path& path)
{
    ASSERT(!"clipOut is unsupported in GLES2Canvas.\n");
}

void GLES2Canvas::save()
{
    m_stateStack.append(State(m_stateStack.last()));
    m_state = &m_stateStack.last();
}

void GLES2Canvas::restore()
{
    ASSERT(!m_stateStack.isEmpty());
    bool hadClippingPaths = !m_state->m_clippingPaths.isEmpty();
    m_stateStack.removeLast();
    m_state = &m_stateStack.last();
    if (hadClippingPaths) {
        m_context->clear(GraphicsContext3D::STENCIL_BUFFER_BIT);
        beginStencilDraw();
        StateVector::const_iterator iter;
        for (iter = m_stateStack.begin(); iter < m_stateStack.end(); ++iter) {
            const State& state = *iter;
            const Vector<Path>& clippingPaths = state.m_clippingPaths;
            Vector<Path>::const_iterator pathIter;
            for (pathIter = clippingPaths.begin(); pathIter < clippingPaths.end(); ++pathIter) {
                // Red is used so we can see it if it ends up in the color buffer.
                Color red(255, 0, 0, 255);
                fillPath(*pathIter, red);
            }
        }
    }
}

void GLES2Canvas::drawTexturedRect(unsigned texture, const IntSize& textureSize, const FloatRect& srcRect, const FloatRect& dstRect, ColorSpace colorSpace, CompositeOperator compositeOp)
{
    m_context->applyCompositeOperator(compositeOp);
    applyClipping(false);

    m_context->useQuadVertices();
    m_context->setActiveTexture(GraphicsContext3D::TEXTURE0);

    m_context->bindTexture(GraphicsContext3D::TEXTURE_2D, texture);

    drawQuad(textureSize, srcRect, dstRect, m_state->m_ctm, m_state->m_alpha);
}

void GLES2Canvas::drawTexturedRect(Texture* texture, const FloatRect& srcRect, const FloatRect& dstRect, ColorSpace colorSpace, CompositeOperator compositeOp)
{
    drawTexturedRect(texture, srcRect, dstRect, m_state->m_ctm, m_state->m_alpha, colorSpace, compositeOp, m_state->m_clippingEnabled);
}


void GLES2Canvas::drawTexturedRect(Texture* texture, const FloatRect& srcRect, const FloatRect& dstRect, const AffineTransform& transform, float alpha, ColorSpace colorSpace, CompositeOperator compositeOp, bool clip)
{
    m_context->applyCompositeOperator(compositeOp);
    applyClipping(clip);
    const TilingData& tiles = texture->tiles();
    IntRect tileIdxRect = tiles.overlappedTileIndices(srcRect);

    m_context->useQuadVertices();
    m_context->setActiveTexture(GraphicsContext3D::TEXTURE0);

    for (int y = tileIdxRect.y(); y <= tileIdxRect.maxY(); y++) {
        for (int x = tileIdxRect.x(); x <= tileIdxRect.maxX(); x++)
            drawTexturedRectTile(texture, tiles.tileIndex(x, y), srcRect, dstRect, transform, alpha);
    }
}

void GLES2Canvas::drawTexturedRectTile(Texture* texture, int tile, const FloatRect& srcRect, const FloatRect& dstRect, const AffineTransform& transform, float alpha)
{
    if (dstRect.isEmpty())
        return;

    const TilingData& tiles = texture->tiles();

    texture->bindTile(tile);

    FloatRect srcRectClippedInTileSpace;
    FloatRect dstRectIntersected;
    tiles.intersectDrawQuad(srcRect, dstRect, tile, &srcRectClippedInTileSpace, &dstRectIntersected);

    IntRect tileBoundsWithBorder = tiles.tileBoundsWithBorder(tile);

    drawQuad(tileBoundsWithBorder.size(), srcRectClippedInTileSpace, dstRectIntersected, transform, alpha);
}

void GLES2Canvas::drawQuad(const IntSize& textureSize, const FloatRect& srcRect, const FloatRect& dstRect, const AffineTransform& transform, float alpha)
{
    AffineTransform matrix(m_flipMatrix);
    matrix *= transform;
    matrix.translate(dstRect.x(), dstRect.y());
    matrix.scale(dstRect.width(), dstRect.height());

    AffineTransform texMatrix;
    texMatrix.scale(1.0f / textureSize.width(), 1.0f / textureSize.height());
    texMatrix.translate(srcRect.x(), srcRect.y());
    texMatrix.scale(srcRect.width(), srcRect.height());

    bindFramebuffer();

    m_context->useTextureProgram(matrix, texMatrix, alpha);
    m_context->drawArrays(GraphicsContext3D::TRIANGLE_STRIP, 0, 4);
    checkGLError("glDrawArrays");
}

void GLES2Canvas::setCompositeOperation(CompositeOperator op)
{
    m_state->m_compositeOp = op;
}

Texture* GLES2Canvas::createTexture(NativeImagePtr ptr, Texture::Format format, int width, int height)
{
    return m_context->createTexture(ptr, format, width, height);
}

Texture* GLES2Canvas::getTexture(NativeImagePtr ptr)
{
    return m_context->getTexture(ptr);
}

#if USE(SKIA)
// This is actually cross-platform code, but since its only caller is inside a
// USE(SKIA), it will cause a warning-as-error on Chrome/Mac.
static void interpolateQuadratic(DoubleVector* vertices, const FloatPoint& p0, const FloatPoint& p1, const FloatPoint& p2)
{
    float tIncrement = 1.0f / pathTesselation, t = tIncrement;
    Quadratic c = Quadratic::fromBezier(p0, p1, p2);
    for (int i = 0; i < pathTesselation; ++i, t += tIncrement) {
        FloatPoint p = c.evaluate(t);
        vertices->append(p.x());
        vertices->append(p.y());
        vertices->append(1.0);
    }
}

static void interpolateCubic(DoubleVector* vertices, const FloatPoint& p0, const FloatPoint& p1, const FloatPoint& p2, const FloatPoint& p3)
{
    float tIncrement = 1.0f / pathTesselation, t = tIncrement;
    Cubic c = Cubic::fromBezier(p0, p1, p2, p3);
    for (int i = 0; i < pathTesselation; ++i, t += tIncrement) {
        FloatPoint p = c.evaluate(t);
        vertices->append(p.x());
        vertices->append(p.y());
        vertices->append(1.0);
    }
}
#endif

struct PolygonData {
    PolygonData(FloatVector* vertices, WTF::Vector<short>* indices)
      : m_vertices(vertices)
      , m_indices(indices)
    {
    }
    FloatVector* m_vertices;
    WTF::Vector<short>* m_indices;
};

static void beginData(GLenum type, void* data)
{
    ASSERT(type == GL_TRIANGLES);
}

static void edgeFlagData(GLboolean flag, void* data)
{
}

static void vertexData(void* vertexData, void* data)
{
    static_cast<PolygonData*>(data)->m_indices->append(reinterpret_cast<long>(vertexData));
}

static void endData(void* data)
{
}

static void combineData(GLdouble coords[3], void* vertexData[4],
                                 GLfloat weight[4], void **outData, void* data)
{
    PolygonData* polygonData = static_cast<PolygonData*>(data);
    int index = polygonData->m_vertices->size() / 3;
    polygonData->m_vertices->append(static_cast<float>(coords[0]));
    polygonData->m_vertices->append(static_cast<float>(coords[1]));
    polygonData->m_vertices->append(1.0f);
    *outData = reinterpret_cast<void*>(index);
}

typedef void (*TESSCB)();

void GLES2Canvas::createVertexBufferFromPath(const Path& path, int* count, unsigned* vertexBuffer, unsigned* indexBuffer)
{
    *vertexBuffer = m_context->graphicsContext3D()->createBuffer();
    checkGLError("createVertexBufferFromPath, createBuffer");
    *indexBuffer = m_context->graphicsContext3D()->createBuffer();
    checkGLError("createVertexBufferFromPath, createBuffer");
    DoubleVector inVertices;
    WTF::Vector<size_t> contours;
#if USE(SKIA)
    const SkPath* skPath = path.platformPath();
    SkPoint pts[4];
    SkPath::Iter iter(*skPath, true);
    SkPath::Verb verb;
    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        switch (verb) {
        case SkPath::kMove_Verb:
            inVertices.append(pts[0].fX);
            inVertices.append(pts[0].fY);
            inVertices.append(1.0);
            break;
        case SkPath::kLine_Verb:
            inVertices.append(pts[1].fX);
            inVertices.append(pts[1].fY);
            inVertices.append(1.0);
            break;
        case SkPath::kQuad_Verb:
            interpolateQuadratic(&inVertices, pts[0], pts[1], pts[2]);
            break;
        case SkPath::kCubic_Verb:
            interpolateCubic(&inVertices, pts[0], pts[1], pts[2], pts[3]);
            break;
        case SkPath::kClose_Verb:
            contours.append(inVertices.size() / 3);
            break;
        case SkPath::kDone_Verb:
            break;
        }
    }
#else
    ASSERT(!"Path extraction not implemented on this platform.");
#endif

    GLUtesselator* tess = internal_gluNewTess();
    internal_gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);
    internal_gluTessCallback(tess, GLU_TESS_BEGIN_DATA, (TESSCB) &beginData);
    internal_gluTessCallback(tess, GLU_TESS_VERTEX_DATA, (TESSCB) &vertexData);
    internal_gluTessCallback(tess, GLU_TESS_END_DATA, (TESSCB) &endData);
    internal_gluTessCallback(tess, GLU_TESS_EDGE_FLAG_DATA, (TESSCB) &edgeFlagData);
    internal_gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (TESSCB) &combineData);
    WTF::Vector<short> indices;
    FloatVector vertices;
    vertices.reserveInitialCapacity(inVertices.size());
    PolygonData data(&vertices, &indices);
    internal_gluTessBeginPolygon(tess, &data);
    WTF::Vector<size_t>::const_iterator contour;
    size_t i = 0;
    for (contour = contours.begin(); contour != contours.end(); ++contour) {
        internal_gluTessBeginContour(tess);
        for (; i < *contour; ++i) {
            vertices.append(inVertices[i * 3]);
            vertices.append(inVertices[i * 3 + 1]);
            vertices.append(1.0f);
            internal_gluTessVertex(tess, &inVertices[i * 3], reinterpret_cast<void*>(i));
        }
        internal_gluTessEndContour(tess);
    }
    internal_gluTessEndPolygon(tess);
    internal_gluDeleteTess(tess);

    m_context->graphicsContext3D()->bindBuffer(GraphicsContext3D::ARRAY_BUFFER, *vertexBuffer);
    checkGLError("createVertexBufferFromPath, bindBuffer ARRAY_BUFFER");
    m_context->graphicsContext3D()->bufferData(GraphicsContext3D::ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GraphicsContext3D::STREAM_DRAW);
    checkGLError("createVertexBufferFromPath, bufferData ARRAY_BUFFER");

    m_context->graphicsContext3D()->bindBuffer(GraphicsContext3D::ELEMENT_ARRAY_BUFFER, *indexBuffer);
    checkGLError("createVertexBufferFromPath, bindBuffer ELEMENT_ARRAY_BUFFER");
    m_context->graphicsContext3D()->bufferData(GraphicsContext3D::ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(short), indices.data(), GraphicsContext3D::STREAM_DRAW);
    checkGLError("createVertexBufferFromPath, bufferData ELEMENT_ARRAY_BUFFER");
    *count = indices.size();
}

void GLES2Canvas::fillPath(const Path& path, const Color& color)
{
    if (SharedGraphicsContext3D::useLoopBlinnForPathRendering()) {
        bindFramebuffer();
        m_context->applyCompositeOperator(m_state->m_compositeOp);

        m_pathCache.clear();
        LoopBlinnPathProcessor processor;
        processor.process(path, m_pathCache);
        if (!m_pathVertexBuffer)
            m_pathVertexBuffer = m_context->createBuffer();
        m_context->bindBuffer(GraphicsContext3D::ARRAY_BUFFER, m_pathVertexBuffer);
        int byteSizeOfVertices = 2 * m_pathCache.numberOfVertices() * sizeof(float);
        int byteSizeOfTexCoords = 3 * m_pathCache.numberOfVertices() * sizeof(float);
        int byteSizeOfInteriorVertices = 2 * m_pathCache.numberOfInteriorVertices() * sizeof(float);
        m_context->bufferData(GraphicsContext3D::ARRAY_BUFFER,
                              byteSizeOfVertices + byteSizeOfTexCoords + byteSizeOfInteriorVertices,
                              GraphicsContext3D::STATIC_DRAW);
        m_context->bufferSubData(GraphicsContext3D::ARRAY_BUFFER, 0, byteSizeOfVertices, m_pathCache.vertices());
        m_context->bufferSubData(GraphicsContext3D::ARRAY_BUFFER, byteSizeOfVertices, byteSizeOfTexCoords, m_pathCache.texcoords());
        m_context->bufferSubData(GraphicsContext3D::ARRAY_BUFFER, byteSizeOfVertices + byteSizeOfTexCoords, byteSizeOfInteriorVertices, m_pathCache.interiorVertices());

        AffineTransform matrix(m_flipMatrix);
        matrix *= m_state->m_ctm;

        // Draw the exterior
        m_context->useLoopBlinnExteriorProgram(0, byteSizeOfVertices, matrix, color);
        m_context->drawArrays(GraphicsContext3D::TRIANGLES, 0, m_pathCache.numberOfVertices());

        // Draw the interior
        m_context->useLoopBlinnInteriorProgram(byteSizeOfVertices + byteSizeOfTexCoords, matrix, color);
        m_context->drawArrays(GraphicsContext3D::TRIANGLES, 0, m_pathCache.numberOfInteriorVertices());
    } else {
        int count;
        unsigned vertexBuffer, indexBuffer;
        createVertexBufferFromPath(path, &count, &vertexBuffer, &indexBuffer);
        m_context->graphicsContext3D()->bindBuffer(GraphicsContext3D::ARRAY_BUFFER, vertexBuffer);
        checkGLError("bindBuffer");
        m_context->graphicsContext3D()->bindBuffer(GraphicsContext3D::ELEMENT_ARRAY_BUFFER, indexBuffer);
        checkGLError("bindBuffer");

        AffineTransform matrix(m_flipMatrix);
        matrix *= m_state->m_ctm;

        m_context->useFillSolidProgram(matrix, color);
        checkGLError("useFillSolidProgram");

        bindFramebuffer();
        m_context->graphicsContext3D()->drawElements(GraphicsContext3D::TRIANGLES, count, GraphicsContext3D::UNSIGNED_SHORT, 0);
        checkGLError("drawArrays");

        m_context->graphicsContext3D()->deleteBuffer(vertexBuffer);
        checkGLError("deleteBuffer");

        m_context->graphicsContext3D()->deleteBuffer(indexBuffer);
        checkGLError("deleteBuffer");
    }
}

void GLES2Canvas::beginStencilDraw()
{
    // Turn on stencil test.
    m_context->enableStencil(true);
    checkGLError("enable STENCIL_TEST");

    // Stencil test never passes, so colorbuffer is not drawn.
    m_context->graphicsContext3D()->stencilFunc(GraphicsContext3D::NEVER, 1, 1);
    checkGLError("stencilFunc");

    // All writes incremement the stencil buffer.
    m_context->graphicsContext3D()->stencilOp(GraphicsContext3D::INCR,
                                              GraphicsContext3D::INCR,
                                              GraphicsContext3D::INCR);
    checkGLError("stencilOp");
}

void GLES2Canvas::applyClipping(bool enable)
{
    m_context->enableStencil(enable);
    if (enable) {
        // Enable drawing only where stencil is non-zero.
        m_context->graphicsContext3D()->stencilFunc(GraphicsContext3D::EQUAL, m_state->m_clippingPaths.size() % 256, 1);
        checkGLError("stencilFunc");
        // Keep all stencil values the same.
        m_context->graphicsContext3D()->stencilOp(GraphicsContext3D::KEEP,
                                                  GraphicsContext3D::KEEP,
                                                  GraphicsContext3D::KEEP);
        checkGLError("stencilOp");
    }
}

void GLES2Canvas::checkGLError(const char* header)
{
#ifndef NDEBUG
    unsigned err;
    while ((err = m_context->getError()) != GraphicsContext3D::NO_ERROR) {
        const char* errorStr = "*** UNKNOWN ERROR ***";
        switch (err) {
        case GraphicsContext3D::INVALID_ENUM:
            errorStr = "GraphicsContext3D::INVALID_ENUM";
            break;
        case GraphicsContext3D::INVALID_VALUE:
            errorStr = "GraphicsContext3D::INVALID_VALUE";
            break;
        case GraphicsContext3D::INVALID_OPERATION:
            errorStr = "GraphicsContext3D::INVALID_OPERATION";
            break;
        case GraphicsContext3D::INVALID_FRAMEBUFFER_OPERATION:
            errorStr = "GraphicsContext3D::INVALID_FRAMEBUFFER_OPERATION";
            break;
        case GraphicsContext3D::OUT_OF_MEMORY:
            errorStr = "GraphicsContext3D::OUT_OF_MEMORY";
            break;
        }
        if (header)
            LOG_ERROR("%s:  %s", header, errorStr);
        else
            LOG_ERROR("%s", errorStr);
    }
#endif
}

}
