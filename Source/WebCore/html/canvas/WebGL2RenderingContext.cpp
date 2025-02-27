/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(WEBGL) && ENABLE(WEBGL2)
#include "WebGL2RenderingContext.h"

#include "CachedImage.h"
#include "EXTTextureFilterAnisotropic.h"
#include "Extensions3D.h"
#include "HTMLCanvasElement.h"
#include "HTMLImageElement.h"
#include "HTMLVideoElement.h"
#include "ImageData.h"
#include "OESTextureFloat.h"
#include "OESTextureFloatLinear.h"
#include "OESTextureHalfFloat.h"
#include "OESTextureHalfFloatLinear.h"
#include "RenderBox.h"
#include "WebGLActiveInfo.h"
#include "WebGLCompressedTextureATC.h"
#include "WebGLCompressedTexturePVRTC.h"
#include "WebGLCompressedTextureS3TC.h"
#include "WebGLContextAttributes.h"
#include "WebGLDebugRendererInfo.h"
#include "WebGLDebugShaders.h"
#include "WebGLDepthTexture.h"
#include "WebGLLoseContext.h"
#include "WebGLQuery.h"
#include "WebGLSampler.h"
#include "WebGLSync.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArrayObject.h"
#include <JavaScriptCore/GenericTypedArrayViewInlines.h>
#include <JavaScriptCore/JSGenericTypedArrayViewInlines.h>

namespace WebCore {

WebGL2RenderingContext::WebGL2RenderingContext(HTMLCanvasElement& passedCanvas, GraphicsContext3D::Attributes attributes)
    : WebGLRenderingContextBase(passedCanvas, attributes)
{
}

WebGL2RenderingContext::WebGL2RenderingContext(HTMLCanvasElement& passedCanvas, RefPtr<GraphicsContext3D>&& context, GraphicsContext3D::Attributes attributes)
    : WebGLRenderingContextBase(passedCanvas, WTFMove(context), attributes)
{
    initializeShaderExtensions();
    initializeVertexArrayObjects();
}

void WebGL2RenderingContext::initializeVertexArrayObjects()
{
    m_defaultVertexArrayObject = WebGLVertexArrayObject::create(*this, WebGLVertexArrayObject::VAOTypeDefault);
    addContextObject(m_defaultVertexArrayObject.get());
    m_boundVertexArrayObject = m_defaultVertexArrayObject;
    if (!isGLES2Compliant())
        initVertexAttrib0();
}

void WebGL2RenderingContext::initializeShaderExtensions()
{
    m_context->getExtensions().ensureEnabled("GL_OES_standard_derivatives");
    m_context->getExtensions().ensureEnabled("GL_EXT_draw_buffers");
    m_context->getExtensions().ensureEnabled("GL_EXT_shader_texture_lod");
    m_context->getExtensions().ensureEnabled("GL_EXT_frag_depth");
}

inline static Optional<unsigned> arrayBufferViewElementSize(const ArrayBufferView& data)
{
    switch (data.getType()) {
    case JSC::NotTypedArray:
    case JSC::TypeDataView:
        return Nullopt;
    case JSC::TypeInt8:
    case JSC::TypeUint8:
    case JSC::TypeUint8Clamped:
    case JSC::TypeInt16:
    case JSC::TypeUint16:
    case JSC::TypeInt32:
    case JSC::TypeUint32:
    case JSC::TypeFloat32:
    case JSC::TypeFloat64:
        return elementSize(data.getType());
    }
}

void WebGL2RenderingContext::bufferData(GC3Denum target, const ArrayBufferView& data, GC3Denum usage, GC3Duint srcOffset, GC3Duint length)
{
    auto optionalElementSize = arrayBufferViewElementSize(data);
    if (!optionalElementSize) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "bufferData", "Invalid type of Array Buffer View");
        return;
    }
    auto elementSize = optionalElementSize.value();
    Checked<GC3Duint, RecordOverflow> checkedElementSize(elementSize);

    Checked<GC3Duint, RecordOverflow> checkedSrcOffset(srcOffset);
    Checked<GC3Duint, RecordOverflow> checkedByteSrcOffset = checkedSrcOffset * checkedElementSize;
    Checked<GC3Duint, RecordOverflow> checkedlength(length);
    Checked<GC3Duint, RecordOverflow> checkedByteLength = checkedlength * checkedElementSize;

    if (checkedByteSrcOffset.hasOverflowed()
        || checkedByteLength.hasOverflowed()
        || checkedByteSrcOffset.unsafeGet() > data.byteLength()
        || checkedByteLength.unsafeGet() > data.byteLength() - checkedByteSrcOffset.unsafeGet()) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "bufferData", "srcOffset or length is out of bounds");
        return;
    }

    auto slice = Uint8Array::create(data.possiblySharedBuffer(), data.byteOffset() + checkedByteSrcOffset.unsafeGet(), checkedByteLength.unsafeGet());
    if (!slice) {
        synthesizeGLError(GraphicsContext3D::OUT_OF_MEMORY, "bufferData", "Could not create intermediate ArrayBufferView");
        return;
    }
    WebGLRenderingContextBase::bufferData(target, BufferDataSource(slice.get()), usage);
}

void WebGL2RenderingContext::bufferSubData(GC3Denum target, long long offset, const ArrayBufferView& data, GC3Duint srcOffset, GC3Duint length)
{
    auto optionalElementSize = arrayBufferViewElementSize(data);
    if (!optionalElementSize) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "bufferSubData", "Invalid type of Array Buffer View");
        return;
    }
    auto elementSize = optionalElementSize.value();
    Checked<GC3Duint, RecordOverflow> checkedElementSize(elementSize);

    Checked<GC3Duint, RecordOverflow> checkedSrcOffset(srcOffset);
    Checked<GC3Duint, RecordOverflow> checkedByteSrcOffset = checkedSrcOffset * checkedElementSize;
    Checked<GC3Duint, RecordOverflow> checkedlength(length);
    Checked<GC3Duint, RecordOverflow> checkedByteLength = checkedlength * checkedElementSize;

    if (checkedByteSrcOffset.hasOverflowed()
        || checkedByteLength.hasOverflowed()
        || checkedByteSrcOffset.unsafeGet() > data.byteLength()
        || checkedByteLength.unsafeGet() > data.byteLength() - checkedByteSrcOffset.unsafeGet()) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "bufferSubData", "srcOffset or length is out of bounds");
        return;
    }

    auto slice = Uint8Array::create(data.possiblySharedBuffer(), data.byteOffset() + checkedByteSrcOffset.unsafeGet(), checkedByteLength.unsafeGet());
    if (!slice) {
        synthesizeGLError(GraphicsContext3D::OUT_OF_MEMORY, "bufferSubData", "Could not create intermediate ArrayBufferView");
        return;
    }

    WebGLRenderingContextBase::bufferSubData(target, offset, BufferDataSource(slice.get()));
}

void WebGL2RenderingContext::copyBufferSubData(GC3Denum readTarget, GC3Denum writeTarget, GC3Dint64 readOffset, GC3Dint64 writeOffset, GC3Dint64 size)
{
    if (isContextLostOrPending())
        return;
    if ((readTarget == GraphicsContext3D::ELEMENT_ARRAY_BUFFER && writeTarget != GraphicsContext3D::ELEMENT_ARRAY_BUFFER)
        || (writeTarget == GraphicsContext3D::ELEMENT_ARRAY_BUFFER && readTarget != GraphicsContext3D::ELEMENT_ARRAY_BUFFER)) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "copyBufferSubData", "Either both targets need to be ELEMENT_ARRAY_BUFFER or neither should be ELEMENT_ARRAY_BUFFER.");
        return;
    }
    if (readOffset < 0 || writeOffset < 0 || size < 0) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "copyBufferSubData", "offset < 0");
        return;
    }
    WebGLBuffer* readBuffer = validateBufferDataParameters("copyBufferSubData", readTarget, GraphicsContext3D::STATIC_DRAW);
    WebGLBuffer* writeBuffer = validateBufferDataParameters("copyBufferSubData", writeTarget, GraphicsContext3D::STATIC_DRAW);
    if (!readBuffer || !writeBuffer) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "copyBufferSubData", "Invalid readTarget or writeTarget");
        return;
    }

    Checked<GC3Dintptr, RecordOverflow> checkedReadOffset(readOffset);
    Checked<GC3Dintptr, RecordOverflow> checkedWriteOffset(writeOffset);
    Checked<GC3Dsizeiptr, RecordOverflow> checkedSize(size);
    if (checkedReadOffset.hasOverflowed() || checkedWriteOffset.hasOverflowed() || checkedSize.hasOverflowed()) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "copyBufferSubData", "Offset or size is too big");
        return;
    }

    if (!writeBuffer->associateCopyBufferSubData(*readBuffer, checkedReadOffset.unsafeGet(), checkedWriteOffset.unsafeGet(), checkedSize.unsafeGet())) {
        this->synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "copyBufferSubData", "offset out of range");
        return;
    }

    m_context->moveErrorsToSyntheticErrorList();
#if PLATFORM(MAC) || PLATFORM(IOS)
    m_context->copyBufferSubData(readTarget, writeTarget, checkedReadOffset.unsafeGet(), checkedWriteOffset.unsafeGet(), checkedSize.unsafeGet());
#endif
    if (m_context->moveErrorsToSyntheticErrorList()) {
        // The bufferSubData function failed. Tell the buffer it doesn't have the data it thinks it does.
        writeBuffer->disassociateBufferData();
    }
}

