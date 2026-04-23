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

#ifndef ArmageTron_RAW_PIXEL_TEXTURE_H
#define ArmageTron_RAW_PIXEL_TEXTURE_H

#include "rTexture.h"
#include "rRenderEnums.h"

#ifndef DEDICATED

// Texture class that creates an OpenGL texture from raw pixel data
// Used for dynamically generated textures like moviepack previews
class rRawPixelTexture : public rITexture
{
public:
    rRawPixelTexture();
    ~rRawPixelTexture() override;

    // Load texture from raw pixel data
    // @param pixels Pointer to pixel data (will be copied)
    // @param width Width in pixels
    // @param height Height in pixels
    // @param format Pixel format (default RGBA8)
    // @return true if texture was created successfully
    bool LoadFromPixels(const unsigned char* pixels, int width, int height,
                        rPixelFormat format = rPixelFormat::RGBA8);

    // Get texture dimensions
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    // Check if texture is valid
    bool IsValid() const { return textureId_ != 0; }

protected:
    void OnSelect(bool enforce) override;
    void OnUnload() override;

private:
    unsigned int textureId_;
    int width_;
    int height_;
};

#endif // DEDICATED

#endif // ArmageTron_RAW_PIXEL_TEXTURE_H
