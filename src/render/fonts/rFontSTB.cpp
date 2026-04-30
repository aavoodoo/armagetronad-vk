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

stb_truetype based font implementation

*/

#include "aa_config.h"

#ifndef DEDICATED

#include "rFontSTB.h"
#include "rRender.h"
#include "rVertex.h"
#include "rRenderQueue.h"
#include "rRenderBucket.h"
#include "rMSDF.h"
#include "tDirectories.h"
#include "tBackgroundProcess.h"

#include <glm/glm.hpp>
#include <fstream>
#include <cstring>
#include <algorithm>
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

// stb_truetype implementation
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// External configuration for MSDF mode (defined in rFont.cpp)
extern int sr_fontMSDFMode;
extern float sr_fontMSDFRange;
extern int sr_fontMSDFGlyphSize;
extern float sr_fontSDFPxRangeMult;

// Initial atlas size
static const int INITIAL_ATLAS_WIDTH = 512;
static const int INITIAL_ATLAS_HEIGHT = 512;
static const int MAX_ATLAS_SIZE = 4096;

// Data for async MSDF generation
struct AsyncMSDFTask
{
    msdf::Shape shape;
    float scale;
    glm::vec2 translate;
    float range;
    int glyphSize;
    FontMode mode;
    unsigned int codepoint;
    rGlyphMetrics metrics;  // Pre-computed metrics
};

rFontSTB::rFontSTB()
    : fontInfo_(nullptr)
    , scale_(0)
    , ascent_(0)
    , descent_(0)
    , lineGap_(0)
    , fontSize_(0)
    , valid_(false)
    , textureId_(0)
    , atlasWidth_(0)
    , atlasHeight_(0)
{
}

rFontSTB::~rFontSTB()
{
    if (textureId_ != 0)
    {
        RenderDeleteTexture(textureId_);
    }
    delete static_cast<stbtt_fontinfo*>(fontInfo_);
}

bool rFontSTB::Load(const char* path, int size)
{
    // Clean up previous font
    if (fontInfo_)
    {
        delete static_cast<stbtt_fontinfo*>(fontInfo_);
        fontInfo_ = nullptr;
    }
    fontData_.clear();
    glyphCache_.clear();
    kerningCache_.clear();
    valid_ = false;

    // Load font file
#ifdef __ANDROID__
    // On Android, use SDL_IOFromFile to read from APK assets (fopen can't).
    {
        const char* assetPath = path;
        if (assetPath[0] == '.' && assetPath[1] == '/')
            assetPath += 2;
        SDL_IOStream* io = SDL_IOFromFile(assetPath, "rb");
        if (!io)
            return false;
        Sint64 size = SDL_GetIOSize(io);
        if (size <= 0)
        {
            SDL_CloseIO(io);
            return false;
        }
        fontData_.resize((size_t)size);
        SDL_ReadIO(io, fontData_.data(), (size_t)size);
        SDL_CloseIO(io);
    }
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    fontData_.resize(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(fontData_.data()), fileSize))
    {
        fontData_.clear();
        return false;
    }
    file.close();
#endif

    // Initialize stb_truetype
    fontInfo_ = new stbtt_fontinfo;
    stbtt_fontinfo* info = static_cast<stbtt_fontinfo*>(fontInfo_);

    if (!stbtt_InitFont(info, fontData_.data(), 0))
    {
        delete info;
        fontInfo_ = nullptr;
        fontData_.clear();
        return false;
    }

    // Calculate scaling
    fontSize_ = size;
    scale_ = stbtt_ScaleForPixelHeight(info, static_cast<float>(size));

    // Get font metrics
    stbtt_GetFontVMetrics(info, &ascent_, &descent_, &lineGap_);

    // Determine font mode from configuration
    mode_ = static_cast<FontMode>(sr_fontMSDFMode);
    msdfRange_ = sr_fontMSDFRange;
    msdfGlyphSize_ = sr_fontMSDFGlyphSize;

    // Create initial texture atlas based on mode
    if (mode_ == FontMode::LEGACY)
    {
        CreateAtlas(INITIAL_ATLAS_WIDTH, INITIAL_ATLAS_HEIGHT);
    }
    else if (mode_ == FontMode::SDF)
    {
        atlasSDF_ = std::unique_ptr<rFontAtlasSDF>(
            new rFontAtlasSDF(INITIAL_ATLAS_WIDTH, INITIAL_ATLAS_HEIGHT, MAX_ATLAS_SIZE));
    }
    else if (mode_ == FontMode::MSDF)
    {
        atlasMSDF_ = std::unique_ptr<rFontAtlasMSDF>(
            new rFontAtlasMSDF(INITIAL_ATLAS_WIDTH, INITIAL_ATLAS_HEIGHT, MAX_ATLAS_SIZE));
    }
    else  // MTSDF
    {
        atlasMTSDF_ = std::unique_ptr<rFontAtlasMTSDF>(
            new rFontAtlasMTSDF(INITIAL_ATLAS_WIDTH, INITIAL_ATLAS_HEIGHT, MAX_ATLAS_SIZE));
    }

    // Pre-load ASCII characters synchronously for better performance
    initializing_ = true;
    for (unsigned int c = 32; c < 127; ++c)
    {
        LoadGlyph(c);
    }
    initializing_ = false;  // Switch to async mode for on-demand glyphs

    // Flush texture uploads to GPU before marking font as valid
    RenderFlush();

    valid_ = true;
    return true;
}

bool rFontSTB::IsValid() const
{
    return valid_;
}

