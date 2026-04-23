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

Font atlas - wrapper for generic texture atlas

*/

#ifndef RFONTATLAS_H
#define RFONTATLAS_H

// Use the generic texture atlas implementation
#include "rTextureAtlas.h"

// Font-specific type aliases using the generic atlas
using rFontAtlasSDF = rTextureAtlas<glm::u8vec1>;
using rFontAtlasMSDF = rTextureAtlas<glm::u8vec3>;
using rFontAtlasMTSDF = rTextureAtlas<glm::u8vec4>;

// Legacy compatibility - traits are now in the generic header
template <typename Pixel>
using AtlasTraits = TextureAtlasTraits<Pixel>;

#endif  // RFONTATLAS_H