void WebGL2RenderingContext::getBufferSubData(GC3Denum target, long long srcByteOffset, RefPtr<ArrayBufferView>&& dstData, GC3Duint dstOffset, GC3Duint length)
{
    if (isContextLostOrPending())
        return;
    WebGLBuffer* buffer = validateBufferDataParameters("bufferSubData", target, GraphicsContext3D::STATIC_DRAW);
    if (!buffer) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "No WebGLBuffer is bound to target");
        return;
    }

    // FIXME: Implement "If target is TRANSFORM_FEEDBACK_BUFFER, and any transform feedback object is currently active, an INVALID_OPERATION error is generated."

    if (!dstData) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "Null dstData");
        return;
    }

    auto optionalElementSize = arrayBufferViewElementSize(*dstData);
    if (!optionalElementSize) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "Invalid type of Array Buffer View");
        return;
    }
    auto elementSize = optionalElementSize.value();
    auto dstDataLength = dstData->byteLength() / elementSize;

    if (dstOffset > dstDataLength) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "dstOffset is larger than the length of the destination buffer.");
        return;
    }

    GC3Duint copyLength = length ? length : dstDataLength - dstOffset;

    Checked<GC3Duint, RecordOverflow> checkedDstOffset(dstOffset);
    Checked<GC3Duint, RecordOverflow> checkedCopyLength(copyLength);
    auto checkedDestinationEnd = checkedDstOffset + checkedCopyLength;
    if (checkedDestinationEnd.hasOverflowed()) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "dstOffset + copyLength is too high");
        return;
    }

    if (checkedDestinationEnd.unsafeGet() > dstDataLength) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "end of written destination is past the end of the buffer");
        return;
    }

    if (srcByteOffset < 0) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "srcByteOffset is less than 0");
        return;
    }

    Checked<GC3Dintptr, RecordOverflow> checkedSrcByteOffset(srcByteOffset);
    Checked<GC3Dintptr, RecordOverflow> checkedCopyLengthPtr(copyLength);
    Checked<GC3Dintptr, RecordOverflow> checkedElementSize(elementSize);
    auto checkedSourceEnd = checkedSrcByteOffset + checkedCopyLengthPtr * checkedElementSize;
    if (checkedSourceEnd.hasOverflowed() || checkedSourceEnd.unsafeGet() > buffer->byteLength()) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getBufferSubData", "Parameters would read outside the bounds of the source buffer");
        return;
    }

    m_context->moveErrorsToSyntheticErrorList();
#if PLATFORM(MAC) || PLATFORM(IOS)
    // FIXME: Coalesce multiple getBufferSubData() calls to use a single map() call
    void* ptr = m_context->mapBufferRange(target, checkedSrcByteOffset.unsafeGet(), static_cast<GC3Dsizeiptr>(checkedCopyLengthPtr.unsafeGet() * checkedElementSize.unsafeGet()), GraphicsContext3D::MAP_READ_BIT);
    memcpy(static_cast<char*>(dstData->baseAddress()) + dstData->byteOffset() + dstOffset * elementSize, ptr, copyLength * elementSize);
    bool success = m_context->unmapBuffer(target);
    ASSERT_UNUSED(success, success);
#endif
    m_context->moveErrorsToSyntheticErrorList();
}

void WebGL2RenderingContext::blitFramebuffer(GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dbitfield, GC3Denum)
{
}

void WebGL2RenderingContext::framebufferTextureLayer(GC3Denum, GC3Denum, GC3Duint, GC3Dint, GC3Dint)
{
}

WebGLGetInfo WebGL2RenderingContext::getInternalformatParameter(GC3Denum, GC3Denum, GC3Denum)
{
    return WebGLGetInfo();
}

void WebGL2RenderingContext::invalidateFramebuffer(GC3Denum, Vector<GC3Denum>)
{
}

void WebGL2RenderingContext::invalidateSubFramebuffer(GC3Denum, Vector<GC3Denum>, GC3Dint, GC3Dint, GC3Dsizei, GC3Dsizei)
{
}

void WebGL2RenderingContext::readBuffer(GC3Denum)
{
}

void WebGL2RenderingContext::renderbufferStorageMultisample(GC3Denum, GC3Dsizei, GC3Denum, GC3Dsizei, GC3Dsizei)
{
}

bool WebGL2RenderingContext::validateTexStorageFuncParameters(GC3Denum target, GC3Dsizei levels, GC3Denum internalFormat, GC3Dsizei width, GC3Dsizei height, const char* functionName)
{
    if (width < 0 || height < 0) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, functionName, "width or height < 0");
        return false;
    }

    if (width > m_maxTextureSize || height > m_maxTextureSize) {
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "texture dimensions are larger than the maximum texture size");
        return false;
    }

    if (target == GraphicsContext3D::TEXTURE_CUBE_MAP) {
        if (width != height) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, functionName, "width != height for cube map");
            return false;
        }
    } else if (target != GraphicsContext3D::TEXTURE_2D) {
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "invalid target");
        return false;
    }

    if (levels < 0 || levels > m_maxTextureLevel) {
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "number of levels is out of bounds");
        return false;
    }

    switch (internalFormat) {
    case GraphicsContext3D::R8:
    case GraphicsContext3D::R8_SNORM:
    case GraphicsContext3D::R16F:
    case GraphicsContext3D::R32F:
    case GraphicsContext3D::R8UI:
    case GraphicsContext3D::R8I:
    case GraphicsContext3D::R16UI:
    case GraphicsContext3D::R16I:
    case GraphicsContext3D::R32UI:
    case GraphicsContext3D::R32I:
    case GraphicsContext3D::RG8:
    case GraphicsContext3D::RG8_SNORM:
    case GraphicsContext3D::RG16F:
    case GraphicsContext3D::RG32F:
    case GraphicsContext3D::RG8UI:
    case GraphicsContext3D::RG8I:
    case GraphicsContext3D::RG16UI:
    case GraphicsContext3D::RG16I:
    case GraphicsContext3D::RG32UI:
    case GraphicsContext3D::RG32I:
    case GraphicsContext3D::RGB8:
    case GraphicsContext3D::SRGB8:
    case GraphicsContext3D::RGB565:
    case GraphicsContext3D::RGB8_SNORM:
    case GraphicsContext3D::R11F_G11F_B10F:
    case GraphicsContext3D::RGB9_E5:
    case GraphicsContext3D::RGB16F:
    case GraphicsContext3D::RGB32F:
    case GraphicsContext3D::RGB8UI:
    case GraphicsContext3D::RGB8I:
    case GraphicsContext3D::RGB16UI:
    case GraphicsContext3D::RGB16I:
    case GraphicsContext3D::RGB32UI:
    case GraphicsContext3D::RGB32I:
    case GraphicsContext3D::RGBA8:
    case GraphicsContext3D::SRGB8_ALPHA8:
    case GraphicsContext3D::RGBA8_SNORM:
    case GraphicsContext3D::RGB5_A1:
    case GraphicsContext3D::RGBA4:
    case GraphicsContext3D::RGB10_A2:
    case GraphicsContext3D::RGBA16F:
    case GraphicsContext3D::RGBA32F:
    case GraphicsContext3D::RGBA8UI:
    case GraphicsContext3D::RGBA8I:
    case GraphicsContext3D::RGB10_A2UI:
    case GraphicsContext3D::RGBA16UI:
    case GraphicsContext3D::RGBA16I:
    case GraphicsContext3D::RGBA32I:
    case GraphicsContext3D::RGBA32UI:
    case GraphicsContext3D::DEPTH_COMPONENT16:
    case GraphicsContext3D::DEPTH_COMPONENT24:
    case GraphicsContext3D::DEPTH_COMPONENT32F:
    case GraphicsContext3D::DEPTH24_STENCIL8:
    case GraphicsContext3D::DEPTH32F_STENCIL8:
        break;
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "Unknown internalFormat");
        return false;
    }

    return true;
}

