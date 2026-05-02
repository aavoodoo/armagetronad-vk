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

#ifndef ArmageTron_MOVIEPACK_H
#define ArmageTron_MOVIEPACK_H

#include "tString.h"
#include "tArray.h"
#include "uMenu.h"
#include <vector>
#include <string>
#include <utility>

#ifndef DEDICATED
#include "rTexture.h"
#endif

//! Information about a single moviepack
struct gMoviepack
{
    tString name;      //!< Display name (derived from filename)
    tString path;      //!< Full path to .aamvp.zip file or legacy folder
    bool isZip;        //!< True if this is a zip file, false for legacy folder

    gMoviepack() : isZip(false) {}
    gMoviepack(const tString& n, const tString& p, bool zip)
        : name(n), path(p), isZip(zip) {}
};

//! Manager for moviepack selection and activation
class gMoviepackManager
{
public:
    //! Get the singleton instance
    static gMoviepackManager& Get();

    //! Scan for available moviepacks in data directories
    void ScanMoviepacks();

    //! Get the number of available moviepacks (including "None")
    int GetCount() const { return moviepacks_.Len(); }

    //! Get moviepack info by index
    const gMoviepack* GetMoviepack(int index) const;

    //! Get the currently active moviepack index
    int GetActiveIndex() const { return activeIndex_; }

    //! Set the active moviepack by index
    void SetActiveIndex(int index);

    //! Get the active moviepack name (for config persistence)
    const tString& GetActiveMoviepackName() const;

    //! Restore selection from saved name
    void RestoreFromName(const tString& name);

    //! Activate the currently selected moviepack (extract zip if needed)
    bool ActivateMoviepack();

    //! Deactivate current moviepack and cleanup
    void DeactivateMoviepack();

    //! Check if a moviepack is currently active (not "None")
    bool IsMoviepackActive() const { return activeIndex_ > 0; }

    //! Called after the renderer initializes to apply PP effects from the active
    //! moviepack. ScanMoviepacks() runs before sr_glOut is set, so the renderer-
    //! dependent part of ActivateMoviepack() (PP activation, effect search path)
    //! is skipped at startup and must be re-applied once the renderer is ready.
    void NotifyRendererReady();

#ifndef DEDICATED
    //! Extract and cache preview image for a moviepack
    //! Returns cached texture, or NULL if no preview available
    rITexture* GetPreviewTexture(int index);

    //! Extract and cache title image for a moviepack
    //! Returns cached texture, or NULL if no title available
    rITexture* GetTitleTexture(int index);
#endif

private:
    gMoviepackManager();
    ~gMoviepackManager();

    // Prevent copying
    gMoviepackManager(const gMoviepackManager&);
    gMoviepackManager& operator=(const gMoviepackManager&);

    //! Extract a single file from a zip archive to memory
    bool ExtractFileFromZip(const tString& zipPath, const char* fileName,
                            void** outData, size_t* outSize);

#ifndef DEDICATED
    //! Load a texture from a ZIP file and cache it
    rITexture* LoadTextureFromZip(const tString& zipPath, const char* filename,
                                   tArray<rITexture*>& cache, int index);
#endif

    //! Extract entire zip contents to a directory
    bool ExtractZipToDirectory(const tString& zipPath, const tString& destDir);

    //! Get path for temporary moviepack extraction
    tString GetTempExtractPath() const;

    //! Cleanup temporary extraction directory
    void CleanupTempDirectory();

    tArray<gMoviepack*> moviepacks_; //!< Available moviepacks (index 0 = "None")
    int activeIndex_;                 //!< Currently selected index
    tString extractPath_;             //!< Path where current zip is extracted
    bool zipExtracted_;               //!< Whether a zip is currently extracted

    //! Saved pre-moviepack config values, restored on deactivation
    std::vector<std::pair<std::string, std::string>> savedSettings_;

#ifndef DEDICATED
    tArray<rITexture*> previewTextures_; //!< Cached preview textures
    tArray<rITexture*> titleTextures_;   //!< Cached title textures
#endif
};

#ifndef DEDICATED
//! Menu item for moviepack selection with preview
class gMoviepackMenuItem : public uMenuItemSelection<int>
{
public:
    gMoviepackMenuItem(uMenu* menu);
    virtual ~gMoviepackMenuItem();

    //! Render background with preview image
    virtual void RenderBackground();

    //! Reserve space for preview image on right
    virtual REAL SpaceRight() { return 0.25; }

    //! Called when selection changes (scroll only — activation deferred to key release)
    virtual void LeftRight(int lr);

    //! Called when left/right key is released — activates the selected moviepack
    virtual void LeftRightRelease();

    //! Called when Enter is pressed - applies moviepack and returns to title
    virtual void Enter();

private:
    int selectionIndex_;  //!< Local selection state
    void UpdateFromManager();
};
#endif

#if !defined(DEDICATED) && defined(HAVE_SHADERC_SHADERC_HPP)
//! Compile all GLSL shaders in a moviepack ZIP and add pre-compiled SPIR-V alongside the sources.
//! Only available on platforms with shaderc (macOS/desktop). Prints progress to stdout.
//! Returns true on success; the ZIP is updated in place.
bool sr_CompileMoviepack(const char* zipPath);
#endif

//! True if the active moviepack ships a file at moviepack/<relPath>.
//! Used by texture-selection sites to fall back to the system default when
//! a "lighting only" / minimal moviepack does not override every asset.
//! Returns false when no moviepack is active.
bool sg_MoviepackHasFile(const char* relPath);

#endif // ArmageTron_MOVIEPACK_H
