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

Generalized texture atlas with rectangle packing and multi-channel support

*/

#ifndef RTEXTUREATLAS_H
#define RTEXTUREATLAS_H

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "tRectPacker.h"
#include <vector>
#include <memory>
#include <algorithm>

#ifndef DEDICATED
#include "rRender.h"
#endif

//! Atlas configuration parameters
struct TextureAtlasConfig
{
    int initialWidth = 512;   //!< Initial atlas width
    int initialHeight = 512;  //!< Initial atlas height
    int maxSize = 4096;       //!< Maximum atlas dimension
    int padding = 1;          //!< Pixels between items
    bool canGrow = true;      //!< Allow atlas resizing
    float growthFactor = 2.0f; //!< Size multiplier on grow
};

//! Traits for atlas pixel types - maps glm types to GL formats
template <typename Pixel>
struct TextureAtlasTraits;

//! Single channel (SDF)
template <>
struct TextureAtlasTraits<glm::u8vec1>
{
    static constexpr int channels = 1;
#ifndef DEDICATED
    static constexpr int format = rGLConst::Red;
    static constexpr int internalFormat = rGLConst::R8;
#endif
    using float_type = float;

    static glm::u8vec1 fromFloat(float f)
    {
        return glm::u8vec1(static_cast<unsigned char>(glm::clamp(f * 255.0f, 0.0f, 255.0f)));
    }
};

//! RGB (MSDF)
template <>
struct TextureAtlasTraits<glm::u8vec3>
{
    static constexpr int channels = 3;
#ifndef DEDICATED
    static constexpr int format = rGLConst::RGB;
    static constexpr int internalFormat = rGLConst::RGB8;
#endif
    using float_type = glm::vec3;

