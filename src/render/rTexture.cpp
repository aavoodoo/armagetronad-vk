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

#include "rSDL.h"

#include "aa_config.h"

// stb_image for image loading (replaces SDL_image)
#ifndef DEDICATED
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <string>
#include <fstream>
#endif

#include "rTexture.h"
#include "tString.h"
#include "rScreen.h"
#include "tDirectories.h"
#include "tLocale.h"
#include "tConsole.h"
#include "tException.h"
#include "tResourceManager.h"

#include <sstream>
#include <set>
#include <iostream>
#ifdef __ANDROID__
#include <vector>
#endif

#ifndef DEDICATED
#include "rRender.h"

// Helper function to load image using stb_image and create SDL_Surface
static SDL_Surface* sr_LoadImageSTB(const char* filename)
{
    // Check for empty filename
    if (!filename || !filename[0])
    {
        return nullptr;
    }

    // Load image with stb_image, force RGBA output for consistency
    int width, height, originalChannels;
    unsigned char* pixels = nullptr;

#ifdef __ANDROID__
    // On Android, stbi_load uses fopen which cannot read APK assets.
    // Use SDL_IOFromFile (routed through AAssetManager) then stbi_load_from_memory.
    {
        const char* assetPath = filename;
        if (assetPath[0] == '.' && assetPath[1] == '/')
            assetPath += 2;
        SDL_IOStream* io = SDL_IOFromFile(assetPath, "rb");
        if (io)
        {
            Sint64 size = SDL_GetIOSize(io);
            if (size > 0)
            {
                std::vector<unsigned char> buf((size_t)size);
                SDL_ReadIO(io, buf.data(), (size_t)size);
                SDL_CloseIO(io);
                pixels = stbi_load_from_memory(buf.data(), (int)size,
                                               &width, &height, &originalChannels, 4);
            }
            else
            {
                SDL_CloseIO(io);
            }
        }
    }
#else
    pixels = stbi_load(filename, &width, &height, &originalChannels, 4);
#endif

    if (!pixels)
    {
        return nullptr;
    }

    // stb_image returns R,G,B,A byte order - create surface with RGBA format
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    SDL_PixelFormat srcFormat = SDL_PIXELFORMAT_RGBA8888;
    SDL_PixelFormat dstFormat = SDL_PIXELFORMAT_BGRA8888;
#else
    SDL_PixelFormat srcFormat = SDL_PIXELFORMAT_ABGR8888;
    SDL_PixelFormat dstFormat = SDL_PIXELFORMAT_ARGB8888;
#endif

    // Create a temporary surface with the raw pixel data
    SDL_Surface* tempSurface = SDL_CreateSurfaceFrom(
        width, height, srcFormat, pixels, width * 4);

    if (!tempSurface)
    {
        stbi_image_free(pixels);
        return nullptr;
    }

    // Convert to target format
    SDL_Surface* convertedSurface = SDL_ConvertSurface(tempSurface, dstFormat);

    SDL_DestroySurface(tempSurface);
    stbi_image_free(pixels);

    return convertedSurface;
}
#endif


// ******************************************************************************************
// *
// *	rSurface
// *
// ******************************************************************************************
//!
//!		@param	fileName	name of the file to load sufrace from
//!
// ******************************************************************************************

rSurface::rSurface( char const * fileName, tPath const * path )
{
    Init();
    Create( fileName, path );
}

// ******************************************************************************************
// *
// *	~rSurface
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rSurface::~rSurface( void )
{
    Clear();
}

// ******************************************************************************************
// *
// *	rSurface
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rSurface::rSurface( void )
{
    Init();
}

// ******************************************************************************************
// *
// *   rSurface
// *
// ******************************************************************************************
//!
//!        @param  other   source to copy from
//!
// ******************************************************************************************

rSurface::rSurface( rSurface const & other )
{
    Init();
    CopyFrom( other );
}

// ******************************************************************************************
// *
// *   operator =
// *
// ******************************************************************************************
//!
//!        @param  other
//!        @return
//!
// ******************************************************************************************

rSurface & rSurface::operator =( rSurface const & other )
{
    if ( &other != this )
    {
        Clear();
        CopyFrom( other );
    }

    return *this;
}

// ******************************************************************************************
// *
// *	Init
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rSurface::Init( void )
{
    surface_ = 0;
    format_ = 0;
}

// ******************************************************************************************
// *
// *   Clear
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rSurface::Clear( void )
{
#ifndef DEDICATED
    // delete surface
    if ( surface_ )
        SDL_DestroySurface( surface_ );

#endif
    surface_ = 0;
}