float rFontSTB::GetLineHeight() const
{
    return (ascent_ - descent_ + lineGap_) * scale_;
}

float rFontSTB::GetAscender() const
{
    return ascent_ * scale_;
}

float rFontSTB::GetDescender() const
{
    return descent_ * scale_;
}

bool rFontSTB::GetGlyphMetrics(unsigned int codepoint, rGlyphMetrics& metrics)
{
    auto it = glyphCache_.find(codepoint);
    if (it != glyphCache_.end() && it->second.state == GlyphState::LOADED)
    {
        metrics = it->second.metrics;
        return true;
    }

    if (LoadGlyph(codepoint))
    {
        metrics = glyphCache_[codepoint].metrics;
        return true;
    }

    return false;
}

bool rFontSTB::LoadGlyph(unsigned int codepoint)
{
    if (!fontInfo_)
        return false;

    // Use MSDF path for non-legacy modes
    if (mode_ != FontMode::LEGACY)
    {
        return LoadMSDFGlyph(codepoint);
    }

    stbtt_fontinfo* info = static_cast<stbtt_fontinfo*>(fontInfo_);

    // Check if already cached
    auto it = glyphCache_.find(codepoint);
    if (it != glyphCache_.end())
    {
        return it->second.state == GlyphState::LOADED;
    }

    // Get glyph index
    int glyphIndex = stbtt_FindGlyphIndex(info, static_cast<int>(codepoint));

    // Get glyph metrics
    int advanceWidth, leftSideBearing;
    stbtt_GetGlyphHMetrics(info, glyphIndex, &advanceWidth, &leftSideBearing);

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(info, glyphIndex, scale_, scale_, &x0, &y0, &x1, &y1);

    int glyphWidth = x1 - x0;
    int glyphHeight = y1 - y0;

    CachedGlyph cached;
    cached.metrics.width = glyphWidth;
    cached.metrics.height = glyphHeight;
    cached.metrics.bearingX = x0;
    cached.metrics.bearingY = -y0; // Convert to top-down
    cached.metrics.advance = static_cast<int>(advanceWidth * scale_);
    cached.metrics.textureId = textureId_;
    cached.state = GlyphState::NOT_LOADED;

    // Handle empty glyphs (like space)
    if (glyphWidth <= 0 || glyphHeight <= 0)
    {
        cached.metrics.texU0 = 0;
        cached.metrics.texV0 = 0;
        cached.metrics.texU1 = 0;
        cached.metrics.texV1 = 0;
        cached.state = GlyphState::LOADED;
        glyphCache_[codepoint] = cached;
        return true;
    }

    // 1-pixel padding on each side prevents bilinear filtering from bleeding
    // neighbouring glyph texels into the sampled glyph region.
    static constexpr int kPad = 1;

    // Find space in atlas (include padding in reserved region)
    int atlasX, atlasY;
    int packW = glyphWidth  + 2 * kPad;
    int packH = glyphHeight + 2 * kPad;
    if (!packer_->Pack(packW, packH, atlasX, atlasY))
    {
        // Need to grow atlas
        if (!EnsureAtlasSpace(packW, packH))
        {
            glyphCache_[codepoint] = cached;
            return false;
        }
        if (!packer_->Pack(packW, packH, atlasX, atlasY))
        {
            glyphCache_[codepoint] = cached;
            return false;
        }
    }

    // Render glyph to temporary buffer
    std::vector<unsigned char> glyphBitmap(glyphWidth * glyphHeight);
    stbtt_MakeGlyphBitmap(info, glyphBitmap.data(), glyphWidth, glyphHeight,
                          glyphWidth, scale_, scale_, glyphIndex);

    // Upload glyph at (atlasX+kPad, atlasY+kPad); padding pixels are
    // implicitly 0 (atlas is cleared on creation/growth).
    UpdateAtlasRegion(atlasX + kPad, atlasY + kPad, glyphWidth, glyphHeight, glyphBitmap.data());

    // Store texture coordinates with half-texel inset so bilinear filtering
    // never samples the padding pixels at glyph boundaries.
    static constexpr float kHalf = 0.5f;
    cached.metrics.texU0 = (atlasX + kPad + kHalf) / atlasWidth_;
    cached.metrics.texV0 = (atlasY + kPad + kHalf) / atlasHeight_;
    cached.metrics.texU1 = (atlasX + kPad + glyphWidth  - kHalf) / atlasWidth_;
    cached.metrics.texV1 = (atlasY + kPad + glyphHeight - kHalf) / atlasHeight_;
    cached.state = GlyphState::LOADED;

    glyphCache_[codepoint] = cached;
    return true;
}