void WebGL2RenderingContext::texStorage2D(GC3Denum target, GC3Dsizei levels, GC3Denum internalFormat, GC3Dsizei width, GC3Dsizei height)
{
    if (isContextLostOrPending())
        return;

    WebGLTexture* texture = validateTextureBinding("texStorage2D", target, false);
    if (!texture)
        return;

    if (!validateTexStorageFuncParameters(target, levels, internalFormat, width, height, "texStorage2D"))
        return;

    if (!validateNPOTTextureLevel(width, height, levels, "texStorage2D"))
        return;

    if (texture->immutable()) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "texStorage2D", "texStorage2D already called on this texture");
        return;
    }
    texture->setImmutable();

    m_context->texStorage2D(target, levels, internalFormat, width, height);

    {
        GC3Denum format;
        GC3Denum type;
        if (!GraphicsContext3D::possibleFormatAndTypeForInternalFormat(internalFormat, format, type)) {
            synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "texStorage2D", "Texture has unknown internal format");
            return;
        }

        GC3Dsizei levelWidth = width;
        GC3Dsizei levelHeight = height;

        unsigned size;
        GC3Denum error = m_context->computeImageSizeInBytes(format, type, width, height, m_unpackAlignment, &size, nullptr);
        if (error != GraphicsContext3D::NO_ERROR) {
            synthesizeGLError(error, "texStorage2D", "bad dimensions");
            return;
        }

        Vector<char> data(size);
        memset(data.data(), 0, size);

        for (GC3Dsizei level = 0; level < levels; ++level) {
            if (target == GraphicsContext3D::TEXTURE_CUBE_MAP) {
                m_context->texSubImage2D(GraphicsContext3D::TEXTURE_CUBE_MAP_POSITIVE_X, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
                m_context->texSubImage2D(GraphicsContext3D::TEXTURE_CUBE_MAP_NEGATIVE_X, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
                m_context->texSubImage2D(GraphicsContext3D::TEXTURE_CUBE_MAP_POSITIVE_Y, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
                m_context->texSubImage2D(GraphicsContext3D::TEXTURE_CUBE_MAP_NEGATIVE_Y, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
                m_context->texSubImage2D(GraphicsContext3D::TEXTURE_CUBE_MAP_POSITIVE_Z, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
                m_context->texSubImage2D(GraphicsContext3D::TEXTURE_CUBE_MAP_NEGATIVE_Z, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
            } else
                m_context->texSubImage2D(target, level, 0, 0, levelWidth, levelHeight, format, type, data.data());
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
        }
    }

    for (GC3Dsizei level = 0; level < levels; ++level) {
        if (target == GraphicsContext3D::TEXTURE_CUBE_MAP) {
            texture->setLevelInfo(GraphicsContext3D::TEXTURE_CUBE_MAP_POSITIVE_X, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
            texture->setLevelInfo(GraphicsContext3D::TEXTURE_CUBE_MAP_NEGATIVE_X, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
            texture->setLevelInfo(GraphicsContext3D::TEXTURE_CUBE_MAP_POSITIVE_Y, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
            texture->setLevelInfo(GraphicsContext3D::TEXTURE_CUBE_MAP_NEGATIVE_Y, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
            texture->setLevelInfo(GraphicsContext3D::TEXTURE_CUBE_MAP_POSITIVE_Z, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
            texture->setLevelInfo(GraphicsContext3D::TEXTURE_CUBE_MAP_NEGATIVE_Z, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
        } else
            texture->setLevelInfo(target, level, internalFormat, width, height, GraphicsContext3D::UNSIGNED_BYTE);
    }
}

void WebGL2RenderingContext::texStorage3D(GC3Denum, GC3Dsizei, GC3Denum, GC3Dsizei, GC3Dsizei, GC3Dsizei)
{
}

void WebGL2RenderingContext::texImage3D(GC3Denum, GC3Dint, GC3Dint, GC3Dsizei, GC3Dsizei, GC3Dsizei, GC3Dint, GC3Denum, GC3Denum, RefPtr<ArrayBufferView>&&)
{
}

void WebGL2RenderingContext::texSubImage3D(GC3Denum, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dsizei, GC3Dsizei, GC3Dsizei, GC3Denum, GC3Denum, RefPtr<ArrayBufferView>&&)
{
}

void WebGL2RenderingContext::texSubImage3D(GC3Denum, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Denum, GC3Denum, TexImageSource&&)
{
}

void WebGL2RenderingContext::copyTexSubImage3D(GC3Denum, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dsizei, GC3Dsizei)
{
}

void WebGL2RenderingContext::compressedTexImage3D(GC3Denum, GC3Dint, GC3Denum, GC3Dsizei, GC3Dsizei, GC3Dsizei, GC3Dint, GC3Dsizei, RefPtr<ArrayBufferView>&&)
{
}

void WebGL2RenderingContext::compressedTexSubImage3D(GC3Denum, GC3Dint, GC3Dint, GC3Dint, GC3Dint, GC3Dsizei, GC3Dsizei, GC3Dsizei, GC3Denum, GC3Dsizei, RefPtr<ArrayBufferView>&&)
{
}

GC3Dint WebGL2RenderingContext::getFragDataLocation(WebGLProgram*, String)
{
    return 0;
}

void WebGL2RenderingContext::uniform1ui(WebGLUniformLocation*, GC3Duint)
{
}

void WebGL2RenderingContext::uniform2ui(WebGLUniformLocation*, GC3Duint, GC3Duint)
{
}

void WebGL2RenderingContext::uniform3ui(WebGLUniformLocation*, GC3Duint, GC3Duint, GC3Duint)
{
}

void WebGL2RenderingContext::uniform4ui(WebGLUniformLocation*, GC3Duint, GC3Duint, GC3Duint, GC3Duint)
{
}

void WebGL2RenderingContext::uniform1uiv(WebGLUniformLocation*, RefPtr<Uint32Array>&&)
{
}

void WebGL2RenderingContext::uniform2uiv(WebGLUniformLocation*, RefPtr<Uint32Array>&&)
{
}

void WebGL2RenderingContext::uniform3uiv(WebGLUniformLocation*, RefPtr<Uint32Array>&&)
{
}

void WebGL2RenderingContext::uniform4uiv(WebGLUniformLocation*, RefPtr<Uint32Array>&&)
{
}

void WebGL2RenderingContext::uniformMatrix2x3fv(WebGLUniformLocation*, GC3Dboolean, RefPtr<Float32Array>&&)
{
}

void WebGL2RenderingContext::uniformMatrix3x2fv(WebGLUniformLocation*, GC3Dboolean, RefPtr<Float32Array>&&)
{
}

void WebGL2RenderingContext::uniformMatrix2x4fv(WebGLUniformLocation*, GC3Dboolean, RefPtr<Float32Array>&&)
{
}

void WebGL2RenderingContext::uniformMatrix4x2fv(WebGLUniformLocation*, GC3Dboolean, RefPtr<Float32Array>&&)
{
}

void WebGL2RenderingContext::uniformMatrix3x4fv(WebGLUniformLocation*, GC3Dboolean, RefPtr<Float32Array>&&)
{
}

void WebGL2RenderingContext::uniformMatrix4x3fv(WebGLUniformLocation*, GC3Dboolean, RefPtr<Float32Array>&&)
{
}

void WebGL2RenderingContext::vertexAttribI4i(GC3Duint, GC3Dint, GC3Dint, GC3Dint, GC3Dint)
{
}

void WebGL2RenderingContext::vertexAttribI4iv(GC3Duint, RefPtr<Int32Array>&&)
{
}

void WebGL2RenderingContext::vertexAttribI4ui(GC3Duint, GC3Duint, GC3Duint, GC3Duint, GC3Duint)
{
}

void WebGL2RenderingContext::vertexAttribI4uiv(GC3Duint, RefPtr<Uint32Array>&&)
{
}

void WebGL2RenderingContext::vertexAttribIPointer(GC3Duint, GC3Dint, GC3Denum, GC3Dsizei, GC3Dint64)
{
}

void WebGL2RenderingContext::clear(GC3Dbitfield mask)
{
    if (isContextLostOrPending())
        return;
    if (mask & ~(GraphicsContext3D::COLOR_BUFFER_BIT | GraphicsContext3D::DEPTH_BUFFER_BIT | GraphicsContext3D::STENCIL_BUFFER_BIT)) {
        synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clear", "invalid mask");
        return;
    }
    if (m_framebufferBinding && (mask & GraphicsContext3D::COLOR_BUFFER_BIT) && isIntegerFormat(m_framebufferBinding->getColorBufferFormat())) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "clear", "cannot clear an integer buffer");
        return;
    }
    const char* reason = "framebuffer incomplete";
    if (m_framebufferBinding && !m_framebufferBinding->onAccess(graphicsContext3D(), &reason)) {
        synthesizeGLError(GraphicsContext3D::INVALID_FRAMEBUFFER_OPERATION, "clear", reason);
        return;
    }
    if (!clearIfComposited(mask))
        m_context->clear(mask);
    markContextChanged();
}

void WebGL2RenderingContext::vertexAttribDivisor(GC3Duint index, GC3Duint divisor)
{
    if (isContextLostOrPending())
        return;
    
    WebGLRenderingContextBase::vertexAttribDivisor(index, divisor);
}

void WebGL2RenderingContext::drawArraysInstanced(GC3Denum mode, GC3Dint first, GC3Dsizei count, GC3Dsizei instanceCount)
{
    if (isContextLostOrPending())
        return;

    WebGLRenderingContextBase::drawArraysInstanced(mode, first, count, instanceCount);
}

void WebGL2RenderingContext::drawElementsInstanced(GC3Denum mode, GC3Dsizei count, GC3Denum type, GC3Dint64 offset, GC3Dsizei instanceCount)
{
    if (isContextLostOrPending())
        return;

    WebGLRenderingContextBase::drawElementsInstanced(mode, count, type, offset, instanceCount);
}

void WebGL2RenderingContext::drawRangeElements(GC3Denum, GC3Duint, GC3Duint, GC3Dsizei, GC3Denum, GC3Dint64)
{
}

void WebGL2RenderingContext::drawBuffers(Vector<GC3Denum> buffers)
{
    if (isContextLost())
        return;
    GC3Dsizei n = buffers.size();
    const GC3Denum* bufs = buffers.data();
    if (!m_framebufferBinding) {
        if (n != 1) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "drawBuffers", "more than one buffer");
            return;
        }
        if (bufs[0] != GraphicsContext3D::BACK && bufs[0] != GraphicsContext3D::NONE) {
            synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "drawBuffers", "BACK or NONE");
            return;
        }
        // Because the backbuffer is simulated on all current WebKit ports, we need to change BACK to COLOR_ATTACHMENT0.
        GC3Denum value = (bufs[0] == GraphicsContext3D::BACK) ? GraphicsContext3D::COLOR_ATTACHMENT0 : GraphicsContext3D::NONE;
        graphicsContext3D()->getExtensions().drawBuffersEXT(1, &value);
        setBackDrawBuffer(bufs[0]);
    } else {
        if (n > getMaxDrawBuffers()) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "drawBuffers", "more than max draw buffers");
            return;
        }
        for (GC3Dsizei i = 0; i < n; ++i) {
            if (bufs[i] != GraphicsContext3D::NONE && bufs[i] != static_cast<GC3Denum>(GraphicsContext3D::COLOR_ATTACHMENT0 + i)) {
                synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "drawBuffers", "COLOR_ATTACHMENTi or NONE");
                return;
            }
        }
        m_framebufferBinding->drawBuffers(buffers);
    }
}

void WebGL2RenderingContext::clearBufferiv(GC3Denum buffer, GC3Dint drawbuffer, RefPtr<Int32Array>&&)
{
    switch (buffer) {
    case GraphicsContext3D::COLOR:
        if (drawbuffer < 0 || drawbuffer >= getMaxDrawBuffers()) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clearBufferiv", "buffer index out of range");
            return;
        }
        // TODO: Call clearBufferiv, requires gl3.h and ES3/gl.h
        break;
    case GraphicsContext3D::STENCIL:
        if (drawbuffer) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clearBufferiv", "buffer index must be 0");
            return;
        }
        // TODO: Call clearBufferiv, requires gl3.h and ES3/gl.h
        break;
    case GraphicsContext3D::DEPTH:
    case GraphicsContext3D::DEPTH_STENCIL:
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "clearBufferiv", "buffer argument must be COLOR or STENCIL");
        break;
    }
}