// ******************************************************************************************
// *
// *	Create
// *
// ******************************************************************************************
//!
//!		@param	fileName	name of the image file to load
//!
// ******************************************************************************************

void rSurface::Create( char const * fileName, tPath const *path )
{
#ifndef DEDICATED
    sr_LockSDL();

    // find path of image and load it using stb_image
    SDL_Surface *surface;
    if(path) {
        tString s = path->GetReadPath( fileName );
        surface = sr_LoadImageSTB(s.c_str());
    } else {
        surface = sr_LoadImageSTB(fileName);
    }
    Create(surface);

    sr_UnlockSDL();
#endif
}

// ******************************************************************************************
// *
// *	Create
// *
// ******************************************************************************************
//!
//!		@param	surface
//!
// ******************************************************************************************

void rSurface::Create( SDL_Surface * surface )
{
#ifndef DEDICATED
    // clear previous surface
    Clear();

    // take ownership
    surface_ = surface;

    // determine texture format
    if ( surface_ )
    {
        switch (AA_GetSurfaceBytesPerPixel(surface_)){
        case 1:
            format_ = rGLConst::Luminance;
            break;

        case 2:
            format_ = rGLConst::Luminance8Alpha8;
            break;

        case 3:
            if (AA_GetSurfaceRmask(surface_) == 0x000000ff)
                format_ = rGLConst::RGB;
            else
                format_ = rGLConst::BGR;
            break;

        case 4:
            if (AA_GetSurfaceRmask(surface_) == 0x000000ff)
                format_ = rGLConst::RGBA;
            else
                format_ = rGLConst::BGRA;
            break;

        default:
            {
                // fallback: convert the texture into a known format.
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                SDL_PixelFormat targetFormat = SDL_PIXELFORMAT_BGRA8888;
                format_ = rGLConst::BGRA;
#else
                SDL_PixelFormat targetFormat = SDL_PIXELFORMAT_RGBA8888;
                format_ = rGLConst::RGBA;
#endif

                SDL_Surface *convtex = SDL_ConvertSurface(surface_, targetFormat);

                SDL_DestroySurface(surface_);
                surface_ = convtex;
            }
            break;
        }
    }
#endif
}

// ******************************************************************************************
// *
// *	CreateQuarter
// *
// ******************************************************************************************
//!
//!		@param	surface to scale down
//!
// ******************************************************************************************

void rSurface::CreateQuarter( rSurface const & big )
{
#ifndef DEDICATED
    // clear previous surface
    Clear();

    tASSERT( big.surface_ );
    format_ = big.format_;

    // determine dimensions
    int sourceW = big.surface_->w;
    int sourceH = big.surface_->h;
    int w = (sourceW+1)/2;
    int h = (sourceH+1)/2;

    // create new surface of new sizes with same format
    surface_ = SDL_CreateSurface(w, h, AA_GetSurfaceFormat(big.surface_));

    tASSERT( surface_ );

    int bytesPerPixel = AA_GetSurfaceBytesPerPixel(surface_);
    int sourcePitch = big.surface_->pitch;
    int pitch = surface_->pitch;
    unsigned char const * source = (unsigned char const *)big.surface_->pixels;
    unsigned char * dest = (unsigned char *) surface_->pixels;
    for( int i = 0; i < h; ++i )
    {
        int off = i * pitch;
        int soff1 = (i<<1)*sourcePitch;
        int soff2 = (((i<<1)+1)%sourceH)*sourcePitch;
        for( int j = 0; j < w; ++j )
        {
            int ind = j*bytesPerPixel;
            int sind1 = (j<<1)*bytesPerPixel;
            int sind2 = (((j<<1)+1)%sourceW)*bytesPerPixel;

            for( int b = 0; b < bytesPerPixel; ++b )
            {
                // box filter
                dest[off+ind+b] = ( source[soff1+sind1+b] + source[soff2+sind1+b] + source[soff1+sind2+b] + source[soff2+sind2+b] )>>2;
            }
        }
    }
#endif
}

// ******************************************************************************************
// *
// *	CopyFrom
// *
// ******************************************************************************************
//!
//!		@param	other
//!
// ******************************************************************************************

void rSurface::CopyFrom( rSurface const & other )
{
#ifndef DEDICATED
    tASSERT( 0 == surface_ );
    if( other.surface_ )
    {
        // copy surface with same format
        surface_ = SDL_ConvertSurface(other.surface_, AA_GetSurfaceFormat(other.surface_));

        // copy flags
        format_ = other.format_;
    }
#endif
}