bool rFontSTB::LoadMSDFGlyph(unsigned int codepoint)
{
    stbtt_fontinfo* info = static_cast<stbtt_fontinfo*>(fontInfo_);

    // Check if already cached or in progress
    {
        auto it = glyphCache_.find(codepoint);
        if (it != glyphCache_.end())
        {
            // If LOADED, return true; if LOADING, return false (not ready yet)
            return it->second.state == GlyphState::LOADED;
        }
    }

    // Check if already being generated asynchronously
    if (!initializing_)
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        if (glyphsInFlight_.count(codepoint) > 0)
        {
            return false;  // Already being generated
        }
    }

    // Get glyph index
    int glyphIndex = stbtt_FindGlyphIndex(info, static_cast<int>(codepoint));

    // Get glyph metrics (in font units)
    int advanceWidth, leftSideBearing;
    stbtt_GetGlyphHMetrics(info, glyphIndex, &advanceWidth, &leftSideBearing);

    // Get glyph bounding box (in font units)
    int ix0, iy0, ix1, iy1;
    stbtt_GetGlyphBox(info, glyphIndex, &ix0, &iy0, &ix1, &iy1);

    // Calculate scale to fit glyph in atlas cell with padding
    float padding = msdfRange_;
    float glyphWidthUnits = static_cast<float>(ix1 - ix0);
    float glyphHeightUnits = static_cast<float>(iy1 - iy0);

    // Determine texture ID based on mode
    unsigned int activeTexId = GetActiveTextureId();

    CachedGlyph cached;
    cached.metrics.bearingX = static_cast<int>(leftSideBearing * scale_);
    cached.metrics.bearingY = static_cast<int>(iy1 * scale_);
    cached.metrics.advance = static_cast<int>(advanceWidth * scale_);
    cached.metrics.textureId = activeTexId;
    cached.state = GlyphState::NOT_LOADED;

    // Handle empty glyphs (like space)
    if (glyphWidthUnits <= 0 || glyphHeightUnits <= 0)
    {
        cached.metrics.width = 0;
        cached.metrics.height = 0;
        cached.metrics.texU0 = 0;
        cached.metrics.texV0 = 0;
        cached.metrics.texU1 = 0;
        cached.metrics.texV1 = 0;
        cached.state = GlyphState::LOADED;
        glyphCache_[codepoint] = cached;
        return true;
    }

    // Get shape from stb_truetype
    stbtt_vertex* vertices = nullptr;
    int numVerts = stbtt_GetGlyphShape(info, glyphIndex, &vertices);

    if (numVerts <= 0)
    {
        cached.metrics.width = 0;
        cached.metrics.height = 0;
        cached.metrics.texU0 = 0;
        cached.metrics.texV0 = 0;
        cached.metrics.texU1 = 0;
        cached.metrics.texV1 = 0;
        cached.state = GlyphState::LOADED;
        glyphCache_[codepoint] = cached;
        if (vertices)
            stbtt_FreeShape(info, vertices);
        return true;
    }

    // Convert to MSDF shape
    msdf::Shape shape = msdf::shapeFromSTBVertices(vertices, numVerts);
    stbtt_FreeShape(info, vertices);

    if (shape.isEmpty())
    {
        cached.metrics.width = 0;
        cached.metrics.height = 0;
        cached.metrics.texU0 = 0;
        cached.metrics.texV0 = 0;
        cached.metrics.texU1 = 0;
        cached.metrics.texV1 = 0;
        cached.state = GlyphState::LOADED;
        glyphCache_[codepoint] = cached;
        return true;
    }

    // Normalize shape structure (split single-edge contours)
    shape.normalize();

    // Apply edge coloring
    msdf::colorEdges(shape, 3.0f);

    // Calculate scale to fit glyph in MSDF cell (square cell, glyph centered)
    // The glyph is scaled to fit in (msdfGlyphSize_ - 2*padding) pixels
    float maxGlyphDim = std::max(glyphWidthUnits, glyphHeightUnits);
    float msdfScale = (msdfGlyphSize_ - 2.0f * padding) / maxGlyphDim;

    // Center the glyph within the cell
    float scaledGlyphWidth = glyphWidthUnits * msdfScale;
    float scaledGlyphHeight = glyphHeightUnits * msdfScale;

    // Horizontal centering offset in atlas pixels
    float centeringX = (msdfGlyphSize_ - 2.0f * padding - scaledGlyphWidth) * 0.5f;
    float centeringY = (msdfGlyphSize_ - 2.0f * padding - scaledGlyphHeight) * 0.5f;

    // Translation: maps font unit (0,0) to atlas pixel position
    // Font point (x,y) -> atlas pixel (x * msdfScale + translate.x, y * msdfScale + translate.y)
    glm::vec2 translate(
        padding + centeringX - ix0 * msdfScale,
        padding + centeringY - iy0 * msdfScale);

    // The ratio to convert from atlas pixels to screen pixels
    float atlasToScreen = scale_ / msdfScale;

    // The full cell size in screen pixels
    float cellScreenSize = msdfGlyphSize_ * atlasToScreen;

    // The quad is square (matches texture cell aspect ratio)
    // SDF shader will make padding area transparent
    cached.metrics.width = static_cast<int>(cellScreenSize + 0.5f);
    cached.metrics.height = static_cast<int>(cellScreenSize + 0.5f);

    // Bearing adjustments:
    // - Original bearing positions the glyph's left edge (ix0) and top edge (iy1)
    // - In MSDF cell, the glyph left edge is at (padding + centeringX) pixels from cell left
    // - We need to offset the bearing by this amount in screen space
    float leftEdgeOffset = (padding + centeringX) * atlasToScreen;
    float topEdgeOffset = (padding + centeringY) * atlasToScreen;

    cached.metrics.bearingX = static_cast<int>(ix0 * scale_ - leftEdgeOffset);
    cached.metrics.bearingY = static_cast<int>(iy1 * scale_ + topEdgeOffset);

    // Async path: schedule background MSDF generation for on-demand glyphs
    if (!initializing_)
    {
        // Mark as loading
        cached.state = GlyphState::LOADING;
        glyphCache_[codepoint] = cached;

        // Add to in-flight set
        {
            std::lock_guard<std::mutex> lock(inFlightMutex_);
            glyphsInFlight_.insert(codepoint);
        }

        // Capture values for lambda
        FontMode capturedMode = mode_;
        float capturedScale = msdfScale;
        glm::vec2 capturedTranslate = translate;
        float capturedRange = msdfRange_;
        int capturedGlyphSize = msdfGlyphSize_;
        rGlyphMetrics capturedMetrics = cached.metrics;

        // Raw pointer to this - safe because font outlives background task
        // (font is cleared when reloading, which waits for tasks to complete)
        rFontSTB* self = this;

        // Schedule background MSDF generation
        tLambdaRunner::ScheduleBackground([self, capturedMode, capturedScale,
                                           capturedTranslate, capturedRange,
                                           capturedGlyphSize, codepoint,
                                           capturedMetrics,
                                           shape = std::move(shape)]() mutable {
            // Generate bitmap on background thread
            std::vector<unsigned char> bitmapData;
            int channels = 0;

            if (capturedMode == FontMode::SDF)
            {
                msdf::BitmapSDF bitmap(capturedGlyphSize, capturedGlyphSize);
                msdf::generateSDF(bitmap, shape, capturedScale, capturedTranslate, capturedRange);

                // Convert float to u8
                bitmapData.resize(capturedGlyphSize * capturedGlyphSize);
                for (int i = 0; i < capturedGlyphSize * capturedGlyphSize; ++i)
                {
                    bitmapData[i] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i] * 255.0f, 0.0f, 255.0f));
                }
                channels = 1;
            }
            else if (capturedMode == FontMode::MSDF)
            {
                msdf::BitmapMSDF bitmap(capturedGlyphSize, capturedGlyphSize);
                msdf::generateMSDF(bitmap, shape, capturedScale, capturedTranslate, capturedRange);
                msdf::errorCorrectionMSDF(bitmap, capturedRange);

                // Convert float vec3 to u8
                bitmapData.resize(capturedGlyphSize * capturedGlyphSize * 3);
                for (int i = 0; i < capturedGlyphSize * capturedGlyphSize; ++i)
                {
                    bitmapData[i * 3 + 0] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].r * 255.0f, 0.0f, 255.0f));
                    bitmapData[i * 3 + 1] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].g * 255.0f, 0.0f, 255.0f));
                    bitmapData[i * 3 + 2] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].b * 255.0f, 0.0f, 255.0f));
                }
                channels = 3;
            }
            else  // MTSDF
            {
                msdf::BitmapMTSDF bitmap(capturedGlyphSize, capturedGlyphSize);
                msdf::generateMTSDF(bitmap, shape, capturedScale, capturedTranslate, capturedRange);
                msdf::errorCorrectionMTSDF(bitmap, capturedRange);

                // Convert float vec4 to u8
                bitmapData.resize(capturedGlyphSize * capturedGlyphSize * 4);
                for (int i = 0; i < capturedGlyphSize * capturedGlyphSize; ++i)
                {
                    bitmapData[i * 4 + 0] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].r * 255.0f, 0.0f, 255.0f));
                    bitmapData[i * 4 + 1] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].g * 255.0f, 0.0f, 255.0f));
                    bitmapData[i * 4 + 2] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].b * 255.0f, 0.0f, 255.0f));
                    bitmapData[i * 4 + 3] = static_cast<unsigned char>(
                        std::clamp(bitmap.data[i].a * 255.0f, 0.0f, 255.0f));
                }
                channels = 4;
            }

            // Add to pending queue
            PendingGlyph pg;
            pg.codepoint = codepoint;
            pg.metrics = capturedMetrics;
            pg.bitmapData = std::move(bitmapData);
            pg.bitmapWidth = capturedGlyphSize;
            pg.bitmapHeight = capturedGlyphSize;
            pg.channels = channels;

            {
                std::lock_guard<std::mutex> lock(self->pendingGlyphsMutex_);
                self->pendingGlyphs_.push_back(std::move(pg));
            }
        });

        return false;  // Glyph not ready yet, will be available next frame
    }

    // Helper: try to pack into atlas, growing if needed. On grow, rescale
    // all cached glyph UVs so previously-packed glyphs remain correct.
    //
    // IMPORTANT: never grow mid-frame. Growing creates a new VkImage (new
    // VkImageView) which invalidates all descriptor sets for this atlas.
    // Any text vertex batches already written this frame carry UV coords
    // relative to the OLD (smaller) atlas. When ExecutePhase runs, those
    // batches will be bound to the NEW atlas, sampling wrong positions.
    // If we detect that frame recording is active we skip the grow and set
    // atlasGrowPending_ — BeginFrame() will do the grow at the safe
    // pre-render window of the next frame.
    auto packOrGrow = [&](auto& atlas, int cellSize, int& outX, int& outY) -> bool {
        bool ok = atlas->pack(cellSize, cellSize, outX, outY);
        if (!ok) {
            // Defer the grow when mid-frame — BUT NOT during initialization.
            // Fonts are loaded on-demand during rendering (RenderIsFrameStarted=true),
            // but must complete their initial ASCII preload synchronously regardless.
            if (RenderIsFrameStarted() && !initializing_) {
                atlasGrowPending_ = true;
                return false;
            }
            int oldW = atlas->width(), oldH = atlas->height();
            if (atlas->grow()) {
                float sx = static_cast<float>(oldW) / atlas->width();
                float sy = static_cast<float>(oldH) / atlas->height();
                for (auto& [cp, g] : glyphCache_) {
                    if (g.state == GlyphState::LOADED && g.metrics.textureId == atlas->textureId()) {
                        g.metrics.texU0 *= sx; g.metrics.texU1 *= sx;
                        g.metrics.texV0 *= sy; g.metrics.texV1 *= sy;
                    }
                }
                ok = atlas->pack(cellSize, cellSize, outX, outY);
            }
        }
        return ok;
    };

    // Sync path: generate and upload immediately (used during initialization)
    // Pack into atlas — 1-pixel margin on each side prevents bilinear filtering
    // from bleeding across cell boundaries. UVs cover the full glyph cell
    // (including its internal msdfRange_ padding for distance computation).
    static constexpr int kSdfMargin = 1;
    int cellWithMargin = msdfGlyphSize_ + 2 * kSdfMargin;
    int atlasX, atlasY;
    bool packed = false;

    if (mode_ == FontMode::SDF && atlasSDF_)
    {
        packed = packOrGrow(atlasSDF_, cellWithMargin, atlasX, atlasY);

        if (packed)
        {
            msdf::BitmapSDF bitmap(msdfGlyphSize_, msdfGlyphSize_);
            msdf::generateSDF(bitmap, shape, msdfScale, translate, msdfRange_);
            atlasSDF_->uploadFromFloat(atlasX + kSdfMargin, atlasY + kSdfMargin,
                                       msdfGlyphSize_, msdfGlyphSize_, bitmap.data.data());

            cached.metrics.textureId = atlasSDF_->textureId();
            cached.metrics.texU0 = static_cast<float>(atlasX + kSdfMargin) / atlasSDF_->width();
            cached.metrics.texV0 = static_cast<float>(atlasY + kSdfMargin) / atlasSDF_->height();
            cached.metrics.texU1 = static_cast<float>(atlasX + kSdfMargin + msdfGlyphSize_) / atlasSDF_->width();
            cached.metrics.texV1 = static_cast<float>(atlasY + kSdfMargin + msdfGlyphSize_) / atlasSDF_->height();
        }
    }
    else if (mode_ == FontMode::MSDF && atlasMSDF_)
    {
        packed = packOrGrow(atlasMSDF_, cellWithMargin, atlasX, atlasY);

        if (packed)
        {
            msdf::BitmapMSDF bitmap(msdfGlyphSize_, msdfGlyphSize_);
            msdf::generateMSDF(bitmap, shape, msdfScale, translate, msdfRange_);
            msdf::errorCorrectionMSDF(bitmap, msdfRange_);
            atlasMSDF_->uploadFromFloat(atlasX + kSdfMargin, atlasY + kSdfMargin,
                                        msdfGlyphSize_, msdfGlyphSize_, bitmap.data.data());

            cached.metrics.textureId = atlasMSDF_->textureId();
            cached.metrics.texU0 = static_cast<float>(atlasX + kSdfMargin) / atlasMSDF_->width();
            cached.metrics.texV0 = static_cast<float>(atlasY + kSdfMargin) / atlasMSDF_->height();
            cached.metrics.texU1 = static_cast<float>(atlasX + kSdfMargin + msdfGlyphSize_) / atlasMSDF_->width();
            cached.metrics.texV1 = static_cast<float>(atlasY + kSdfMargin + msdfGlyphSize_) / atlasMSDF_->height();
        }
    }
    else if (mode_ == FontMode::MTSDF && atlasMTSDF_)
    {
        packed = packOrGrow(atlasMTSDF_, cellWithMargin, atlasX, atlasY);

        if (packed)
        {
            msdf::BitmapMTSDF bitmap(msdfGlyphSize_, msdfGlyphSize_);
            msdf::generateMTSDF(bitmap, shape, msdfScale, translate, msdfRange_);
            msdf::errorCorrectionMTSDF(bitmap, msdfRange_);
            atlasMTSDF_->uploadFromFloat(atlasX + kSdfMargin, atlasY + kSdfMargin,
                                         msdfGlyphSize_, msdfGlyphSize_, bitmap.data.data());

            cached.metrics.textureId = atlasMTSDF_->textureId();
            cached.metrics.texU0 = static_cast<float>(atlasX + kSdfMargin) / atlasMTSDF_->width();
            cached.metrics.texV0 = static_cast<float>(atlasY + kSdfMargin) / atlasMTSDF_->height();
            cached.metrics.texU1 = static_cast<float>(atlasX + kSdfMargin + msdfGlyphSize_) / atlasMTSDF_->width();
            cached.metrics.texV1 = static_cast<float>(atlasY + kSdfMargin + msdfGlyphSize_) / atlasMTSDF_->height();
        }
    }

    if (!packed)
    {
        glyphCache_[codepoint] = cached;
        return false;
    }

    // Metrics (width, height, bearings) were already set above with padding
    cached.state = GlyphState::LOADED;

    glyphCache_[codepoint] = cached;
    return true;
}