void WebGL2RenderingContext::clearBufferuiv(GC3Denum buffer, GC3Dint drawbuffer, RefPtr<Uint32Array>&&)
{
    switch (buffer) {
    case GraphicsContext3D::COLOR:
        if (drawbuffer < 0 || drawbuffer >= getMaxDrawBuffers()) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clearBufferuiv", "buffer index out of range");
            return;
        }
        // TODO: Call clearBufferuiv, requires gl3.h and ES3/gl.h
        break;
    case GraphicsContext3D::DEPTH:
    case GraphicsContext3D::STENCIL:
    case GraphicsContext3D::DEPTH_STENCIL:
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "clearBufferuiv", "buffer argument must be COLOR");
        break;
    }
}

void WebGL2RenderingContext::clearBufferfv(GC3Denum buffer, GC3Dint drawbuffer, RefPtr<Float32Array>&&)
{
    switch (buffer) {
    case GraphicsContext3D::COLOR:
        if (drawbuffer < 0 || drawbuffer >= getMaxDrawBuffers()) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clearBufferfv", "buffer index out of range");
            return;
        }
        // TODO: Call clearBufferfv, requires gl3.h and ES3/gl.h
        break;
    case GraphicsContext3D::DEPTH:
        if (drawbuffer) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clearBufferfv", "buffer index must be 0");
            return;
        }
        // TODO: Call clearBufferfv, requires gl3.h and ES3/gl.h
        break;
    case GraphicsContext3D::STENCIL:
    case GraphicsContext3D::DEPTH_STENCIL:
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "clearBufferfv", "buffer argument must be COLOR OR DEPTH");
        break;
    }
}

void WebGL2RenderingContext::clearBufferfi(GC3Denum buffer, GC3Dint drawbuffer, GC3Dfloat, GC3Dint)
{
    switch (buffer) {
    case GraphicsContext3D::DEPTH_STENCIL:
        if (drawbuffer) {
            synthesizeGLError(GraphicsContext3D::INVALID_VALUE, "clearBufferfv", "buffer index must be 0");
            return;
        }
        // TODO: Call clearBufferfi, requires gl3.h and ES3/gl.h
        break;
    case GraphicsContext3D::COLOR:
    case GraphicsContext3D::DEPTH:
    case GraphicsContext3D::STENCIL:
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "clearBufferfv", "buffer argument must be DEPTH_STENCIL");
        break;
    }
}

RefPtr<WebGLQuery> WebGL2RenderingContext::createQuery()
{
    return nullptr;
}

void WebGL2RenderingContext::deleteQuery(WebGLQuery*)
{
}

GC3Dboolean WebGL2RenderingContext::isQuery(WebGLQuery*)
{
    return false;
}

void WebGL2RenderingContext::beginQuery(GC3Denum, WebGLQuery*)
{
}

void WebGL2RenderingContext::endQuery(GC3Denum)
{
}

RefPtr<WebGLQuery> WebGL2RenderingContext::getQuery(GC3Denum, GC3Denum)
{
    return nullptr;
}

WebGLGetInfo WebGL2RenderingContext::getQueryParameter(WebGLQuery*, GC3Denum)
{
    return WebGLGetInfo();
}

RefPtr<WebGLSampler> WebGL2RenderingContext::createSampler()
{
    return nullptr;
}

void WebGL2RenderingContext::deleteSampler(WebGLSampler*)
{
}

GC3Dboolean WebGL2RenderingContext::isSampler(WebGLSampler*)
{
    return false;
}

void WebGL2RenderingContext::bindSampler(GC3Duint, WebGLSampler*)
{
}

void WebGL2RenderingContext::samplerParameteri(WebGLSampler*, GC3Denum, GC3Dint)
{
}

void WebGL2RenderingContext::samplerParameterf(WebGLSampler*, GC3Denum, GC3Dfloat)
{
}

WebGLGetInfo WebGL2RenderingContext::getSamplerParameter(WebGLSampler*, GC3Denum)
{
    return WebGLGetInfo();
}

RefPtr<WebGLSync> WebGL2RenderingContext::fenceSync(GC3Denum, GC3Dbitfield)
{
    return nullptr;
}

GC3Dboolean WebGL2RenderingContext::isSync(WebGLSync*)
{
    return false;
}

void WebGL2RenderingContext::deleteSync(WebGLSync*)
{
}

GC3Denum WebGL2RenderingContext::clientWaitSync(WebGLSync*, GC3Dbitfield, GC3Duint64)
{
    return 0;
}

void WebGL2RenderingContext::waitSync(WebGLSync*, GC3Dbitfield, GC3Duint64)
{
}

WebGLGetInfo WebGL2RenderingContext::getSyncParameter(WebGLSync*, GC3Denum)
{
    return WebGLGetInfo();
}

RefPtr<WebGLTransformFeedback> WebGL2RenderingContext::createTransformFeedback()
{
    return nullptr;
}

void WebGL2RenderingContext::deleteTransformFeedback(WebGLTransformFeedback*)
{
}

GC3Dboolean WebGL2RenderingContext::isTransformFeedback(WebGLTransformFeedback*)
{
    return false;
}

void WebGL2RenderingContext::bindTransformFeedback(GC3Denum, WebGLTransformFeedback*)
{
}

void WebGL2RenderingContext::beginTransformFeedback(GC3Denum)
{
}

void WebGL2RenderingContext::endTransformFeedback()
{
}

void WebGL2RenderingContext::transformFeedbackVaryings(WebGLProgram*, Vector<String>, GC3Denum)
{
}

RefPtr<WebGLActiveInfo> WebGL2RenderingContext::getTransformFeedbackVarying(WebGLProgram*, GC3Duint)
{
    return nullptr;
}

void WebGL2RenderingContext::pauseTransformFeedback()
{
}

void WebGL2RenderingContext::resumeTransformFeedback()
{
}

void WebGL2RenderingContext::bindBufferBase(GC3Denum, GC3Duint, WebGLBuffer*)
{
}

void WebGL2RenderingContext::bindBufferRange(GC3Denum, GC3Duint, WebGLBuffer*, GC3Dint64, GC3Dint64)
{
}

WebGLGetInfo WebGL2RenderingContext::getIndexedParameter(GC3Denum target, GC3Duint)
{
    switch (target) {
    case GraphicsContext3D::TRANSFORM_FEEDBACK_BUFFER_BINDING:
    case GraphicsContext3D::TRANSFORM_FEEDBACK_BUFFER_SIZE:
    case GraphicsContext3D::TRANSFORM_FEEDBACK_BUFFER_START:
    case GraphicsContext3D::UNIFORM_BUFFER_BINDING:
    case GraphicsContext3D::UNIFORM_BUFFER_SIZE:
    case GraphicsContext3D::UNIFORM_BUFFER_START:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getIndexedParameter", "parameter name not yet supported");
        return WebGLGetInfo();
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getIndexedParameter", "invalid parameter name");
        return WebGLGetInfo();
            
    }
    return WebGLGetInfo();
}

Uint32Array* WebGL2RenderingContext::getUniformIndices(WebGLProgram*, Vector<String>)
{
    return nullptr;
}

Int32Array* WebGL2RenderingContext::getActiveUniforms(WebGLProgram*, RefPtr<Uint32Array>&&, GC3Denum)
{
    return nullptr;
}

GC3Duint WebGL2RenderingContext::getUniformBlockIndex(WebGLProgram*, String)
{
    return 0;
}

WebGLGetInfo WebGL2RenderingContext::getActiveUniformBlockParameter(WebGLProgram*, GC3Duint, GC3Denum)
{
    return WebGLGetInfo();
}

WebGLGetInfo WebGL2RenderingContext::getActiveUniformBlockName(WebGLProgram*, GC3Duint)
{
    return WebGLGetInfo();
}

void WebGL2RenderingContext::uniformBlockBinding(WebGLProgram*, GC3Duint, GC3Duint)
{
}

RefPtr<WebGLVertexArrayObject> WebGL2RenderingContext::createVertexArray()
{
    if (isContextLost())
        return nullptr;
    
    auto object = WebGLVertexArrayObject::create(*this, WebGLVertexArrayObject::VAOTypeUser);
    addContextObject(object.ptr());
    return WTFMove(object);
}

void WebGL2RenderingContext::deleteVertexArray(WebGLVertexArrayObject* arrayObject)
{
    if (!arrayObject || isContextLost())
        return;
    
    if (arrayObject->isDeleted())
        return;
    
    if (!arrayObject->isDefaultObject() && arrayObject == m_boundVertexArrayObject)
        setBoundVertexArrayObject(0);
    
    arrayObject->deleteObject(graphicsContext3D());
}

GC3Dboolean WebGL2RenderingContext::isVertexArray(WebGLVertexArrayObject* arrayObject)
{
    if (!arrayObject || isContextLost())
        return 0;
    
    if (!arrayObject->hasEverBeenBound() || !arrayObject->validate(0, *this))
        return 0;
    
    return m_context->isVertexArray(arrayObject->object());
}