// surface cache
typedef std::pair< tString, tPath const * > rSurfaceCacheKey;

class rSurfaceCacheValue
{
public:
    rSurface surface;
    bool used;

    rSurfaceCacheValue()
    : used( true )
    {}
};

typedef std::map< rSurfaceCacheKey, rSurfaceCacheValue > rSurfaceCacheMap;
static rSurfaceCacheMap sr_surfaceCacheMap;

// ******************************************************************************************
// *
// *	GetSurface
// *
// ******************************************************************************************
//!
//!		@param	fileName the file name of the surface relative to the given path
//!     @param  path     search path
//!
// ******************************************************************************************
rSurface const * rSurfaceCache::GetSurface( char const * fileName, tPath const *path )
{
    rSurfaceCacheKey key( tString( fileName ), path );
    // bool inCache = ( sr_surfaceCacheMap.find(key) != sr_surfaceCacheMap.end() );

    rSurfaceCacheValue & val = sr_surfaceCacheMap[ key ];
    if( !val.surface.GetSurface() )
    {
        // first time, load
        val.surface.Create( fileName, path );
    }

    if( !val.surface.GetSurface() )
    {
        // previous error, don't retry
        return NULL;
    }

    // mark as used and return
    val.used = true;
    return &val.surface;
}

// ******************************************************************************************
// *
// *	CycleCache
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************
void rSurfaceCache::CycleCache()
{
    for( rSurfaceCacheMap::iterator i = sr_surfaceCacheMap.begin(); i != sr_surfaceCacheMap.end();  )
    {
        rSurfaceCacheMap::iterator next = i;
        next++;

        rSurfaceCacheValue & value = (*i).second;
        if( !value.used )
        {
            sr_surfaceCacheMap.erase( i );
        }
        else
        {
            value.used = false;
        }

        i = next;
    }
}

// ******************************************************************************************
// *
// *	ClearCache
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************
void rSurfaceCache::ClearCache()
{
    sr_surfaceCacheMap.clear();
}

// ******************************************************************************************
// *
// *	~rITexture
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rITexture::~rITexture( void )
{
    s_textures_.Remove(this,id_);
}

// ******************************************************************************************
// *
// *	UnloadAll
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rITexture::UnloadAll( void )
{
    for(int i=s_textures_.Len()-1;i>=0;i--)
    {
        s_textures_(i)->Unload();
    }
    rSurfaceCache::ClearCache();
}

// ******************************************************************************************
// *
// *	LoadAll
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rITexture::LoadAll( void )
{
    // s_reportErrors=false;
    for(int i=s_textures_.Len()-1;i>=0;i--)
    {
        s_textures_(i)->Select();
        //if (i>=s_textures.Len())
        //    i=s_textures.Len()-1;
    }
    // s_reportErrors=true;
}

// ******************************************************************************************
// *
// *	rITexture
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rITexture::rITexture( void )
        : id_( -1 )
{
}

// ******************************************************************************************
// *
// *	OnSelect
// *
// ******************************************************************************************
//!
//!		@param	enforce enforce when set to true, the texture should be loaded even if the configuration says it should not
//!
// ******************************************************************************************

void rITexture::OnSelect( bool enforce )
{
    if ( id_ < 0 )
        s_textures_.Add(this,id_);
}

// ******************************************************************************************
// *
// *	OnUnload
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rITexture::OnUnload( void )
{
}

// ******************************************************************************************
// *
// *	rISurfaceTexture
// *
// ******************************************************************************************
//!
//!		@param	group	texture group ( floor/wall)
//!		@param	repx    flag indicating the x repeat mode
//!		@param	repy    flag indicating the y repeat mode
//!		@param	storeAlpha flag indicating whether the alpha channel should be stored
//!
// ******************************************************************************************

rISurfaceTexture::rISurfaceTexture( int group, bool repx, bool repy, bool storeAlpha )
        : group_( group ), textureModeLast_( -1), repx_( repx ), repy_( repy ), storeAlpha_( storeAlpha )
{
}

// ******************************************************************************************
// *
// *	~rISurfaceTexture
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rISurfaceTexture::~rISurfaceTexture( void )
{
#ifndef DEDICATED
    if (tint_) { RenderDeleteTexture(tint_); tint_ = 0; }
#endif
}