unsigned int rFontSTB::GetActiveTextureId() const
{
    if (mode_ == FontMode::SDF && atlasSDF_)
        return atlasSDF_->textureId();
    if (mode_ == FontMode::MSDF && atlasMSDF_)
        return atlasMSDF_->textureId();
    if (mode_ == FontMode::MTSDF && atlasMTSDF_)
        return atlasMTSDF_->textureId();
    return textureId_;  // Legacy mode
}

// Called once per frame before any rendering begins (from rBeginFrame via sr_FontBeginFrame).
// Performs any atlas grow that was deferred from the previous frame because growing
// mid-frame would corrupt already-batched vertex UV coordinates.
void rFontSTB::BeginFrame()
{
    if (!atlasGrowPending_) return;
    atlasGrowPending_ = false;

    auto doGrow = [&](auto& atlas) {
        if (!atlas) return;
        int oldW = atlas->width(), oldH = atlas->height();
        if (atlas->grow()) {
            float sx = static_cast<float>(oldW) / atlas->width();
            float sy = static_cast<float>(oldH) / atlas->height();
            for (auto& [cp, g] : glyphCache_) {
                if (g.state == GlyphState::LOADED && g.metrics.textureId == atlas->textureId()) {
                    g.metrics.texU0 *= sx; g.metrics.texU1 *= sx;
                    g.metrics.texV0 *= sy; g.metrics.texV1 *= sy;
                }
            }
        }
    };

    if (mode_ == FontMode::SDF)        doGrow(atlasSDF_);
    else if (mode_ == FontMode::MSDF)  doGrow(atlasMSDF_);
    else if (mode_ == FontMode::MTSDF) doGrow(atlasMTSDF_);
}