    static glm::u8vec3 fromFloat(const glm::vec3& f)
    {
        return glm::u8vec3(
            static_cast<unsigned char>(glm::clamp(f.r * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(glm::clamp(f.g * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(glm::clamp(f.b * 255.0f, 0.0f, 255.0f)));
    }
};

//! RGBA (MTSDF)
template <>
struct TextureAtlasTraits<glm::u8vec4>
{
    static constexpr int channels = 4;
#ifndef DEDICATED
    static constexpr int format = rGLConst::RGBA;
    static constexpr int internalFormat = rGLConst::RGBA8;
#endif
    using float_type = glm::vec4;

    static glm::u8vec4 fromFloat(const glm::vec4& f)
    {
        return glm::u8vec4(
            static_cast<unsigned char>(glm::clamp(f.r * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(glm::clamp(f.g * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(glm::clamp(f.b * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(glm::clamp(f.a * 255.0f, 0.0f, 255.0f)));
    }
};

//! Region in atlas with UV coordinates
struct TextureAtlasRegion
{
    int x = 0;        //!< X position in atlas (pixels)
    int y = 0;        //!< Y position in atlas (pixels)
    int width = 0;    //!< Width in atlas (pixels)
    int height = 0;   //!< Height in atlas (pixels)
    float u0 = 0.0f;  //!< Left UV coordinate [0,1]
    float v0 = 0.0f;  //!< Top UV coordinate [0,1]
    float u1 = 0.0f;  //!< Right UV coordinate [0,1]
    float v1 = 0.0f;  //!< Bottom UV coordinate [0,1]

    //!< Check if region is valid
    bool valid() const { return width > 0 && height > 0; }
};

//! Generalized SDF texture atlas using configurable packing
//! @tparam Pixel Pixel type (glm::u8vec1, glm::u8vec3, or glm::u8vec4)
//! @tparam Packer Rectangle packer type (default: tRectPacker)
template <typename Pixel, typename Packer = tRectPacker>
class rTextureAtlas
{
public:
    using traits = TextureAtlasTraits<Pixel>;
    using float_pixel = typename traits::float_type;
    using Region = TextureAtlasRegion;

    //! Construct with configuration
    explicit rTextureAtlas(const TextureAtlasConfig& config = TextureAtlasConfig());

    //! Construct with explicit dimensions (legacy compatibility)
    rTextureAtlas(int initialWidth, int initialHeight, int maxSize);

    ~rTextureAtlas();

    //! Pack a rectangle, returns region with UV coordinates
    //! @param rectWidth Width of rectangle to pack
    //! @param rectHeight Height of rectangle to pack
    //! @param outRegion Output region with position and UVs
    //! @return true if rectangle was successfully packed
    bool pack(int rectWidth, int rectHeight, Region& outRegion);

    //! Pack a rectangle (legacy interface)
    //! @param width Width of rectangle to pack
    //! @param height Height of rectangle to pack
    //! @param outX Output X position in atlas
    //! @param outY Output Y position in atlas
    //! @return true if rectangle was successfully packed
    bool pack(int width, int height, int& outX, int& outY);

    //! Upload from float bitmap (converts to uint8)
    //! @param region Region to upload to
    //! @param floatData Pointer to float pixel data
    void uploadFromFloat(const Region& region, const float_pixel* floatData);

    //! Upload from float bitmap (legacy interface)
    //! @param x X position in atlas
    //! @param y Y position in atlas
    //! @param w Width of region
    //! @param h Height of region
    //! @param floatData Pointer to float pixel data
    void uploadFromFloat(int x, int y, int w, int h, const float_pixel* floatData);

    //! Upload raw byte data
    //! @param region Region to upload to
    //! @param data Pointer to pixel data
    void upload(const Region& region, const Pixel* data);

    //! Upload raw byte data (legacy interface)
    //! @param x X position in atlas
    //! @param y Y position in atlas
    //! @param w Width of region
    //! @param h Height of region
    //! @param data Pointer to pixel data
    void upload(int x, int y, int w, int h, const Pixel* data);

    //! Grow atlas (doubles size up to max)
    //! @return true if growth was successful
    bool grow();

    //! Reset all allocations
    void reset();

    //! Get OpenGL texture ID
    unsigned int textureId() const { return textureId_; }

    //! Get current atlas width
    int width() const { return width_; }

    //! Get current atlas height
    int height() const { return height_; }

    //! Get maximum atlas size
    int maxSize() const { return config_.maxSize; }

    //! Get configuration
    const TextureAtlasConfig& config() const { return config_; }

    //! Check if atlas needs to be re-uploaded after grow
    bool needsReupload() const { return needsReupload_; }

    //! Clear reupload flag
    void clearReuploadFlag() { needsReupload_ = false; }

    //! Compute UV coordinates for a region
    void computeUVs(Region& region) const;

private:
    void createTexture();
    void setupTextureParams();
    void uploadFullTexture();

    TextureAtlasConfig config_;
    int width_, height_;
    unsigned int textureId_ = 0;
    std::unique_ptr<Packer> packer_;
    std::vector<Pixel> data_;  // CPU-side copy for regrowth
    bool needsReupload_ = false;
};

// Convenient type aliases
using rTextureAtlasSDF = rTextureAtlas<glm::u8vec1>;
using rTextureAtlasMSDF = rTextureAtlas<glm::u8vec3>;
using rTextureAtlasMTSDF = rTextureAtlas<glm::u8vec4>;

//=============================================================================
// Template implementation
//=============================================================================

template <typename Pixel, typename Packer>
rTextureAtlas<Pixel, Packer>::rTextureAtlas(const TextureAtlasConfig& config)
    : config_(config)
    , width_(config.initialWidth)
    , height_(config.initialHeight)
    , textureId_(0)
{
    data_.resize(width_ * height_);
    std::fill(data_.begin(), data_.end(), Pixel());
    packer_ = std::unique_ptr<Packer>(new Packer(width_, height_));
    createTexture();
}

template <typename Pixel, typename Packer>
rTextureAtlas<Pixel, Packer>::rTextureAtlas(int initialWidth, int initialHeight, int maxSize)
    : width_(initialWidth)
    , height_(initialHeight)
    , textureId_(0)
{
    config_.initialWidth = initialWidth;
    config_.initialHeight = initialHeight;
    config_.maxSize = maxSize;

    data_.resize(width_ * height_);
    std::fill(data_.begin(), data_.end(), Pixel());
    packer_ = std::unique_ptr<Packer>(new Packer(width_, height_));
    createTexture();
}

template <typename Pixel, typename Packer>
rTextureAtlas<Pixel, Packer>::~rTextureAtlas()
{
#ifndef DEDICATED
    if (textureId_ != 0)
    {
        RenderDeleteTexture(textureId_);
    }
#endif
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::createTexture()
{
#ifndef DEDICATED
    if (textureId_ == 0)
    {
        textureId_ = RenderGenTexture();
    }

    RenderBindTexture(rGLConst::Texture2D, textureId_);
    RenderPixelStorei(rGLConst::UnpackAlignment, 1);

    RenderTexImage2D(rGLConst::Texture2D, 0, traits::internalFormat,
                 width_, height_, 0, traits::format,
                 rGLConst::UnsignedByte, data_.data());

    setupTextureParams();

    // Flush to ensure texture data is committed to GPU before use
    RenderFlush();
#endif
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::setupTextureParams()
{
#ifndef DEDICATED
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMinFilter, rGLConst::Linear);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMagFilter, rGLConst::Linear);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapS, rGLConst::ClampToEdge);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapT, rGLConst::ClampToEdge);
#endif
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::uploadFullTexture()
{
#ifndef DEDICATED
    RenderBindTexture(rGLConst::Texture2D, textureId_);
    RenderPixelStorei(rGLConst::UnpackAlignment, 1);

    RenderTexImage2D(rGLConst::Texture2D, 0, traits::internalFormat,
                 width_, height_, 0, traits::format,
                 rGLConst::UnsignedByte, data_.data());
#endif
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::computeUVs(Region& region) const
{
    region.u0 = static_cast<float>(region.x) / static_cast<float>(width_);
    region.v0 = static_cast<float>(region.y) / static_cast<float>(height_);
    region.u1 = static_cast<float>(region.x + region.width) / static_cast<float>(width_);
    region.v1 = static_cast<float>(region.y + region.height) / static_cast<float>(height_);
}

template <typename Pixel, typename Packer>
bool rTextureAtlas<Pixel, Packer>::pack(int rectWidth, int rectHeight, Region& outRegion)
{
    int x, y;
    if (!packer_->Pack(rectWidth, rectHeight, x, y))
    {
        return false;
    }

    outRegion.x = x;
    outRegion.y = y;
    outRegion.width = rectWidth;
    outRegion.height = rectHeight;
    computeUVs(outRegion);

    return true;
}

template <typename Pixel, typename Packer>
bool rTextureAtlas<Pixel, Packer>::pack(int rectWidth, int rectHeight, int& outX, int& outY)
{
    return packer_->Pack(rectWidth, rectHeight, outX, outY);
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::uploadFromFloat(const Region& region, const float_pixel* floatData)
{
    uploadFromFloat(region.x, region.y, region.width, region.height, floatData);
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::uploadFromFloat(int x, int y, int w, int h, const float_pixel* floatData)
{
    // Convert float data to uint8
    std::vector<Pixel> converted(w * h);
    for (int i = 0; i < w * h; ++i)
    {
        converted[i] = traits::fromFloat(floatData[i]);
    }
    upload(x, y, w, h, converted.data());
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::upload(const Region& region, const Pixel* pixelData)
{
    upload(region.x, region.y, region.width, region.height, pixelData);
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::upload(int x, int y, int w, int h, const Pixel* pixelData)
{
    // Update CPU-side copy
    for (int row = 0; row < h; ++row)
    {
        for (int col = 0; col < w; ++col)
        {
            data_[(y + row) * width_ + (x + col)] = pixelData[row * w + col];
        }
    }

#ifndef DEDICATED
    // Update GPU texture
    RenderBindTexture(rGLConst::Texture2D, textureId_);
    RenderPixelStorei(rGLConst::UnpackAlignment, 1);
    RenderTexSubImage2D(rGLConst::Texture2D, 0, x, y, w, h,
                    traits::format, rGLConst::UnsignedByte, pixelData);
    // Flush to ensure glyph data is committed to GPU
    RenderFlush();
#endif
}

template <typename Pixel, typename Packer>
bool rTextureAtlas<Pixel, Packer>::grow()
{
    if (!config_.canGrow)
    {
        return false;
    }

    int newWidth = static_cast<int>(width_ * config_.growthFactor);
    int newHeight = static_cast<int>(height_ * config_.growthFactor);

    if (newWidth > config_.maxSize || newHeight > config_.maxSize)
    {
        return false;
    }

    // Create new larger data buffer
    std::vector<Pixel> newData(newWidth * newHeight);
    std::fill(newData.begin(), newData.end(), Pixel());

    // Copy old data (top-left corner of new atlas)
    for (int row = 0; row < height_; ++row)
    {
        for (int col = 0; col < width_; ++col)
        {
            newData[row * newWidth + col] = data_[row * width_ + col];
        }
    }

    // Update state
    width_ = newWidth;
    height_ = newHeight;
    data_ = std::move(newData);
    packer_ = std::unique_ptr<Packer>(new Packer(width_, height_));
    needsReupload_ = true;

    // Recreate GPU texture
    createTexture();

    return true;
}

template <typename Pixel, typename Packer>
void rTextureAtlas<Pixel, Packer>::reset()
{
    packer_->Reset();
    std::fill(data_.begin(), data_.end(), Pixel());
    uploadFullTexture();
    needsReupload_ = false;
}

#endif  // RTEXTUREATLAS_H
