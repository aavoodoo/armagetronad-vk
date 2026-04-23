/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#include "rRawPixelTexture.h"
#include "rRender.h"

#ifndef DEDICATED


rRawPixelTexture::rRawPixelTexture()
    : textureId_(0)
    , width_(0)
    , height_(0)
{
}

rRawPixelTexture::~rRawPixelTexture()
{
    if (textureId_ != 0)
    {
        RenderDeleteTexture(textureId_);
        textureId_ = 0;
    }
}

bool rRawPixelTexture::LoadFromPixels(const unsigned char* pixels, int width, int height,
                                       rPixelFormat format)
{
    // Validate input parameters
    if (pixels == nullptr)
    {
        return false;
    }

    // Validate dimensions (reasonable bounds for textures)
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
    {
        return false;
    }

    // Clean up existing texture if any
    if (textureId_ != 0)
    {
        RenderDeleteTexture(textureId_);
        textureId_ = 0;
    }

    textureId_ = RenderGenTexture();
    if (textureId_ == 0)
    {
        return false;
    }

    // Only set dimensions after successful texture generation
    width_ = width;
    height_ = height;

    RenderBindTexture(rGLConst::Texture2D, textureId_);

    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMinFilter, rGLConst::Linear);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMagFilter, rGLConst::Linear);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapS, rGLConst::ClampToEdge);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapT, rGLConst::ClampToEdge);

    int glFormat = rPixelFormatToInt(format);
    int internalFormat = glFormat;

    // For single-channel formats, use appropriate internal format
    if (format == rPixelFormat::R8 || format == rPixelFormat::L8)
    {
        internalFormat = rGLConst::Red;
    }

    RenderTexImage2D(rGLConst::Texture2D, 0, internalFormat, width, height, 0,
                     glFormat, rGLConst::UnsignedByte, pixels);

    return true;
}

void rRawPixelTexture::OnSelect(bool enforce)
{
    if (textureId_ != 0)
    {
        RenderBindTexture(rGLConst::Texture2D, textureId_);
    }
}

void rRawPixelTexture::OnUnload()
{
    if (textureId_ != 0)
    {
        RenderDeleteTexture(textureId_);
        textureId_ = 0;
    }
}

#endif // DEDICATED