// ******************************************************************************************
// *
// *	ProcessImage
// *
// ******************************************************************************************
//!
//!		@param	surface the surface to process
//!
// ******************************************************************************************

void rISurfaceTexture::ProcessImage( SDL_Surface * surface )
{
}

#ifndef DEDICATED
static bool sr_IsPowerOfTwo( int i )
{
    return i == 1 || ( ( (i & 1) == 0  ) && sr_IsPowerOfTwo( i >> 1 ) );
}

static int sr_GetMaxTextureSizeCore()
{
    // guaranteed supported size
    int maxSize = RenderGetMaxTextureSize();
    if (maxSize < 64) maxSize = 64;
    return maxSize;
}

static int sr_GetMaxTextureSize()
{
    static int maxSize = sr_GetMaxTextureSizeCore();
    return maxSize;
}
#endif

// ******************************************************************************************
// *
// *	Upload
// *
// ******************************************************************************************
//!
//!		@param	surface
//!
// ******************************************************************************************

void rISurfaceTexture::Upload( rSurface const & surface )
{
#ifndef DEDICATED
    sr_LockSDL();
    int texformat = surface.GetFormat();
    SDL_Surface * tex = surface.GetSurface();
    tASSERT( tex );

    bool texalpha = AA_GetSurfaceAmask(tex) != 0;

    ProcessImage(tex);

    if(repx_)
        RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapS,rGLConst::Repeat);
    else
        RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapS,rGLConst::ClampToEdge);
    if(repy_)
        RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapT,rGLConst::Repeat);
    else
        RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapT,rGLConst::ClampToEdge);

    int format;
    if (sr_texturesTruecolor)
        if (storageHack_ || ( storeAlpha_ && texalpha ) )
            format=rGLConst::RGBA8;
        else
            format=rGLConst::RGB8;
    else
        if (storageHack_ || ( storeAlpha_ && texalpha ) )
            format=rGLConst::RGBA4;
        else
            format=rGLConst::RGB5;

    if( !sr_IsPowerOfTwo( tex->w ) || !sr_IsPowerOfTwo( tex->h ) )
    {
        static bool warn = true;
        if( warn )
        {
            warn = false;
            rFileTexture * texture = dynamic_cast< rFileTexture * >( this );
            if( texture )
            {
                con << "\nWARNING: non-power-of-two texture dimensions in texture " << texture->GetFileName() << ". If you're the artist creating it, correct it by rescaling, please; it may cease to work in future versions or even not work right now for some people.\n\n";
            }
            else
            {
                con << "\nWARNING: non-power-of-two texture dimensions in unknown texture. If you're the artist creating one, recheck your work, it may cease to work in future versions or even not work right now for some people.\n\n";
            }
        }

        // no power of two, use modern mipmap generation
        RenderTexImage2D(rGLConst::Texture2D, 0, format, tex->w, tex->h, 0,
                         texformat, rGLConst::UnsignedByte, tex->pixels);
        RenderGenerateMipmap(rGLConst::Texture2D);
    }
    else
    {
        int level = 0;

        // mipmap generation pipeline
        rSurface even(surface), odd(surface);
        rSurface * current = &even;
        rSurface * next = &odd;

        bool sizeOK = false;
        while(true)
        {
            // upload current as mipmap level.
            tex = current->GetSurface();
            tASSERT( tex );

            // test whether the size is OK
            if( !sizeOK && tex->w <= sr_GetMaxTextureSize() && tex->h <= sr_GetMaxTextureSize() )
            {
                // so far, so good; check via proxy
                RenderTexImage2D(rGLConst::ProxyTexture2D,level,format,tex->w,tex->h,0,
                                 texformat,rGLConst::UnsignedByte,tex->pixels);
                int width = RenderGetTexLevelParameteriv(rGLConst::ProxyTexture2D, 0, rGLConst::TextureWidth);
                sizeOK = ( width != 0 );
            }

            if( sizeOK )
            {
                // upload and increase level
                RenderTexImage2D(rGLConst::Texture2D,level,format,tex->w,tex->h,0,
                                 texformat,rGLConst::UnsignedByte,tex->pixels);
                level++;
            }

            // scale down for next level
            if( tex->w == 1 && tex->h == 1 )
            {
                break;
            }
            else
            {
                next->CreateQuarter( *current );
                rSurface * swap = next;
                next = current;
                current = swap;
            }
        }
    }

    sr_UnlockSDL();
 #endif
}