void WebGL2RenderingContext::bindVertexArray(WebGLVertexArrayObject* arrayObject)
{
    if (isContextLost())
        return;
    
    if (arrayObject && (arrayObject->isDeleted() || !arrayObject->validate(0, *this) || !m_contextObjects.contains(arrayObject))) {
        m_context->synthesizeGLError(GraphicsContext3D::INVALID_OPERATION);
        return;
    }
    if (arrayObject && !arrayObject->isDefaultObject() && arrayObject->object()) {
        m_context->bindVertexArray(arrayObject->object());
        
        arrayObject->setHasEverBeenBound();
        setBoundVertexArrayObject(arrayObject);
    } else {
        m_context->bindVertexArray(m_defaultVertexArrayObject->object());
        setBoundVertexArrayObject(m_defaultVertexArrayObject);
    }
}

WebGLExtension* WebGL2RenderingContext::getExtension(const String& name)
{
    if (isContextLostOrPending())
        return nullptr;

    if ((equalIgnoringASCIICase(name, "EXT_texture_filter_anisotropic") || equalIgnoringASCIICase(name, "WEBKIT_EXT_texture_filter_anisotropic"))
        && m_context->getExtensions().supports("GL_EXT_texture_filter_anisotropic")) {
        if (!m_extTextureFilterAnisotropic) {
            m_context->getExtensions().ensureEnabled("GL_EXT_texture_filter_anisotropic");
            m_extTextureFilterAnisotropic = std::make_unique<EXTTextureFilterAnisotropic>(*this);
        }
        return m_extTextureFilterAnisotropic.get();
    }
    if (equalIgnoringASCIICase(name, "OES_texture_float")
        && m_context->getExtensions().supports("GL_OES_texture_float")) {
        if (!m_oesTextureFloat) {
            m_context->getExtensions().ensureEnabled("GL_OES_texture_float");
            m_oesTextureFloat = std::make_unique<OESTextureFloat>(*this);
        }
        return m_oesTextureFloat.get();
    }
    if (equalIgnoringASCIICase(name, "OES_texture_float_linear")
        && m_context->getExtensions().supports("GL_OES_texture_float_linear")) {
        if (!m_oesTextureFloatLinear) {
            m_context->getExtensions().ensureEnabled("GL_OES_texture_float_linear");
            m_oesTextureFloatLinear = std::make_unique<OESTextureFloatLinear>(*this);
        }
        return m_oesTextureFloatLinear.get();
    }
    if (equalIgnoringASCIICase(name, "OES_texture_half_float")
        && m_context->getExtensions().supports("GL_OES_texture_half_float")) {
        if (!m_oesTextureHalfFloat) {
            m_context->getExtensions().ensureEnabled("GL_OES_texture_half_float");
            m_oesTextureHalfFloat = std::make_unique<OESTextureHalfFloat>(*this);
        }
        return m_oesTextureHalfFloat.get();
    }
    if (equalIgnoringASCIICase(name, "OES_texture_half_float_linear")
        && m_context->getExtensions().supports("GL_OES_texture_half_float_linear")) {
        if (!m_oesTextureHalfFloatLinear) {
            m_context->getExtensions().ensureEnabled("GL_OES_texture_half_float_linear");
            m_oesTextureHalfFloatLinear = std::make_unique<OESTextureHalfFloatLinear>(*this);
        }
        return m_oesTextureHalfFloatLinear.get();
    }
    if (equalIgnoringASCIICase(name, "WEBGL_lose_context")) {
        if (!m_webglLoseContext)
            m_webglLoseContext = std::make_unique<WebGLLoseContext>(*this);
        return m_webglLoseContext.get();
    }
    if ((equalIgnoringASCIICase(name, "WEBKIT_WEBGL_compressed_texture_atc"))
        && WebGLCompressedTextureATC::supported(*this)) {
        if (!m_webglCompressedTextureATC)
            m_webglCompressedTextureATC = std::make_unique<WebGLCompressedTextureATC>(*this);
        return m_webglCompressedTextureATC.get();
    }
    if ((equalIgnoringASCIICase(name, "WEBKIT_WEBGL_compressed_texture_pvrtc"))
        && WebGLCompressedTexturePVRTC::supported(*this)) {
        if (!m_webglCompressedTexturePVRTC)
            m_webglCompressedTexturePVRTC = std::make_unique<WebGLCompressedTexturePVRTC>(*this);
        return m_webglCompressedTexturePVRTC.get();
    }
    if (equalIgnoringASCIICase(name, "WEBGL_compressed_texture_s3tc")
        && WebGLCompressedTextureS3TC::supported(*this)) {
        if (!m_webglCompressedTextureS3TC)
            m_webglCompressedTextureS3TC = std::make_unique<WebGLCompressedTextureS3TC>(*this);
        return m_webglCompressedTextureS3TC.get();
    }
    if (equalIgnoringASCIICase(name, "WEBGL_depth_texture")
        && WebGLDepthTexture::supported(*graphicsContext3D())) {
        if (!m_webglDepthTexture)
            m_webglDepthTexture = std::make_unique<WebGLDepthTexture>(*this);
        return m_webglDepthTexture.get();
    }
    if (equalIgnoringASCIICase(name, "WEBGL_debug_renderer_info")) {
        if (!m_webglDebugRendererInfo)
            m_webglDebugRendererInfo = std::make_unique<WebGLDebugRendererInfo>(*this);
        return m_webglDebugRendererInfo.get();
    }
    if (equalIgnoringASCIICase(name, "WEBGL_debug_shaders")
        && m_context->getExtensions().supports("GL_ANGLE_translated_shader_source")) {
        if (!m_webglDebugShaders)
            m_webglDebugShaders = std::make_unique<WebGLDebugShaders>(*this);
        return m_webglDebugShaders.get();
    }

    return nullptr;
}

Vector<String> WebGL2RenderingContext::getSupportedExtensions()
{
    Vector<String> result;
    
    if (m_isPendingPolicyResolution)
        return result;

    if (m_context->getExtensions().supports("GL_OES_texture_float"))
        result.append("OES_texture_float");
    if (m_context->getExtensions().supports("GL_OES_texture_float_linear"))
        result.append("OES_texture_float_linear");
    if (m_context->getExtensions().supports("GL_OES_texture_half_float"))
        result.append("OES_texture_half_float");
    if (m_context->getExtensions().supports("GL_OES_texture_half_float_linear"))
        result.append("OES_texture_half_float_linear");
    if (m_context->getExtensions().supports("GL_EXT_texture_filter_anisotropic"))
        result.append("WEBKIT_EXT_texture_filter_anisotropic");
    if (WebGLCompressedTextureATC::supported(*this))
        result.append("WEBKIT_WEBGL_compressed_texture_atc");
    if (WebGLCompressedTexturePVRTC::supported(*this))
        result.append("WEBKIT_WEBGL_compressed_texture_pvrtc");
    if (WebGLCompressedTextureS3TC::supported(*this))
        result.append("WEBGL_compressed_texture_s3tc");
    if (WebGLDepthTexture::supported(*graphicsContext3D()))
        result.append("WEBGL_depth_texture");
    result.append("WEBGL_lose_context");
    if (m_context->getExtensions().supports("GL_ANGLE_translated_shader_source"))
        result.append("WEBGL_debug_shaders");
    result.append("WEBGL_debug_renderer_info");

    return result;
}

WebGLGetInfo WebGL2RenderingContext::getFramebufferAttachmentParameter(GC3Denum target, GC3Denum attachment, GC3Denum pname)
{
    if (isContextLostOrPending() || !validateFramebufferFuncParameters("getFramebufferAttachmentParameter", target, attachment))
        return WebGLGetInfo();
    
    if (!m_framebufferBinding || !m_framebufferBinding->object()) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "getFramebufferAttachmentParameter", "no framebuffer bound");
        return WebGLGetInfo();
    }
    
    WebGLSharedObject* object = m_framebufferBinding->getAttachmentObject(attachment);
    if (!object) {
        if (pname == GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE)
            return WebGLGetInfo(GraphicsContext3D::NONE);
        // OpenGL ES 2.0 specifies INVALID_ENUM in this case, while desktop GL
        // specifies INVALID_OPERATION.
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getFramebufferAttachmentParameter", "invalid parameter name");
        return WebGLGetInfo();
    }
    
    ASSERT(object->isTexture() || object->isRenderbuffer());
    if (object->isTexture()) {
        switch (pname) {
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            return WebGLGetInfo(GraphicsContext3D::TEXTURE);
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            return WebGLGetInfo(reinterpret_cast<WebGLTexture*>(object));
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING: {
            GC3Dint value = 0;
            m_context->getFramebufferAttachmentParameteriv(target, attachment, pname, &value);
            return WebGLGetInfo(value);
        }
        default:
            synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getFramebufferAttachmentParameter", "invalid parameter name for texture attachment");
            return WebGLGetInfo();
        }
    } else {
        switch (pname) {
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            return WebGLGetInfo(GraphicsContext3D::RENDERBUFFER);
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            return WebGLGetInfo(reinterpret_cast<WebGLRenderbuffer*>(object));
        case GraphicsContext3D::FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING : {
            WebGLRenderbuffer* renderBuffer = reinterpret_cast<WebGLRenderbuffer*>(object);
            GC3Denum renderBufferFormat = renderBuffer->getInternalFormat();
            if (renderBufferFormat == GraphicsContext3D::SRGB8_ALPHA8
                || renderBufferFormat == GraphicsContext3D::COMPRESSED_SRGB8_ETC2
                || renderBufferFormat == GraphicsContext3D::COMPRESSED_SRGB8_ALPHA8_ETC2_EAC
                || renderBufferFormat == GraphicsContext3D::COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2) {
                return WebGLGetInfo(GraphicsContext3D::SRGB);
            }
            return WebGLGetInfo(GraphicsContext3D::LINEAR);
        }
        default:
            synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getFramebufferAttachmentParameter", "invalid parameter name for renderbuffer attachment");
            return WebGLGetInfo();
        }
    }
}