void rFontSTB::ProcessPendingGlyphs()
{
    // Process glyphs that were generated asynchronously
    std::vector<PendingGlyph> pending;
    {
        std::lock_guard<std::mutex> lock(pendingGlyphsMutex_);
        pending = std::move(pendingGlyphs_);
        pendingGlyphs_.clear();
    }

    for (auto& pg : pending)
    {
        // Remove from in-flight set
        {
            std::lock_guard<std::mutex> lock(inFlightMutex_);
            glyphsInFlight_.erase(pg.codepoint);
        }

        // Pack into atlas and upload — 1-pixel margin prevents bilinear bleeding.
        static constexpr int kSdfMarginAsync = 1;
        int cellWithMarginAsync = msdfGlyphSize_ + 2 * kSdfMarginAsync;
        int atlasX, atlasY;
        bool packed = false;

        if (mode_ == FontMode::SDF && atlasSDF_)
        {
            packed = atlasSDF_->pack(cellWithMarginAsync, cellWithMarginAsync, atlasX, atlasY);
            if (!packed && atlasSDF_->grow())
                packed = atlasSDF_->pack(cellWithMarginAsync, cellWithMarginAsync, atlasX, atlasY);

            if (packed)
            {
                atlasSDF_->upload(atlasX + kSdfMarginAsync, atlasY + kSdfMarginAsync,
                    pg.bitmapWidth, pg.bitmapHeight,
                    reinterpret_cast<const glm::u8vec1*>(pg.bitmapData.data()));
                pg.metrics.textureId = atlasSDF_->textureId();
                pg.metrics.texU0 = static_cast<float>(atlasX + kSdfMarginAsync) / atlasSDF_->width();
                pg.metrics.texV0 = static_cast<float>(atlasY + kSdfMarginAsync) / atlasSDF_->height();
                pg.metrics.texU1 = static_cast<float>(atlasX + kSdfMarginAsync + msdfGlyphSize_) / atlasSDF_->width();
                pg.metrics.texV1 = static_cast<float>(atlasY + kSdfMarginAsync + msdfGlyphSize_) / atlasSDF_->height();
            }
        }
        else if (mode_ == FontMode::MSDF && atlasMSDF_)
        {
            packed = atlasMSDF_->pack(cellWithMarginAsync, cellWithMarginAsync, atlasX, atlasY);
            if (!packed && atlasMSDF_->grow())
                packed = atlasMSDF_->pack(cellWithMarginAsync, cellWithMarginAsync, atlasX, atlasY);

            if (packed)
            {
                atlasMSDF_->upload(atlasX + kSdfMarginAsync, atlasY + kSdfMarginAsync,
                    pg.bitmapWidth, pg.bitmapHeight,
                    reinterpret_cast<const glm::u8vec3*>(pg.bitmapData.data()));
                pg.metrics.textureId = atlasMSDF_->textureId();
                pg.metrics.texU0 = static_cast<float>(atlasX + kSdfMarginAsync) / atlasMSDF_->width();
                pg.metrics.texV0 = static_cast<float>(atlasY + kSdfMarginAsync) / atlasMSDF_->height();
                pg.metrics.texU1 = static_cast<float>(atlasX + kSdfMarginAsync + msdfGlyphSize_) / atlasMSDF_->width();
                pg.metrics.texV1 = static_cast<float>(atlasY + kSdfMarginAsync + msdfGlyphSize_) / atlasMSDF_->height();
            }
        }
        else if (mode_ == FontMode::MTSDF && atlasMTSDF_)
        {
            packed = atlasMTSDF_->pack(cellWithMarginAsync, cellWithMarginAsync, atlasX, atlasY);
            if (!packed && atlasMTSDF_->grow())
                packed = atlasMTSDF_->pack(cellWithMarginAsync, cellWithMarginAsync, atlasX, atlasY);

            if (packed)
            {
                atlasMTSDF_->upload(atlasX + kSdfMarginAsync, atlasY + kSdfMarginAsync,
                    pg.bitmapWidth, pg.bitmapHeight,
                    reinterpret_cast<const glm::u8vec4*>(pg.bitmapData.data()));
                pg.metrics.textureId = atlasMTSDF_->textureId();
                pg.metrics.texU0 = static_cast<float>(atlasX + kSdfMarginAsync) / atlasMTSDF_->width();
                pg.metrics.texV0 = static_cast<float>(atlasY + kSdfMarginAsync) / atlasMTSDF_->height();
                pg.metrics.texU1 = static_cast<float>(atlasX + kSdfMarginAsync + msdfGlyphSize_) / atlasMTSDF_->width();
                pg.metrics.texV1 = static_cast<float>(atlasY + kSdfMarginAsync + msdfGlyphSize_) / atlasMTSDF_->height();
            }
        }

        if (packed)
        {
            // Update cache
            std::lock_guard<std::mutex> lock(glyphCacheMutex_);
            CachedGlyph cached;
            cached.metrics = pg.metrics;
            cached.state = GlyphState::LOADED;
            glyphCache_[pg.codepoint] = cached;
        }
    }

    // Ensure all glyph uploads are committed before rendering continues
    if (!pending.empty())
    {
        RenderFlush();
    }
}

