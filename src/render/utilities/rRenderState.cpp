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

#include "rRenderState.h"
#include "rRenderEnums.h"
#include <cmath>

rRenderState& rRenderState::Get()
{
    static rRenderState instance;
    return instance;
}

rRenderState::rRenderState()
{
    Invalidate();
}

int rRenderState::IsEnabled(int capability) const
{
    auto it = enabledCaps_.find(capability);
    if (it != enabledCaps_.end())
    {
        return it->second;
    }
    return -1;
}

bool rRenderState::SetEnabled(int capability, bool enabled)
{
    // Don't cache Texture2D - too many direct enable/disable calls
    // throughout the codebase cause the cache to desync
#ifndef DEDICATED
    if (capability == rGLConst::Texture2D)
    {
        return true; // Always forward the call
    }
#endif

    int newVal = enabled ? 1 : 0;
    auto it = enabledCaps_.find(capability);
    if (it != enabledCaps_.end() && it->second == newVal)
    {
        cacheHits_++;
        return false; // No change needed
    }
    enabledCaps_[capability] = newVal;
    cacheMisses_++;
    return true; // State changed
}

bool rRenderState::GetBlendFunc(int& sfactor, int& dfactor) const
{
    if (blendSrc_ < 0 || blendDst_ < 0)
        return false;
    sfactor = blendSrc_;
    dfactor = blendDst_;
    return true;
}

bool rRenderState::SetBlendFunc(int sfactor, int dfactor)
{
    if (blendSrc_ == sfactor && blendDst_ == dfactor)
    {
        cacheHits_++;
        return false;
    }
    blendSrc_ = sfactor;
    blendDst_ = dfactor;
    cacheMisses_++;
    return true;
}

int rRenderState::GetDepthMask() const
{
    return depthMask_;
}

bool rRenderState::SetDepthMask(bool write)
{
    int newVal = write ? 1 : 0;
    if (depthMask_ == newVal)
    {
        cacheHits_++;
        return false;
    }
    depthMask_ = newVal;
    cacheMisses_++;
    return true;
}

int rRenderState::GetCullFace() const
{
    return cullFace_;
}

bool rRenderState::SetCullFace(int face)
{
    if (cullFace_ == face)
    {
        cacheHits_++;
        return false;
    }
    cullFace_ = face;
    cacheMisses_++;
    return true;
}

int rRenderState::GetFrontFace() const
{
    return frontFace_;
}

bool rRenderState::SetFrontFace(int mode)
{
    if (frontFace_ == mode)
    {
        cacheHits_++;
        return false;
    }
    frontFace_ = mode;
    cacheMisses_++;
    return true;
}

REAL rRenderState::GetLineWidth() const
{
    return lineWidth_;
}

bool rRenderState::SetLineWidth(REAL width)
{
    if (lineWidth_ >= 0 && std::abs(lineWidth_ - width) < 0.001)
    {
        cacheHits_++;
        return false;
    }
    lineWidth_ = width;
    cacheMisses_++;
    return true;
}

bool rRenderState::GetClearColor(REAL& r, REAL& g, REAL& b, REAL& a) const
{
    if (!clearColorValid_)
        return false;
    r = clearR_;
    g = clearG_;
    b = clearB_;
    a = clearA_;
    return true;
}

bool rRenderState::SetClearColor(REAL r, REAL g, REAL b, REAL a)
{
    if (clearColorValid_ &&
        std::abs(clearR_ - r) < 0.001 &&
        std::abs(clearG_ - g) < 0.001 &&
        std::abs(clearB_ - b) < 0.001 &&
        std::abs(clearA_ - a) < 0.001)
    {
        cacheHits_++;
        return false;
    }
    clearR_ = r;
    clearG_ = g;
    clearB_ = b;
    clearA_ = a;
    clearColorValid_ = true;
    cacheMisses_++;
    return true;
}

void rRenderState::Invalidate()
{
    enabledCaps_.clear();
    blendSrc_ = -1;
    blendDst_ = -1;
    depthMask_ = -1;
    cullFace_ = -1;
    frontFace_ = -1;
    lineWidth_ = -1;
    clearColorValid_ = false;
}

void rRenderState::ResetStats()
{
    cacheHits_ = 0;
    cacheMisses_ = 0;
}