bool WebGL2RenderingContext::validateFramebufferFuncParameters(const char* functionName, GC3Denum target, GC3Denum attachment)
{
    if (target != GraphicsContext3D::FRAMEBUFFER) {
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "invalid target");
        return false;
    }
    switch (attachment) {
    case GraphicsContext3D::COLOR_ATTACHMENT0:
    case GraphicsContext3D::DEPTH_ATTACHMENT:
    case GraphicsContext3D::STENCIL_ATTACHMENT:
    case GraphicsContext3D::DEPTH_STENCIL_ATTACHMENT:
        break;
    default:
        if (attachment > GraphicsContext3D::COLOR_ATTACHMENT0
            && attachment < static_cast<GC3Denum>(GraphicsContext3D::COLOR_ATTACHMENT0 + getMaxColorAttachments()))
            break;
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "invalid attachment");
        return false;
    }
    return true;
}

GC3Dint WebGL2RenderingContext::getMaxDrawBuffers()
{
    if (!m_maxDrawBuffers)
        m_context->getIntegerv(GraphicsContext3D::MAX_DRAW_BUFFERS, &m_maxDrawBuffers);
    return m_maxDrawBuffers;
}

GC3Dint WebGL2RenderingContext::getMaxColorAttachments()
{
    // DrawBuffers requires MAX_COLOR_ATTACHMENTS == MAX_DRAW_BUFFERS
    if (!m_maxColorAttachments)
        m_context->getIntegerv(GraphicsContext3D::MAX_DRAW_BUFFERS, &m_maxColorAttachments);
    return m_maxColorAttachments;
}

void WebGL2RenderingContext::renderbufferStorage(GC3Denum target, GC3Denum internalformat, GC3Dsizei width, GC3Dsizei height)
{
    if (isContextLostOrPending())
        return;
    if (target != GraphicsContext3D::RENDERBUFFER) {
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "renderbufferStorage", "invalid target");
        return;
    }
    if (!m_renderbufferBinding || !m_renderbufferBinding->object()) {
        synthesizeGLError(GraphicsContext3D::INVALID_OPERATION, "renderbufferStorage", "no bound renderbuffer");
        return;
    }
    if (!validateSize("renderbufferStorage", width, height))
        return;
    switch (internalformat) {
    case GraphicsContext3D::DEPTH_COMPONENT16:
    case GraphicsContext3D::DEPTH_COMPONENT32F:
    case GraphicsContext3D::DEPTH_COMPONENT24:
    case GraphicsContext3D::RGBA32I:
    case GraphicsContext3D::RGBA32UI:
    case GraphicsContext3D::RGBA16I:
    case GraphicsContext3D::RGBA16UI:
    case GraphicsContext3D::RGBA8:
    case GraphicsContext3D::RGBA8I:
    case GraphicsContext3D::RGBA8UI:
    case GraphicsContext3D::RGB10_A2:
    case GraphicsContext3D::RGB10_A2UI:
    case GraphicsContext3D::RGBA4:
    case GraphicsContext3D::RG32I:
    case GraphicsContext3D::RG32UI:
    case GraphicsContext3D::RG16I:
    case GraphicsContext3D::RG16UI:
    case GraphicsContext3D::RG8:
    case GraphicsContext3D::RG8I:
    case GraphicsContext3D::RG8UI:
    case GraphicsContext3D::R32I:
    case GraphicsContext3D::R32UI:
    case GraphicsContext3D::R16I:
    case GraphicsContext3D::R16UI:
    case GraphicsContext3D::R8:
    case GraphicsContext3D::R8I:
    case GraphicsContext3D::R8UI:
    case GraphicsContext3D::RGB5_A1:
    case GraphicsContext3D::RGB565:
    case GraphicsContext3D::STENCIL_INDEX8:
    case GraphicsContext3D::SRGB8_ALPHA8:
        m_context->renderbufferStorage(target, internalformat, width, height);
        m_renderbufferBinding->setInternalFormat(internalformat);
        m_renderbufferBinding->setIsValid(true);
        m_renderbufferBinding->setSize(width, height);
        break;
    case GraphicsContext3D::DEPTH32F_STENCIL8:
    case GraphicsContext3D::DEPTH24_STENCIL8:
        if (!isDepthStencilSupported()) {
            synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "renderbufferStorage", "invalid internalformat");
            return;
        }
        m_context->renderbufferStorage(target, internalformat, width, height);
        m_renderbufferBinding->setSize(width, height);
        m_renderbufferBinding->setIsValid(isDepthStencilSupported());
        m_renderbufferBinding->setInternalFormat(internalformat);
        break;
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "renderbufferStorage", "invalid internalformat");
        return;
    }
    applyStencilTest();
}

void WebGL2RenderingContext::hint(GC3Denum target, GC3Denum mode)
{
    if (isContextLostOrPending())
        return;
    bool isValid = false;
    switch (target) {
    case GraphicsContext3D::GENERATE_MIPMAP_HINT:
    case GraphicsContext3D::FRAGMENT_SHADER_DERIVATIVE_HINT:
        isValid = true;
        break;
    }
    if (!isValid) {
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "hint", "invalid target");
        return;
    }
    m_context->hint(target, mode);
}

GC3Denum WebGL2RenderingContext::baseInternalFormatFromInternalFormat(GC3Denum internalformat)
{
    // Handles sized, unsized, and compressed internal formats.
    switch (internalformat) {
    case GraphicsContext3D::R8:
    case GraphicsContext3D::R8_SNORM:
    case GraphicsContext3D::R16F:
    case GraphicsContext3D::R32F:
    case GraphicsContext3D::R8I:
    case GraphicsContext3D::R8UI:
    case GraphicsContext3D::R16I:
    case GraphicsContext3D::R16UI:
    case GraphicsContext3D::R32I:
    case GraphicsContext3D::R32UI:
    case GraphicsContext3D::COMPRESSED_R11_EAC:
    case GraphicsContext3D::COMPRESSED_SIGNED_R11_EAC:
        return GraphicsContext3D::RED;
    case GraphicsContext3D::RG8:
    case GraphicsContext3D::RG8_SNORM:
    case GraphicsContext3D::RG16F:
    case GraphicsContext3D::RG32F:
    case GraphicsContext3D::RG8I:
    case GraphicsContext3D::RG8UI:
    case GraphicsContext3D::RG16I:
    case GraphicsContext3D::RG16UI:
    case GraphicsContext3D::RG32I:
    case GraphicsContext3D::RG32UI:
    case GraphicsContext3D::COMPRESSED_RG11_EAC:
    case GraphicsContext3D::COMPRESSED_SIGNED_RG11_EAC:
        return GraphicsContext3D::RG;
    case GraphicsContext3D::RGB8:
    case GraphicsContext3D::RGB8_SNORM:
    case GraphicsContext3D::RGB565:
    case GraphicsContext3D::SRGB8:
    case GraphicsContext3D::RGB16F:
    case GraphicsContext3D::RGB32F:
    case GraphicsContext3D::RGB8I:
    case GraphicsContext3D::RGB8UI:
    case GraphicsContext3D::RGB16I:
    case GraphicsContext3D::RGB16UI:
    case GraphicsContext3D::RGB32I:
    case GraphicsContext3D::RGB32UI:
    case GraphicsContext3D::RGB:
    case GraphicsContext3D::COMPRESSED_RGB8_ETC2:
    case GraphicsContext3D::COMPRESSED_SRGB8_ETC2:
        return GraphicsContext3D::RGB;
    case GraphicsContext3D::RGBA4:
    case GraphicsContext3D::RGB5_A1:
    case GraphicsContext3D::RGBA8:
    case GraphicsContext3D::RGBA8_SNORM:
    case GraphicsContext3D::RGB10_A2:
    case GraphicsContext3D::RGB10_A2UI:
    case GraphicsContext3D::SRGB8_ALPHA8:
    case GraphicsContext3D::RGBA16F:
    case GraphicsContext3D::RGBA32F:
    case GraphicsContext3D::RGBA8I:
    case GraphicsContext3D::RGBA8UI:
    case GraphicsContext3D::RGBA16I:
    case GraphicsContext3D::RGBA16UI:
    case GraphicsContext3D::RGBA32I:
    case GraphicsContext3D::RGBA32UI:
    case GraphicsContext3D::RGBA:
    case GraphicsContext3D::COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GraphicsContext3D::COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GraphicsContext3D::COMPRESSED_RGBA8_ETC2_EAC:
    case GraphicsContext3D::COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
        return GraphicsContext3D::RGBA;
    case GraphicsContext3D::DEPTH_COMPONENT16:
    case GraphicsContext3D::DEPTH_COMPONENT24:
    case GraphicsContext3D::DEPTH_COMPONENT32F:
        return GraphicsContext3D::DEPTH_COMPONENT;
    case GraphicsContext3D::DEPTH24_STENCIL8:
    case GraphicsContext3D::DEPTH32F_STENCIL8:
        return GraphicsContext3D::DEPTH_STENCIL;
    case GraphicsContext3D::LUMINANCE:
    case GraphicsContext3D::LUMINANCE_ALPHA:
    case GraphicsContext3D::ALPHA:
        return internalformat;
    default:
        ASSERT_NOT_REACHED();
        return GraphicsContext3D::NONE;
    }
}