unsigned int rFontSTB::GetTextureId() const
{
    return GetActiveTextureId();
}

float rFontSTB::ComputeScreenPxRange() const
{
    // Screen pixel range = range * (current font size / MSDF glyph size) * multiplier
    // The multiplier allows runtime tuning for proper sharpness
    return msdfRange_ * (static_cast<float>(fontSize_) / static_cast<float>(msdfGlyphSize_)) * sr_fontSDFPxRangeMult;
}

bool rFontSTB::EnsureAtlasSpace(int width, int height)
{
    // Try doubling atlas size
    int newWidth = atlasWidth_ * 2;
    int newHeight = atlasHeight_ * 2;

    if (newWidth > MAX_ATLAS_SIZE || newHeight > MAX_ATLAS_SIZE)
    {
        return false;
    }

    // Create new larger atlas
    CreateAtlas(newWidth, newHeight);

    // Re-render all cached glyphs
    std::vector<unsigned int> codepoints;
    for (const auto& pair : glyphCache_)
    {
        codepoints.push_back(pair.first);
    }
    glyphCache_.clear();

    for (unsigned int cp : codepoints)
    {
        LoadGlyph(cp);
    }

    return true;
}

void rFontSTB::CreateAtlas(int width, int height)
{
    atlasWidth_ = width;
    atlasHeight_ = height;
    atlasData_.resize(width * height, 0);
    packer_ = std::make_unique<tRectPacker>(width, height);

    // Create or recreate OpenGL texture
    if (textureId_ == 0)
    {
        textureId_ = RenderGenTexture();
    }

    RenderBindTexture(rGLConst::Texture2D, textureId_);
    // Set pixel alignment to 1 - glyph bitmaps are tightly packed
    RenderPixelStorei(rGLConst::UnpackAlignment, 1);
    // Use rGLConst::Red format for GL3 compatibility, with swizzle to map R->A and set RGB to 1
    RenderTexImage2D(rGLConst::Texture2D, 0, rGLConst::Red, width, height, 0,
                 rGLConst::Red, rGLConst::UnsignedByte, atlasData_.data());
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMinFilter, rGLConst::Linear);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMagFilter, rGLConst::Linear);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapS, rGLConst::ClampToEdge);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapT, rGLConst::ClampToEdge);
    // Swizzle: sample R channel as Alpha, RGB as 1 (white)
    // This makes font glyphs work correctly with texColor * vColor in shader
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureSwizzleR, rGLConst::SwizzleOne);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureSwizzleG, rGLConst::SwizzleOne);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureSwizzleB, rGLConst::SwizzleOne);
    RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureSwizzleA, rGLConst::Red);

    // Flush to ensure texture is committed to GPU before use
    RenderFlush();
}