// ******************************************************************************************
// *
// *	OnSelect
// *
// ******************************************************************************************
//!
//!		@param	enforce enforce when set to true, the texture should be loaded even if the configuration says it should not
//!
// ******************************************************************************************

void rISurfaceTexture::OnSelect( bool enforce )
{
#ifndef DEDICATED
    if(sr_glOut)
    {
        int texmod=rTextureGroups::TextureMode[group_];
        if (enforce && texmod<0) texmod=rGLConst::NearestMipmapNearest;

        if(textureModeLast_!=texmod)
        {
            // unload texture if the mode changed
            Unload();
            // std::cerr << "loading texture " << fileName << ':' << tint << "\n";

            if (texmod>0){
                if (!tint_) tint_ = RenderGenTexture();
                RenderBindTexture(rGLConst::Texture2D,tint_);

                if (textureModeLast_<0)
                {
                    // delegate core loading work to derived class
                    OnSelectCore();
                }

                RenderEnableState(rGLConst::Texture2D);

                RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureMinFilter,
                                   texmod);

                switch(texmod)
                {
                case rGLConst::Nearest:
                case rGLConst::NearestMipmapNearest:
                    RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureMagFilter,
                                       rGLConst::Nearest);
                    break;
                default:
                    RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureMagFilter,
                                       rGLConst::Linear);
                    break;
                }

            }
            else
            {
                RenderDisableState(rGLConst::Texture2D);
            }
        }
        else
        {
            if (!tint_) tint_ = RenderGenTexture();
            RenderBindTexture(rGLConst::Texture2D,tint_);
            if (texmod>0)
            {
                RenderEnableState(rGLConst::Texture2D);
            }
            else
            {
                RenderDisableState(rGLConst::Texture2D);
            }
        }
        textureModeLast_=texmod;
    }
    rITexture::OnSelect(enforce);
#endif
}

// ******************************************************************************************
// *
// *	OnUnload
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rISurfaceTexture::OnUnload( void )
{
#ifndef DEDICATED
    if (tint_) { RenderDeleteTexture(tint_); tint_ = 0; }
    textureModeLast_=-100;
    rITexture::OnUnload();
#endif
}

// ******************************************************************************************
// *
// *	StoreAlpha
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rISurfaceTexture::StoreAlpha( void )
{
    storeAlpha_ = true;
}

// ******************************************************************************************
// *
// *	rFileTexture
// *
// ******************************************************************************************
//!
//!		@param	group	texture group ( floor/wall)
//!		@param	fileName the filename of the picture to load
//!		@param	repx    flag indicating the x repeat mode
//!		@param	repy    flag indicating the y repeat mode
//!		@param	storeAlpha flag indicating whether the alpha channel should be stored
//!
// ******************************************************************************************

rFileTexture::rFileTexture( int group, char const * fileName, bool repx, bool repy, bool storeAlpha, tPath const *path )
        : rISurfaceTexture( group, repx, repy, storeAlpha )
        ,  fileName_( fileName )
        ,  path_(path)
{
}

// ******************************************************************************************
// *
// *	~rFileTexture
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rFileTexture::~rFileTexture( void )
{
}

// ******************************************************************************************
// *
// *	OnSelectCore
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rFileTexture::OnSelectCore()
{
#ifndef DEDICATED
    // std::cerr << "loading texture " << fileName_ << "\n";
    rSurface const * surface = rSurfaceCache::GetSurface( fileName_, path_ );
    if ( surface )
    {
        this->Upload( *surface );
    }
    else if (s_reportErrors_)
    {
        throw tGenericException( tOutput( "$texture_error_filenotfound", fileName_ ), tOutput("$texture_error_filenotfound_title") );
    }
#endif
}

// ******************************************************************************************
// *
// *	rSurfaceTexture
// *
// ******************************************************************************************
//!
//!		@param	group	texture group ( floor/wall)
//!		@param	surface
//!		@param	repx    flag indicating the x repeat mode
//!		@param	repy    flag indicating the y repeat mode
//!		@param	storeAlpha flag indicating whether the alpha channel should be stored
//!
// ******************************************************************************************

rSurfaceTexture::rSurfaceTexture( int group, rSurface const & surface, bool repx, bool repy, bool storeAlpha )
        : rISurfaceTexture( group, repx, repy, storeAlpha )
        , surface_( surface )
{
}

// ******************************************************************************************
// *
// *	~rSurfaceTexture
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

rSurfaceTexture::~rSurfaceTexture( void )
{
}