bool WebGL2RenderingContext::isIntegerFormat(GC3Denum internalformat)
{
    // FIXME: baseInternalFormatFromInternalFormat() never returns any of these enums.
    // Currently, This function erroneously always returns false.
    switch (baseInternalFormatFromInternalFormat(internalformat)) {
    case GraphicsContext3D::RED_INTEGER:
    case GraphicsContext3D::RG_INTEGER:
    case GraphicsContext3D::RGB_INTEGER:
    case GraphicsContext3D::RGBA_INTEGER:
        return true;
    }
    return false;
}

WebGLGetInfo WebGL2RenderingContext::getParameter(GC3Denum pname)
{
    if (isContextLostOrPending())
        return WebGLGetInfo();
    const int intZero = 0;
    switch (pname) {
    case GraphicsContext3D::ACTIVE_TEXTURE:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::ALIASED_LINE_WIDTH_RANGE:
        return getWebGLFloatArrayParameter(pname);
    case GraphicsContext3D::ALIASED_POINT_SIZE_RANGE:
        return getWebGLFloatArrayParameter(pname);
    case GraphicsContext3D::ALPHA_BITS:
        return getIntParameter(pname);
    case GraphicsContext3D::ARRAY_BUFFER_BINDING:
        return WebGLGetInfo(m_boundArrayBuffer.get());
    case GraphicsContext3D::BLEND:
        return getBooleanParameter(pname);
    case GraphicsContext3D::BLEND_COLOR:
        return getWebGLFloatArrayParameter(pname);
    case GraphicsContext3D::BLEND_DST_ALPHA:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::BLEND_DST_RGB:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::BLEND_EQUATION_ALPHA:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::BLEND_EQUATION_RGB:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::BLEND_SRC_ALPHA:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::BLEND_SRC_RGB:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::BLUE_BITS:
        return getIntParameter(pname);
    case GraphicsContext3D::COLOR_CLEAR_VALUE:
        return getWebGLFloatArrayParameter(pname);
    case GraphicsContext3D::COLOR_WRITEMASK:
        return getBooleanArrayParameter(pname);
    case GraphicsContext3D::COMPRESSED_TEXTURE_FORMATS:
        return WebGLGetInfo(PassRefPtr<Uint32Array>(Uint32Array::create(m_compressedTextureFormats.data(), m_compressedTextureFormats.size())));
    case GraphicsContext3D::CULL_FACE:
        return getBooleanParameter(pname);
    case GraphicsContext3D::CULL_FACE_MODE:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::CURRENT_PROGRAM:
        return WebGLGetInfo(m_currentProgram.get());
    case GraphicsContext3D::DEPTH_BITS:
        if (!m_framebufferBinding && !m_attributes.depth)
            return WebGLGetInfo(intZero);
        return getIntParameter(pname);
    case GraphicsContext3D::DEPTH_CLEAR_VALUE:
        return getFloatParameter(pname);
    case GraphicsContext3D::DEPTH_FUNC:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::DEPTH_RANGE:
        return getWebGLFloatArrayParameter(pname);
    case GraphicsContext3D::DEPTH_TEST:
        return getBooleanParameter(pname);
    case GraphicsContext3D::DEPTH_WRITEMASK:
        return getBooleanParameter(pname);
    case GraphicsContext3D::DITHER:
        return getBooleanParameter(pname);
    case GraphicsContext3D::ELEMENT_ARRAY_BUFFER_BINDING:
        return WebGLGetInfo(m_boundVertexArrayObject->getElementArrayBuffer());
    case GraphicsContext3D::FRAMEBUFFER_BINDING:
        return WebGLGetInfo(m_framebufferBinding.get());
    case GraphicsContext3D::FRONT_FACE:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::GENERATE_MIPMAP_HINT:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::GREEN_BITS:
        return getIntParameter(pname);
    case GraphicsContext3D::IMPLEMENTATION_COLOR_READ_FORMAT:
        return getIntParameter(pname);
    case GraphicsContext3D::IMPLEMENTATION_COLOR_READ_TYPE:
        return getIntParameter(pname);
    case GraphicsContext3D::LINE_WIDTH:
        return getFloatParameter(pname);
    case GraphicsContext3D::MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_CUBE_MAP_TEXTURE_SIZE:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_FRAGMENT_UNIFORM_VECTORS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_RENDERBUFFER_SIZE:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_TEXTURE_IMAGE_UNITS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_TEXTURE_SIZE:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VARYING_VECTORS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VERTEX_ATTRIBS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VERTEX_TEXTURE_IMAGE_UNITS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VERTEX_UNIFORM_VECTORS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VIEWPORT_DIMS:
        return getWebGLIntArrayParameter(pname);
    case GraphicsContext3D::NUM_SHADER_BINARY_FORMATS:
        return getIntParameter(pname);
    case GraphicsContext3D::PACK_ALIGNMENT:
        return getIntParameter(pname);
    case GraphicsContext3D::POLYGON_OFFSET_FACTOR:
        return getFloatParameter(pname);
    case GraphicsContext3D::POLYGON_OFFSET_FILL:
        return getBooleanParameter(pname);
    case GraphicsContext3D::POLYGON_OFFSET_UNITS:
        return getFloatParameter(pname);
    case GraphicsContext3D::RED_BITS:
        return getIntParameter(pname);
    case GraphicsContext3D::RENDERBUFFER_BINDING:
        return WebGLGetInfo(m_renderbufferBinding.get());
    case GraphicsContext3D::RENDERER:
        return WebGLGetInfo(String("WebKit WebGL"));
    case GraphicsContext3D::SAMPLE_BUFFERS:
        return getIntParameter(pname);
    case GraphicsContext3D::SAMPLE_COVERAGE_INVERT:
        return getBooleanParameter(pname);
    case GraphicsContext3D::SAMPLE_COVERAGE_VALUE:
        return getFloatParameter(pname);
    case GraphicsContext3D::SAMPLES:
        return getIntParameter(pname);
    case GraphicsContext3D::SCISSOR_BOX:
        return getWebGLIntArrayParameter(pname);
    case GraphicsContext3D::SCISSOR_TEST:
        return getBooleanParameter(pname);
    case GraphicsContext3D::SHADING_LANGUAGE_VERSION:
        return WebGLGetInfo("WebGL GLSL ES 1.0 (" + m_context->getString(GraphicsContext3D::SHADING_LANGUAGE_VERSION) + ")");
    case GraphicsContext3D::STENCIL_BACK_FAIL:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_BACK_FUNC:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_BACK_PASS_DEPTH_FAIL:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_BACK_PASS_DEPTH_PASS:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_BACK_REF:
        return getIntParameter(pname);
    case GraphicsContext3D::STENCIL_BACK_VALUE_MASK:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_BACK_WRITEMASK:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_BITS:
        if (!m_framebufferBinding && !m_attributes.stencil)
            return WebGLGetInfo(intZero);
        return getIntParameter(pname);
    case GraphicsContext3D::STENCIL_CLEAR_VALUE:
        return getIntParameter(pname);
    case GraphicsContext3D::STENCIL_FAIL:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_FUNC:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_PASS_DEPTH_FAIL:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_PASS_DEPTH_PASS:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_REF:
        return getIntParameter(pname);
    case GraphicsContext3D::STENCIL_TEST:
        return getBooleanParameter(pname);
    case GraphicsContext3D::STENCIL_VALUE_MASK:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::STENCIL_WRITEMASK:
        return getUnsignedIntParameter(pname);
    case GraphicsContext3D::SUBPIXEL_BITS:
        return getIntParameter(pname);
    case GraphicsContext3D::TEXTURE_BINDING_2D:
        return WebGLGetInfo(m_textureUnits[m_activeTextureUnit].texture2DBinding.get());
    case GraphicsContext3D::TEXTURE_BINDING_CUBE_MAP:
        return WebGLGetInfo(m_textureUnits[m_activeTextureUnit].textureCubeMapBinding.get());
    case GraphicsContext3D::UNPACK_ALIGNMENT:
        return getIntParameter(pname);
    case GraphicsContext3D::UNPACK_FLIP_Y_WEBGL:
        return WebGLGetInfo(m_unpackFlipY);
    case GraphicsContext3D::UNPACK_PREMULTIPLY_ALPHA_WEBGL:
        return WebGLGetInfo(m_unpackPremultiplyAlpha);
    case GraphicsContext3D::UNPACK_COLORSPACE_CONVERSION_WEBGL:
        return WebGLGetInfo(m_unpackColorspaceConversion);
    case GraphicsContext3D::VENDOR:
        return WebGLGetInfo(String("WebKit"));
    case GraphicsContext3D::VERSION:
        return WebGLGetInfo("WebGL 2.0 (" + m_context->getString(GraphicsContext3D::VERSION) + ")");
    case GraphicsContext3D::VIEWPORT:
        return getWebGLIntArrayParameter(pname);
    case WebGLDebugRendererInfo::UNMASKED_RENDERER_WEBGL:
        if (m_webglDebugRendererInfo)
            return WebGLGetInfo(m_context->getString(GraphicsContext3D::RENDERER));
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getParameter", "invalid parameter name, WEBGL_debug_renderer_info not enabled");
        return WebGLGetInfo();
    case WebGLDebugRendererInfo::UNMASKED_VENDOR_WEBGL:
        if (m_webglDebugRendererInfo)
            return WebGLGetInfo(m_context->getString(GraphicsContext3D::VENDOR));
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getParameter", "invalid parameter name, WEBGL_debug_renderer_info not enabled");
        return WebGLGetInfo();
    case Extensions3D::MAX_TEXTURE_MAX_ANISOTROPY_EXT: // EXT_texture_filter_anisotropic
        if (m_extTextureFilterAnisotropic)
            return getUnsignedIntParameter(Extensions3D::MAX_TEXTURE_MAX_ANISOTROPY_EXT);
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getParameter", "invalid parameter name, EXT_texture_filter_anisotropic not enabled");
        return WebGLGetInfo();
    case GraphicsContext3D::FRAGMENT_SHADER_DERIVATIVE_HINT:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_3D_TEXTURE_SIZE:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_ARRAY_TEXTURE_LAYERS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_COLOR_ATTACHMENTS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS:
        return getInt64Parameter(pname);
    case GraphicsContext3D::MAX_COMBINED_UNIFORM_BLOCKS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS:
        return getInt64Parameter(pname);
    case GraphicsContext3D::MAX_DRAW_BUFFERS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_ELEMENT_INDEX:
        return getInt64Parameter(pname);
    case GraphicsContext3D::MAX_ELEMENTS_INDICES:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_ELEMENTS_VERTICES:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_FRAGMENT_UNIFORM_COMPONENTS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_FRAGMENT_UNIFORM_BLOCKS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_PROGRAM_TEXEL_OFFSET:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_SAMPLES:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_SERVER_WAIT_TIMEOUT:
        return getInt64Parameter(pname);
    case GraphicsContext3D::MAX_TEXTURE_LOD_BIAS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_UNIFORM_BLOCK_SIZE:
        return getInt64Parameter(pname);
    case GraphicsContext3D::MAX_UNIFORM_BUFFER_BINDINGS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VARYING_COMPONENTS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VERTEX_OUTPUT_COMPONENTS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VERTEX_UNIFORM_BLOCKS:
        return getIntParameter(pname);
    case GraphicsContext3D::MAX_VERTEX_UNIFORM_COMPONENTS: 
        return getIntParameter(pname);                            
    case GraphicsContext3D::MIN_PROGRAM_TEXEL_OFFSET:
        return getIntParameter(pname);
    case GraphicsContext3D::PACK_ROW_LENGTH:
        return getIntParameter(pname);
    case GraphicsContext3D::PACK_SKIP_PIXELS:
        return getIntParameter(pname);
    case GraphicsContext3D::PACK_SKIP_ROWS:
        return getIntParameter(pname);
    case GraphicsContext3D::UNPACK_IMAGE_HEIGHT:
        return getIntParameter(pname);
    case GraphicsContext3D::UNPACK_ROW_LENGTH:
        return getIntParameter(pname);
    case GraphicsContext3D::UNPACK_SKIP_IMAGES:
        return getIntParameter(pname);
    case GraphicsContext3D::UNPACK_SKIP_PIXELS:
        return getIntParameter(pname);
    case GraphicsContext3D::UNPACK_SKIP_ROWS:
        return getIntParameter(pname);
    case GraphicsContext3D::RASTERIZER_DISCARD:
        return getBooleanParameter(pname);
    case GraphicsContext3D::SAMPLE_ALPHA_TO_COVERAGE:
        return getBooleanParameter(pname);
    case GraphicsContext3D::SAMPLE_COVERAGE:
        return getBooleanParameter(pname);
    case GraphicsContext3D::TRANSFORM_FEEDBACK_ACTIVE:
        return getBooleanParameter(pname);
    case GraphicsContext3D::TRANSFORM_FEEDBACK_PAUSED:
        return getBooleanParameter(pname);
    case GraphicsContext3D::UNIFORM_BUFFER_OFFSET_ALIGNMENT:
        return getIntParameter(pname);
    case GraphicsContext3D::VERTEX_ARRAY_BINDING: {
        if (!m_boundVertexArrayObject->isDefaultObject())
            return WebGLGetInfo(static_cast<WebGLVertexArrayObject*>(m_boundVertexArrayObject.get()));
        return WebGLGetInfo();
        }
        break;
    case GraphicsContext3D::DRAW_BUFFER0:
    case GraphicsContext3D::DRAW_BUFFER1:
    case GraphicsContext3D::DRAW_BUFFER2:
    case GraphicsContext3D::DRAW_BUFFER3:
    case GraphicsContext3D::DRAW_BUFFER4:
    case GraphicsContext3D::DRAW_BUFFER5:
    case GraphicsContext3D::DRAW_BUFFER6:
    case GraphicsContext3D::DRAW_BUFFER7:
    case GraphicsContext3D::DRAW_BUFFER8:
    case GraphicsContext3D::DRAW_BUFFER9:
    case GraphicsContext3D::DRAW_BUFFER10:
    case GraphicsContext3D::DRAW_BUFFER11:
    case GraphicsContext3D::DRAW_BUFFER12:
    case GraphicsContext3D::DRAW_BUFFER13:
    case GraphicsContext3D::DRAW_BUFFER14:
    case GraphicsContext3D::DRAW_BUFFER15: {
        GC3Dint value = GraphicsContext3D::NONE;
        if (m_framebufferBinding)
            value = m_framebufferBinding->getDrawBuffer(pname);
        else // emulated backbuffer
            value = m_backDrawBuffer;
        return WebGLGetInfo(value);
        }
    case GraphicsContext3D::COPY_READ_BUFFER:
    case GraphicsContext3D::COPY_WRITE_BUFFER:
    case GraphicsContext3D::PIXEL_PACK_BUFFER_BINDING:   
    case GraphicsContext3D::PIXEL_UNPACK_BUFFER_BINDING:
    case GraphicsContext3D::READ_BUFFER:
    case GraphicsContext3D::SAMPLER_BINDING:
    case GraphicsContext3D::TEXTURE_BINDING_2D_ARRAY:
    case GraphicsContext3D::TEXTURE_BINDING_3D:
    case GraphicsContext3D::READ_FRAMEBUFFER_BINDING:
    case GraphicsContext3D::TRANSFORM_FEEDBACK_BUFFER_BINDING:
    case GraphicsContext3D::UNIFORM_BUFFER_BINDING:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getParameter", "parameter name not yet supported");
        return WebGLGetInfo();
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, "getParameter", "invalid parameter name");
        return WebGLGetInfo();
    }
}