void rFontSTB::UpdateAtlasRegion(int x, int y, int width, int height, const unsigned char* data)
{
    // Update local copy
    for (int row = 0; row < height; ++row)
    {
        memcpy(&atlasData_[(y + row) * atlasWidth_ + x],
               &data[row * width], width);
    }

    // Update GPU texture
    RenderBindTexture(rGLConst::Texture2D, textureId_);
    RenderPixelStorei(rGLConst::UnpackAlignment, 1);
    RenderTexSubImage2D(rGLConst::Texture2D, 0, x, y, width, height,
                        rGLConst::Red, rGLConst::UnsignedByte, data);
    RenderFlush();
}

float rFontSTB::GetTextWidth(const char* text)
{
    if (!valid_ || !text)
        return 0;

    float width = 0;
    unsigned int prevCodepoint = 0;

    while (*text)
    {
        unsigned int codepoint = DecodeUTF8(text);
        if (codepoint == 0)
            break;

        // Add kerning
        if (prevCodepoint != 0)
        {
            width += GetKerning(prevCodepoint, codepoint);
        }

        rGlyphMetrics metrics;
        if (GetGlyphMetrics(codepoint, metrics))
        {
            width += metrics.advance;
        }

        prevCodepoint = codepoint;
    }

    return width;
}

void rFontSTB::GetTextBounds(const char* text, rTextBounds& bounds)
{
    bounds = rTextBounds();

    if (!valid_ || !text || !*text)
        return;

    float x = 0;
    float minY = 0;
    float maxY = 0;
    unsigned int prevCodepoint = 0;
    bool first = true;

    while (*text)
    {
        unsigned int codepoint = DecodeUTF8(text);
        if (codepoint == 0)
            break;

        // Add kerning
        if (prevCodepoint != 0)
        {
            x += GetKerning(prevCodepoint, codepoint);
        }

        rGlyphMetrics metrics;
        if (GetGlyphMetrics(codepoint, metrics))
        {
            if (first)
            {
                bounds.minX = static_cast<float>(metrics.bearingX);
                first = false;
            }

            float glyphTop = static_cast<float>(metrics.bearingY);
            float glyphBottom = glyphTop - metrics.height;

            if (glyphTop > maxY) maxY = glyphTop;
            if (glyphBottom < minY) minY = glyphBottom;

            x += metrics.advance;
        }

        prevCodepoint = codepoint;
    }

    bounds.maxX = x;
    bounds.minY = minY;
    bounds.maxY = maxY;
}