// ******************************************************************************************
// *
// *	OnSelectCore
// *
// ******************************************************************************************
//!
//!
// ******************************************************************************************

void rSurfaceTexture::OnSelectCore()
{
#ifndef DEDICATED
    // upload a copy of the surface ( it may get modified )
    if ( surface_.GetSurface() )
    {
        rSurface copy( surface_ );
        this->Upload( copy );
    }
#endif
}


bool rISurfaceTexture::s_reportErrors_=false;
tList<rITexture> rITexture::s_textures_;

int rTextureGroups::TextureMode[rTextureGroups::TEX_GROUPS]
#ifndef DEDICATED
={rGLConst::LinearMipmapLinear, rGLConst::LinearMipmapLinear, rGLConst::LinearMipmapLinear, rGLConst::Linear }
#endif
;

char const * rTextureGroups::TextureGroupDescription[rTextureGroups::TEX_GROUPS]=
    {
        "$texture_mode_0_help",
        "$texture_mode_1_help",
        "$texture_mode_2_help",
        "$texture_mode_3_help",
    };

bool rISurfaceTexture::storageHack_ = false;

//rTexture ArmageTron_eWall("wWall.png",1,0);
//rTexture ArmageTron_dir_eWall("wall.png",1,0);


static rCallbackBeforeScreenModeChange unload(&rITexture::UnloadAll);

// static rCallbackAfterScreenModeChange load(&rITexture::LoadAll);

rResourceTexture::texlist_t rResourceTexture::textures;

rResourceTexture::rResourceTexture(tResourcePath const &path, bool repx, bool repy) : repx_(repx), repy_(repy) {
    if(!path.Valid()) {
        tex_ = 0;
        return;
    }
    for(texlist_t::iterator iter = textures.begin(); iter != textures.end(); ++iter) {
        if((*iter)->path_ == path) {
            tex_ = *iter;
            tex_->Use();
            return;
        }
    }
    tex_ = new tex_t(path);
}

// verifies a resource texture, redownloads it if it is corrupted (once per session)
static void sr_VerifyResourceTexture( tString const & resourcePath )
{
#ifndef DEDICATED
    static std::set< tString > verified;
    if( verified.find(resourcePath) == verified.end() )
    {
        tString filePath = tResourceManager::locateResource( resourcePath, "", false  );
        if( filePath != "" )
        {
            // check read and write path
            tString w = tDirectories::Resource().GetWritePath( filePath );
            tString r = tDirectories::Resource().GetReadPath( filePath );

            // if they're equal, that means the resource has been downloaded before. Check it
            // by loading it once
            if( w == r )
            {
                rSurface const * surface = rSurfaceCache::GetSurface( filePath, &tDirectories::Resource() );
                if( !surface )
                {
                    // trigger a redownload
                    tResourceManager::locateResource( resourcePath, "", true, true );
                }
            }
        }

        // mark as checked
        verified.insert( resourcePath );
    }
#endif
}

rResourceTexture::InternalTex::InternalTex(tResourcePath const &path) : rFileTexture(rTextureGroups::TEX_OBJ, tResourceManager::locateResource(path.Path().c_str()).c_str(), true, true, true, 0), use_(1), path_(path) {
    sr_VerifyResourceTexture( path.Path() );

    textures.push_back(this);
}

void rResourceTexture::InternalTex::Release() {
    if(--use_ < 1) {
        for(texlist_t::iterator iter = textures.begin(); iter != textures.end(); ++iter) {
            if((*iter)->path_ == path_) {
                textures.erase(iter);
                break;
            }
        }
        Unload();
        delete this;
    }
}

rResourceTexture &rResourceTexture::operator=(rResourceTexture const &other) {
    if(tex_ != other.tex_) {
        repx_ = other.repx_;
        repy_ = other.repy_;
        if(tex_) {
            tex_->Release();
        }
        tex_ = other.tex_;
        if(tex_) {
            tex_->Use();
        }
    }
    return *this;
}

void rResourceTexture::Select() {
#ifndef DEDICATED
    if(tex_) {
        tex_->Select();
        // Override the actual texture's settings
        if(repx_)
            RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapS,rGLConst::Repeat);
        else
            RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapS,rGLConst::ClampToEdge);
        if(repy_)
            RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapT,rGLConst::Repeat);
        else
            RenderTexParameter(rGLConst::Texture2D,rGLConst::TextureWrapT,rGLConst::ClampToEdge);
    } else {
        tERR_WARN("Trying to select a resource texture that's not loaded");
    }
#endif
}