bool WebGL2RenderingContext::validateIndexArrayConservative(GC3Denum type, unsigned& numElementsRequired)
{
    // Performs conservative validation by caching a maximum index of
    // the given type per element array buffer. If all of the bound
    // array buffers have enough elements to satisfy that maximum
    // index, skips the expensive per-draw-call iteration in
    // validateIndexArrayPrecise.
    
    RefPtr<WebGLBuffer> elementArrayBuffer = m_boundVertexArrayObject->getElementArrayBuffer();
    
    if (!elementArrayBuffer)
        return false;
    
    GC3Dsizeiptr numElements = elementArrayBuffer->byteLength();
    // The case count==0 is already dealt with in drawElements before validateIndexArrayConservative.
    if (!numElements)
        return false;
    const ArrayBuffer* buffer = elementArrayBuffer->elementArrayBuffer();
    ASSERT(buffer);
    
    int maxIndex = elementArrayBuffer->getCachedMaxIndex(type);
    if (maxIndex < 0) {
        // Compute the maximum index in the entire buffer for the given type of index.
        switch (type) {
        case GraphicsContext3D::UNSIGNED_BYTE: {
            const GC3Dubyte* p = static_cast<const GC3Dubyte*>(buffer->data());
            for (GC3Dsizeiptr i = 0; i < numElements; i++)
                maxIndex = std::max(maxIndex, static_cast<int>(p[i]));
            break;
        }
        case GraphicsContext3D::UNSIGNED_SHORT: {
            numElements /= sizeof(GC3Dushort);
            const GC3Dushort* p = static_cast<const GC3Dushort*>(buffer->data());
            for (GC3Dsizeiptr i = 0; i < numElements; i++)
                maxIndex = std::max(maxIndex, static_cast<int>(p[i]));
            break;
        }
        case GraphicsContext3D::UNSIGNED_INT: {
            numElements /= sizeof(GC3Duint);
            const GC3Duint* p = static_cast<const GC3Duint*>(buffer->data());
            for (GC3Dsizeiptr i = 0; i < numElements; i++)
                maxIndex = std::max(maxIndex, static_cast<int>(p[i]));
            break;
        }
        default:
            return false;
        }
        elementArrayBuffer->setCachedMaxIndex(type, maxIndex);
    }
    
    if (maxIndex >= 0) {
        // The number of required elements is one more than the maximum
        // index that will be accessed.
        numElementsRequired = maxIndex + 1;
        return true;
    }
    
    return false;
}

bool WebGL2RenderingContext::validateBlendEquation(const char* functionName, GC3Denum mode)
{
    switch (mode) {
    case GraphicsContext3D::FUNC_ADD:
    case GraphicsContext3D::FUNC_SUBTRACT:
    case GraphicsContext3D::FUNC_REVERSE_SUBTRACT:
    case GraphicsContext3D::MIN:
    case GraphicsContext3D::MAX:
        return true;
        break;
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "invalid mode");
        return false;
    }
}

bool WebGL2RenderingContext::validateCapability(const char* functionName, GC3Denum cap)
{
    switch (cap) {
    case GraphicsContext3D::BLEND:
    case GraphicsContext3D::CULL_FACE:
    case GraphicsContext3D::DEPTH_TEST:
    case GraphicsContext3D::DITHER:
    case GraphicsContext3D::POLYGON_OFFSET_FILL:
    case GraphicsContext3D::SAMPLE_ALPHA_TO_COVERAGE:
    case GraphicsContext3D::SAMPLE_COVERAGE:
    case GraphicsContext3D::SCISSOR_TEST:
    case GraphicsContext3D::STENCIL_TEST:
    case GraphicsContext3D::RASTERIZER_DISCARD:
        return true;
    default:
        synthesizeGLError(GraphicsContext3D::INVALID_ENUM, functionName, "invalid capability");
        return false;
    }
}

} // namespace WebCore

#endif // ENABLE(WEBGL)