void rFontSTB::RenderBatched(const char* text, float ndcX, float ndcY,
                             float scaleX, float scaleY, const float color[4])
{
#ifndef DEDICATED
    if (!valid_ || !text || !*text)
        return;

    // Use the correct atlas texture for the active font mode (SDF/MSDF/MTSDF have their own atlases)
    unsigned int activeTexId = GetActiveTextureId();
    RenderBindTexture(rGLConst::Texture2D, activeTexId);

    uint8_t cr = rFloatToU8(color[0]);
    uint8_t cg = rFloatToU8(color[1]);
    uint8_t cb = rFloatToU8(color[2]);
    uint8_t ca = rFloatToU8(color[3]);

    std::vector<rVertex20> glyphVerts;
    glyphVerts.reserve(256);

    float x = 0;
    float baseline = 0;
    unsigned int prevCodepoint = 0;

    const char* p = text;
    while (*p)
    {
        unsigned int codepoint = DecodeUTF8(p);
        if (codepoint == 0)
            break;

        if (prevCodepoint != 0)
        {
            x += GetKerning(prevCodepoint, codepoint);
        }

        rGlyphMetrics metrics;
        if (GetGlyphMetrics(codepoint, metrics) && metrics.width > 0)
        {
            float lx = x + metrics.bearingX;
            float ty = baseline + metrics.bearingY;
            float rx = lx + metrics.width;
            float by = ty - metrics.height;

            // Pre-transform from glyph pixels to NDC
            float nx0 = ndcX + lx * scaleX;
            float ny0 = ndcY + ty * scaleY;
            float nx1 = ndcX + rx * scaleX;
            float ny1 = ndcY + by * scaleY;

            rVertex20 q0(nx0, ny0, 0, cr, cg, cb, ca, metrics.texU0, metrics.texV0);
            rVertex20 q1(nx1, ny0, 0, cr, cg, cb, ca, metrics.texU1, metrics.texV0);
            rVertex20 q2(nx1, ny1, 0, cr, cg, cb, ca, metrics.texU1, metrics.texV1);
            rVertex20 q3(nx0, ny1, 0, cr, cg, cb, ca, metrics.texU0, metrics.texV1);

            glyphVerts.push_back(q0);
            glyphVerts.push_back(q1);
            glyphVerts.push_back(q2);
            glyphVerts.push_back(q0);
            glyphVerts.push_back(q2);
            glyphVerts.push_back(q3);
        }

        x += metrics.advance;
        prevCodepoint = codepoint;
    }

    if (!glyphVerts.empty())
    {
        rRenderStateKey state;
        if (mode_ != FontMode::LEGACY)
        {
            uint8_t modeInt = (mode_ == FontMode::SDF) ? 1u
                            : (mode_ == FontMode::MSDF) ? 2u : 3u;
            state = rRenderStateKey::HUDFont(activeTexId, modeInt, ComputeScreenPxRange());
        }
        else
        {
            state = rRenderStateKey::HUD(activeTexId, rBlendMode::Alpha);
        }
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, glyphVerts.data(), glyphVerts.size());
    }
#endif
}

float rFontSTB::GetKerning(unsigned int left, unsigned int right)
{
    if (!fontInfo_)
        return 0;

    // Use 64-bit key for full Unicode support (codepoints can be up to 0x10FFFF)
    uint64_t key = (static_cast<uint64_t>(left) << 32) | static_cast<uint64_t>(right);
    auto it = kerningCache_.find(key);
    if (it != kerningCache_.end())
    {
        return it->second;
    }

    stbtt_fontinfo* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    int kern = stbtt_GetCodepointKernAdvance(info, static_cast<int>(left), static_cast<int>(right));
    float kernScaled = kern * scale_;

    kerningCache_[key] = kernScaled;
    return kernScaled;
}

unsigned int rFontSTB::DecodeUTF8(const char*& text)
{
    if (!text || !*text)
        return 0;

    unsigned char c = static_cast<unsigned char>(*text++);

    // ASCII
    if (c < 0x80)
    {
        return c;
    }

    // Multi-byte sequence
    unsigned int codepoint = 0;
    int extraBytes = 0;

    if ((c & 0xE0) == 0xC0)
    {
        codepoint = c & 0x1F;
        extraBytes = 1;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        codepoint = c & 0x0F;
        extraBytes = 2;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        codepoint = c & 0x07;
        extraBytes = 3;
    }
    else
    {
        // Invalid UTF-8
        return 0xFFFD; // Replacement character
    }

    for (int i = 0; i < extraBytes; ++i)
    {
        if (!*text)
            return 0xFFFD;

        c = static_cast<unsigned char>(*text++);
        if ((c & 0xC0) != 0x80)
            return 0xFFFD;

        codepoint = (codepoint << 6) | (c & 0x3F);
    }

    return codepoint;
}

// Factory implementation
#include "rFontInterface.h"
#include "tString.h"

static bool s_useSTB = true;

std::unique_ptr<rIFont> rFontFactory::Create(tString const& path, int size)
{
    auto font = std::make_unique<rFontSTB>();
    if (font->Load(static_cast<const char*>(path), size))
    {
        return font;
    }
    return nullptr;
}

void rFontFactory::SetBackend(bool useSTB)
{
    s_useSTB = useSTB;
}

bool rFontFactory::IsSTBAvailable()
{
    return true;
}

bool rFontFactory::IsFTGLAvailable()
{
    return false;
}

#endif // DEDICATED
