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

#include "gMoviepack.h"
#include "gStuff.h"
#include "gGame.h"
#include "tDirectories.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tLocale.h"
#ifndef DEDICATED
#include "rVertex.h"
#include "rRenderBucket.h"
#include "rRenderQueue.h"
#endif

#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/stat.h>
#include <vector>
#ifndef WIN32
#include <dirent.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
extern "C" void sr_iOSRemoveDirectoryRecursive(const char* path);
#endif
#endif

#ifdef WIN32
#include <direct.h>
#define MKDIR_COMPAT(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR_COMPAT(path) mkdir(path, 0755)
#endif

// Include miniz for ZIP handling - use relative path from src/tron
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../thirdparty/miniz/miniz.c"

#ifndef DEDICATED
#include "rScreen.h"
#include "rRender.h"
#include "rTexture.h"
#include "rRawPixelTexture.h"
#include "rModel.h"
#include "rFont.h"
#include "rSysdep.h"
#include "rFrameLifecycle.h"
#include "rViewport.h"
#include "eSound.h"
#include "gLogo.h"
#include "uInput.h"
#include "uInputQueue.h"
#include "tSysTime.h"

// stb_image is already included elsewhere, just declare the function we need
extern "C" {
    unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len,
                                          int* x, int* y, int* comp, int req_comp);
    void stbi_image_free(void* retval_from_stbi_load);
}

// Use rRawPixelTexture from render library for preview textures
using rPreviewTexture = rRawPixelTexture;
#endif

// Configuration for persisting moviepack selection
static tString sg_moviepackName;
static tConfItem<tString> sg_moviepackNameConf("MOVIEPACK_NAME", sg_moviepackName);

// Post-process effect requested by the active moviepack's settings.cfg.
// Populated by the POST_PROCESS_EFFECT key in the pack's cfg during activation.
// Not a user-configurable item — read here and forwarded to the renderer.
static tString sg_moviepackPPEffect;
static tConfItemLine sg_moviepackPPEffectCI("POST_PROCESS_EFFECT", sg_moviepackPPEffect);

// Forward declaration for cleanup
static void RemoveDirectoryRecursive(const tString& path);

// Singleton instance
static gMoviepackManager* sg_instance = nullptr;

gMoviepackManager& gMoviepackManager::Get()
{
    if (!sg_instance)
    {
        sg_instance = new gMoviepackManager();
    }
    return *sg_instance;
}

gMoviepackManager::gMoviepackManager()
    : activeIndex_(0), zipExtracted_(false)
{
    // Add "None" as first option
    gMoviepack* none = new gMoviepack();
    none->name = "$moviepack_none";
    none->path = "";
    none->isZip = false;
    moviepacks_.push_back(none);
}

gMoviepackManager::~gMoviepackManager()
{
    CleanupTempDirectory();

    // Clean up moviepack entries
    for (int i = 0; i < moviepacks_.Len(); ++i)
    {
        delete moviepacks_(i);
    }
    moviepacks_.SetLen(0);

#ifndef DEDICATED
    // Clean up cached textures
    for (int i = 0; i < previewTextures_.Len(); ++i)
        delete previewTextures_(i);
    previewTextures_.SetLen(0);
    for (int i = 0; i < titleTextures_.Len(); ++i)
        delete titleTextures_(i);
    titleTextures_.SetLen(0);
#endif
}

void gMoviepackManager::ScanMoviepacks()
{
    // Restore settings saved before a previous moviepack activation (survives app restart).
    // This prevents stale POST_PROCESS_* values from lingering after an app kill.
    {
        tString savePath = tDirectories::GetUserData() + "/moviepack_saved_settings.cfg";
        std::ifstream loadFile(static_cast<const char*>(savePath));
        if (loadFile.good())
        {
            tCurrentAccessLevel elevate(tAccessLevel_Owner, true);
            tConfItemBase::LoadAll(loadFile, false);
            loadFile.close();
            unlink(static_cast<const char*>(savePath));
        }
    }

    // Clean up any previously extracted ZIP moviepack first
    // This prevents the extracted files from being detected as a "Classic Moviepack"
    tString extractPath = GetTempExtractPath();
    if (extractPath.Len() > 0)
    {
        RemoveDirectoryRecursive(extractPath);
    }
    zipExtracted_ = false;
    extractPath_ = "";

    // Clear existing moviepacks (except "None")
    for (int i = 1; i < moviepacks_.Len(); ++i)
    {
        delete moviepacks_(i);
    }
    if (moviepacks_.Len() > 1)
    {
        moviepacks_.SetLen(1);
    }

#ifndef DEDICATED
    // Clear cached textures
    for (int i = 0; i < previewTextures_.Len(); ++i)
        delete previewTextures_(i);
    previewTextures_.SetLen(0);
    for (int i = 0; i < titleTextures_.Len(); ++i)
        delete titleTextures_(i);
    titleTextures_.SetLen(0);
#endif

    // Get all data paths
    tArray<tString> paths;
    tDirectories::Data().GetPaths(paths);

    // Check for legacy moviepack folder using tDirectories
    {
        std::ifstream t;
        if (tDirectories::Data().Open(t, "moviepack/settings.cfg"))
        {
            t.close();
            // Find the actual path to the moviepack folder
            tString folderPath = tDirectories::Data().GetReadPath("moviepack/settings.cfg");
            // Remove /settings.cfg from the path
            int lastSlash = folderPath.StrPos("/settings.cfg");
            if (lastSlash > 0)
            {
                folderPath = folderPath.SubStr(0, lastSlash);
            }

            gMoviepack* classic = new gMoviepack();
            classic->name = "$moviepack_classic";
            classic->path = folderPath;
            classic->isZip = false;
            moviepacks_.push_back(classic);
        }
        else
        {
        }
    }

    // Scan for .aamvp.zip files in moviepacks/ subdirectory
    for (int p = 0; p < paths.Len(); ++p)
    {
        tString moviepacksDir = paths(p);
        moviepacksDir += "/moviepacks";

        tArray<tString> files;
        tDirectories::GetFiles(moviepacksDir, tString("*.aamvp.zip"), files,
                               tDirectories::eGetFilesFilesOnly);

        for (int f = 0; f < files.Len(); ++f)
        {
            tString filename = files(f);
            tString fullPath = moviepacksDir;
            fullPath += "/";
            fullPath += filename;

            // Extract display name from filename (remove .aamvp.zip)
            tString displayName = filename;
            int extPos = displayName.StrPos(".aamvp.zip");
            if (extPos > 0)
            {
                displayName = displayName.SubStr(0, extPos);
            }

            // Replace underscores with spaces for display
            for (size_t i = 0; i < displayName.Size(); ++i)
            {
                if (displayName[i] == '_')
                {
                    displayName[i] = ' ';
                }
            }

            // Check if we already have this moviepack (from a higher priority path)
            bool exists = false;
            for (int m = 0; m < moviepacks_.Len(); ++m)
            {
                if (moviepacks_(m)->name == displayName)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
            {
                gMoviepack* pack = new gMoviepack();
                pack->name = displayName;
                pack->path = fullPath;
                pack->isZip = true;
                moviepacks_.push_back(pack);
            }
        }
    }

    // Sort moviepacks alphabetically by name (skip index 0 = "None")
    for (int i = 1; i < moviepacks_.Len() - 1; ++i)
    {
        for (int j = i + 1; j < moviepacks_.Len(); ++j)
        {
            if (strcmp(static_cast<const char*>(moviepacks_(j)->name),
                       static_cast<const char*>(moviepacks_(i)->name)) < 0)
            {
                gMoviepack* tmp = moviepacks_(i);
                moviepacks_(i) = moviepacks_(j);
                moviepacks_(j) = tmp;
            }
        }
    }

    // Restore selection from saved name
    if (sg_moviepackName.Len() > 0)
    {
        RestoreFromName(sg_moviepackName);
    }
    else if (sg_moviepackInstalled && sg_moviepackUse)
    {
        // Legacy compatibility: if old MOVIEPACK was enabled, select classic pack
        for (int i = 0; i < moviepacks_.Len(); ++i)
        {
            if (!moviepacks_(i)->isZip && moviepacks_(i)->path.Len() > 0)
            {
                activeIndex_ = i;
                break;
            }
        }
    }

#ifndef DEDICATED
    // Initialize texture cache arrays
    previewTextures_.SetLen(moviepacks_.Len());
    titleTextures_.SetLen(moviepacks_.Len());
    for (int i = 0; i < moviepacks_.Len(); ++i)
    {
        previewTextures_(i) = nullptr;
        titleTextures_(i) = nullptr;
    }
#endif

    // Activate the restored moviepack (extracts ZIP if needed)
    if (activeIndex_ > 0)
    {
        ActivateMoviepack();
    }
}

const gMoviepack* gMoviepackManager::GetMoviepack(int index) const
{
    if (index >= 0 && index < moviepacks_.Len())
    {
        return moviepacks_(index);
    }
    return nullptr;
}

void gMoviepackManager::SetActiveIndex(int index)
{
    if (index >= 0 && index < moviepacks_.Len() && index != activeIndex_)
    {
        // Moviepack changes during gameplay are now safe — shader reload is
        // deferred to next BeginFrame (after GPU idle), and texture unload
        // waits for GPU idle before freeing resources.

        // Deactivate old moviepack
        DeactivateMoviepack();

        // When switching to None, restore user's original settings
        if (index == 0)
        {
            tString savePath = tDirectories::GetUserData() + "/moviepack_saved_settings.cfg";
            std::ifstream loadFile(static_cast<const char*>(savePath));
            if (loadFile.good())
            {
                tCurrentAccessLevel elevate(tAccessLevel_Owner, true);
                tConfItemBase::LoadAll(loadFile, false);
                loadFile.close();
            }
            unlink(static_cast<const char*>(savePath));
        }

        activeIndex_ = index;

        // Save name for persistence
        if (index > 0)
        {
            sg_moviepackName = moviepacks_(index)->name;
        }
        else
        {
            sg_moviepackName = "";
        }

        // Activate new moviepack
        ActivateMoviepack();
    }
}

const tString& gMoviepackManager::GetActiveMoviepackName() const
{
    static tString empty;
    if (activeIndex_ > 0 && activeIndex_ < moviepacks_.Len())
    {
        return moviepacks_(activeIndex_)->name;
    }
    return empty;
}

void gMoviepackManager::RestoreFromName(const tString& name)
{
    for (int i = 0; i < moviepacks_.Len(); ++i)
    {
        if (moviepacks_(i)->name == name)
        {
            activeIndex_ = i;
            return;
        }
    }
    // Name not found, default to None
    activeIndex_ = 0;
}

// Helper to recursively remove a directory (forward declaration for use in ActivateMoviepack)
static void RemoveDirectoryRecursive(const tString& path)
{
#if defined(WIN32)
    tString cmd;
    cmd = "rmdir /s /q \"";
    cmd += path;
    cmd += "\"";
    (void)system(static_cast<const char*>(cmd));
#elif defined(__APPLE__) && TARGET_OS_IOS
    // Use NSFileManager on iOS — POSIX recursive delete can hang on sandbox paths.
    sr_iOSRemoveDirectoryRecursive(static_cast<const char*>(path));
#else
    // Android and other POSIX: system() may be unavailable or restricted.
    // Use opendir/readdir/unlink/rmdir recursively instead.
    const char* p = static_cast<const char*>(path);
    DIR* d = opendir(p);
    if (!d)
    {
        // Directory doesn't exist — nothing to remove.
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        tString child = path;
        child += "/";
        child += entry->d_name;
        const char* cp = static_cast<const char*>(child);
        struct stat st;
        if (stat(cp, &st) == 0 && S_ISDIR(st.st_mode))
        {
            RemoveDirectoryRecursive(child);
        }
        else
        {
            unlink(cp);
        }
    }
    closedir(d);
    rmdir(p);
#endif
}

bool gMoviepackManager::ActivateMoviepack()
{
    if (activeIndex_ <= 0 || activeIndex_ >= moviepacks_.Len())
    {
        return true; // "None" selected, nothing to do
    }

    const gMoviepack* pack = moviepacks_(activeIndex_);
    if (!pack)
    {
        return false;
    }


    if (pack->isZip)
    {
        // Get extraction path and clean up any existing files first
        extractPath_ = GetTempExtractPath();
        RemoveDirectoryRecursive(extractPath_);

        // Verify the directory is actually gone — stale files from a previous pack
        // would be silently mixed with the new pack's content otherwise.
        struct stat st;
        if (stat(static_cast<const char*>(extractPath_), &st) == 0)
        {
            // Directory still exists after removal attempt (permission issue?).
            // Log a warning but continue — extraction will overwrite what it can,
            // but files not present in the new ZIP will linger.
            con << "^1Warning: could not fully clean moviepack directory before extraction: "
                << extractPath_ << "\n";
        }

        // Extract ZIP to the moviepack directory
        if (!ExtractZipToDirectory(pack->path, extractPath_))
        {
            con << tOutput("$moviepack_extract_failed", pack->name) << "\n";
            return false;
        }
        // Verify extracted moviepack is valid (must have settings.cfg)
        tString settingsPath = extractPath_;
        settingsPath += "/settings.cfg";

        std::ifstream settingsFile(static_cast<const char*>(settingsPath));
        if (!settingsFile.good())
        {
            con << tOutput("$moviepack_invalid", pack->name) << "\n";
            RemoveDirectoryRecursive(extractPath_);
            return false;
        }
        settingsFile.close();

        zipExtracted_ = true;

        // Save current user settings on FIRST moviepack activation only
        // (so switching to None restores the user's original values).
        {
            tString savePath = tDirectories::GetUserData() + "/moviepack_saved_settings.cfg";
            struct stat st;
            if (stat(static_cast<const char*>(savePath), &st) != 0)
            {
                // File doesn't exist yet — save current settings
                static const char* moviepackKeys[] = {
                    "MOVIEPACK_FLOOR_RED", "MOVIEPACK_FLOOR_GREEN", "MOVIEPACK_FLOOR_BLUE",
                    "MOVIEPACK_RIM_WALL_STRETCH_X", "MOVIEPACK_RIM_WALL_STRETCH_Y",
                    "MOVIEPACK_WALL_STRETCH", "GRID_SIZE_MOVIEPACK", "FLOOR_DETAIL",
                    nullptr
                };
                std::ofstream saveFile(static_cast<const char*>(savePath));
                for (int k = 0; moviepackKeys[k]; ++k)
                {
                    tConfItemBase* item = tConfItemBase::FindConfigItem(tString(moviepackKeys[k]));
                    if (item)
                    {
                        std::ostringstream val;
                        item->WriteVal(val);
                        if (saveFile.good())
                            saveFile << moviepackKeys[k] << " " << val.str() << "\n";
                    }
                }
            }
        }

        // Reset moviepack-affected settings to defaults before applying the pack's
        // settings.cfg. This ensures a clean baseline regardless of what the previous
        // pack or user changes left behind. Values match the C++ default initializers.
        {
            tCurrentAccessLevel elevate(tAccessLevel_Owner, true);
            tNoisinessSetter silent(false);
            static const char* resetLines =
                "POST_PROCESS_EFFECT \n"
                "MOVIEPACK_FLOOR_RED 0.5\n"
                "MOVIEPACK_FLOOR_GREEN 0.5\n"
                "MOVIEPACK_FLOOR_BLUE 0.5\n"
                "MOVIEPACK_RIM_WALL_STRETCH_X 100\n"
                "MOVIEPACK_RIM_WALL_STRETCH_Y 100\n"
                "MOVIEPACK_WALL_STRETCH 4\n"
                "GRID_SIZE_MOVIEPACK 2\n"
                "FLOOR_DETAIL 2\n";
            std::istringstream resetStream(resetLines);
            tConfItemBase::LoadAll(resetStream, false);
        }

        // Apply the moviepack's settings.cfg with owner elevation.
        {
            std::ifstream applyFile(static_cast<const char*>(settingsPath));
            if (applyFile.good())
            {
                tCurrentAccessLevel elevate(tAccessLevel_Owner, true);
                tNoisinessSetter silent(false);
                tConfItemBase::LoadAll(applyFile, false);
            }
        }
    }

#ifndef DEDICATED
    // Reload all resources so moviepack assets take effect
    // This applies to both ZIP and classic folder moviepacks
    // Only reload if OpenGL context is available (sr_glOut)
    if (sr_glOut)
    {
        // Wait for GPU to finish all in-flight work before unloading
        // textures/models. Without this, in-flight command buffers may
        // still reference resources we're about to free.
        extern void sr_vkWaitIdle();
        sr_vkWaitIdle();

        gLogo::ResetTexture();
        rSurfaceCache::ClearCache();
        rITexture::UnloadAll();
        rModel::ClearCache();
        eLegacyWavData::UnloadAll();
        sr_ReloadFont();
        // Reload Vulkan SPV shaders if using the Vulkan renderer
        extern void sr_vkRendererReloadShaders();
        sr_vkRendererReloadShaders();

        // Notify the post-process system so it can reload effects from
        // the new moviepack directory and register MVP_* tSettingItems
        // for any shader parameters declared by .meta files in the pack.
        extern void sr_vkPostProcessOnMoviepackActivated(const char* name);
        const gMoviepack* activePack = moviepacks_(activeIndex_);
        sr_vkPostProcessOnMoviepackActivated(
            activePack ? static_cast<const char*>(activePack->name) : nullptr);

        // Activate post-processing with the effect requested by the moviepack's cfg.
        // sg_moviepackPPEffect was populated by POST_PROCESS_EFFECT in the settings.cfg
        // loaded above. If the key wasn't present, the string is empty and PP stays off.
        extern void sr_vkPostProcessActivate(const char* effectName);
        extern void sr_vkPostProcessDeactivate();
        if (sg_moviepackPPEffect.Len() > 0)
            sr_vkPostProcessActivate(static_cast<const char*>(sg_moviepackPPEffect));
        else
            sr_vkPostProcessDeactivate();
    }
#endif

    // Update legacy flags for compatibility
    sg_moviepackInstalled = true;
    sg_moviepackUse = true;

    return true;
}

void gMoviepackManager::NotifyRendererReady()
{
#ifndef DEDICATED
    // Only act if a moviepack is active and the renderer is up
    if (activeIndex_ <= 0 || !sr_glOut) return;

    const gMoviepack* activePack = moviepacks_(activeIndex_);

    // Reload uber shaders from the moviepack's pre-compiled SPVs.
    // On startup, ScanMoviepacks() extracted the ZIP but sr_glOut was 0,
    // so ActivateMoviepack()'s shader reload was skipped. Do it now.
    extern void sr_vkRendererReloadShaders();
    sr_vkRendererReloadShaders();

    // Tell the PP system which moviepack is active so it uses the correct
    // shader directory when loading effect scripts (bloom.lua etc.).
    extern void sr_vkPostProcessOnMoviepackActivated(const char*);
    sr_vkPostProcessOnMoviepackActivated(
        activePack ? static_cast<const char*>(activePack->name) : nullptr);

    // Queue the PP effect from the moviepack's settings.cfg for activation
    // on the next BeginFrame.
    extern void sr_vkPostProcessActivate(const char*);
    extern void sr_vkPostProcessDeactivate();
    if (sg_moviepackPPEffect.Len() > 0)
        sr_vkPostProcessActivate(static_cast<const char*>(sg_moviepackPPEffect));
    else
        sr_vkPostProcessDeactivate();
#endif
}

void gMoviepackManager::DeactivateMoviepack()
{
    // Note: activeIndex_ still holds the OLD value at this point
    bool hadMoviepackActive = (activeIndex_ > 0);

#ifndef DEDICATED
    // Notify the post-process system BEFORE resources are freed — this
    // saves any tuned MVP values to the outgoing moviepack's cfg file
    // and destroys dynamic tSettingItems so they don't reference stale
    // effect data after the reload.
    if (hadMoviepackActive && sr_glOut)
    {
        // Wait for all in-flight GPU work to finish before destroying pipelines,
        // descriptor pools, and framebuffers used by the active PP effect.
        // Without this, vkDestroyPipeline/vkDestroyDescriptorPool fire while
        // command buffers are still in flight → validation errors.
        extern void sr_vkWaitIdle();
        sr_vkWaitIdle();
        extern void sr_vkPostProcessOnMoviepackDeactivated();
        sr_vkPostProcessOnMoviepackDeactivated();
        extern void sr_vkPostProcessDeactivate();
        sr_vkPostProcessDeactivate();
    }
#endif

    CleanupTempDirectory();
    zipExtracted_ = false;

    // Note: settings restoration happens in two places:
    // - When switching to None: RestoreUserSettings() is called below
    // - When switching between packs: ActivateMoviepack() reloads defaults first
    // The saved settings file is only consumed when switching to None.

    // Always set moviepack to inactive when deactivating
    // ActivateMoviepack() will set it back to true if activating a new one
    sg_moviepackUse = false;

#ifndef DEDICATED
    // If any moviepack was active and GL is available, reload resources to restore defaults
    if (hadMoviepackActive && sr_glOut)
    {
        // Reset logo texture and hide it — switching to "None" shouldn't flash a title
        gLogo::ResetTexture();
        gLogo::SetDisplayed(false, true);

        // Unload all textures so they reload from correct paths
        rSurfaceCache::ClearCache();
        rITexture::UnloadAll();

        // Clear model cache so they reload
        rModel::ClearCache();

        // Unload sounds
        eLegacyWavData::UnloadAll();

        // Reload font
        sr_ReloadFont();

        // Reload Vulkan SPV shaders if using the Vulkan renderer
        extern void sr_vkRendererReloadShaders();
        sr_vkRendererReloadShaders();
    }
#endif
}

tString gMoviepackManager::GetTempExtractPath() const
{
    // Extract to user data directory's moviepack folder
    // This path is already searched by tDirectories::Data()
    tString userDataDir = tDirectories::GetUserData();
    if (userDataDir.Len() > 0)
    {
        return userDataDir + "/moviepack";
    }
    // Fallback to var directory if user data dir is not set
    return tDirectories::Var().GetWritePath("moviepack");
}

void gMoviepackManager::CleanupTempDirectory()
{
    if (!zipExtracted_ || extractPath_.Len() == 0)
    {
        return;
    }

    // Remove the extracted directory
    RemoveDirectoryRecursive(extractPath_);

    zipExtracted_ = false;
    extractPath_ = "";
}

bool gMoviepackManager::ExtractFileFromZip(const tString& zipPath,
                                            const char* fileName,
                                            void** outData, size_t* outSize)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    // Use implicit conversion from tString to const char*
    if (!mz_zip_reader_init_file(&zip, static_cast<char const*>(zipPath), 0))
    {
        return false;
    }

    int fileIndex = mz_zip_reader_locate_file(&zip, fileName, nullptr, 0);
    if (fileIndex < 0)
    {
        mz_zip_reader_end(&zip);
        return false;
    }

    mz_zip_archive_file_stat fileStat;
    if (!mz_zip_reader_file_stat(&zip, fileIndex, &fileStat))
    {
        mz_zip_reader_end(&zip);
        return false;
    }

    *outSize = static_cast<size_t>(fileStat.m_uncomp_size);
    *outData = malloc(*outSize);
    if (!*outData)
    {
        mz_zip_reader_end(&zip);
        return false;
    }

    if (!mz_zip_reader_extract_to_mem(&zip, fileIndex, *outData, *outSize, 0))
    {
        free(*outData);
        *outData = nullptr;
        *outSize = 0;
        mz_zip_reader_end(&zip);
        return false;
    }

    mz_zip_reader_end(&zip);
    return true;
}

bool gMoviepackManager::ExtractZipToDirectory(const tString& zipPath,
                                               const tString& destDir)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, static_cast<char const*>(zipPath), 0))
    {
        return false;
    }

    // Create destination directory
    MKDIR_COMPAT(static_cast<char const*>(destDir));

    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));

    for (int i = 0; i < numFiles; ++i)
    {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat))
        {
            continue;
        }

        tString destPath = destDir;
        destPath += "/";
        destPath += fileStat.m_filename;


        if (mz_zip_reader_is_file_a_directory(&zip, i))
        {
            // Create directory
            MKDIR_COMPAT(static_cast<char const*>(destPath));
        }
        else
        {
            // Ensure parent directories exist (ZIP entries may not list
            // intermediate directories explicitly, causing extract to fail)
            {
                std::string dp = static_cast<char const*>(destPath);
                for (size_t pos = dp.find('/', destDir.Len()); pos != std::string::npos; pos = dp.find('/', pos + 1))
                {
                    std::string parent = dp.substr(0, pos);
                    MKDIR_COMPAT(parent.c_str());
                }
            }

            // Extract file
            if (!mz_zip_reader_extract_to_file(&zip, i, static_cast<char const*>(destPath), 0))
            {
                // Log error but continue
                con << "Failed to extract: " << fileStat.m_filename << "\n";
            }
        }
    }

    mz_zip_reader_end(&zip);
    return true;
}

#ifndef DEDICATED
// Shared helper: load a texture from a moviepack ZIP file and cache it.
rITexture* gMoviepackManager::LoadTextureFromZip(
    const tString& zipPath, const char* filename,
    tArray<rITexture*>& cache, int index)
{
    void* data = nullptr;
    size_t dataSize = 0;

    if (!ExtractFileFromZip(zipPath, filename, &data, &dataSize))
        return nullptr;

    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(dataSize),
        &width, &height, &channels, 4);
    free(data);

    if (!pixels) return nullptr;

    rPreviewTexture* tex = new rPreviewTexture();
    if (tex->LoadFromPixels(pixels, width, height))
    {
        while (cache.Len() <= index)
            cache.push_back(nullptr);
        cache(index) = tex;
        stbi_image_free(pixels);
        return tex;
    }
    delete tex;
    stbi_image_free(pixels);
    return nullptr;
}

rITexture* gMoviepackManager::GetPreviewTexture(int index)
{
    if (index < 0 || index >= moviepacks_.Len()) return nullptr;
    if (index < previewTextures_.Len() && previewTextures_(index)) return previewTextures_(index);
    const gMoviepack* pack = moviepacks_(index);
    if (!pack || index == 0) return nullptr;
    if (pack->isZip)
        return LoadTextureFromZip(pack->path, "preview.png", previewTextures_, index);
    return nullptr;
}

rITexture* gMoviepackManager::GetTitleTexture(int index)
{
    if (index < 0 || index >= moviepacks_.Len()) return nullptr;
    if (index < titleTextures_.Len() && titleTextures_(index)) return titleTextures_(index);
    const gMoviepack* pack = moviepacks_(index);
    if (!pack || index == 0) return nullptr;
    if (pack->isZip)
        return LoadTextureFromZip(pack->path, "title.jpg", titleTextures_, index);
    return nullptr;
}

// Menu item implementation
gMoviepackMenuItem::gMoviepackMenuItem(uMenu* menu)
    : uMenuItemSelection<int>(menu,
                              tOutput("$misc_moviepack_text"),
                              tOutput("$misc_moviepack_help"),
                              selectionIndex_),
      selectionIndex_(0)
{
    // Populate choices from manager
    gMoviepackManager& mgr = gMoviepackManager::Get();
    for (int i = 0; i < mgr.GetCount(); ++i)
    {
        const gMoviepack* pack = mgr.GetMoviepack(i);
        if (pack)
        {
            NewChoice(tOutput(static_cast<const char*>(pack->name)), tOutput(""), i);
        }
    }

    // Set initial selection
    selectionIndex_ = mgr.GetActiveIndex();
    menu->RequestSpaceBelow(0.2);
}

gMoviepackMenuItem::~gMoviepackMenuItem()
{
}

// Helper: submit a textured quad to the HUD render queue
static void sg_RenderQuad(rITexture* tex, float left, float top, float right, float bottom)
{
    tex->Select();
    unsigned int texId = RenderGetBoundTexture2D();
    std::vector<rVertex20> v;
    v.reserve(6);
    v.push_back(rVertex20(left,  top,    0, 255,255,255,255, 0,0));
    v.push_back(rVertex20(left,  bottom, 0, 255,255,255,255, 0,1));
    v.push_back(rVertex20(right, bottom, 0, 255,255,255,255, 1,1));
    v.push_back(rVertex20(left,  top,    0, 255,255,255,255, 0,0));
    v.push_back(rVertex20(right, bottom, 0, 255,255,255,255, 1,1));
    v.push_back(rVertex20(right, top,    0, 255,255,255,255, 1,0));
    rRenderStateKey state = rRenderStateKey::Textured(texId, rBlendMode::Alpha);
    rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, v.data(), v.size());
}

void gMoviepackMenuItem::RenderBackground()
{
    uMenuItem::RenderBackground();
    if (!sr_glOut) return;

    gMoviepackManager& mgr = gMoviepackManager::Get();
    rITexture* title   = mgr.GetTitleTexture(selectionIndex_);
    rITexture* preview = mgr.GetPreviewTexture(selectionIndex_);

    if (!title && !preview) return;

    // Layout: right side of screen, stacking from the bottom up.
    // Both images use the screen's aspect ratio so they look like
    // miniature screenshots. NDC is [-1,1]; screen aspect = W/H.
    float aspect = (sr_screenWidth > 0 && sr_screenHeight > 0)
        ? (float)sr_screenWidth / (float)sr_screenHeight : 1.77f;

    const float L = 0.55f, R = 0.95f;
    const float ndcW = R - L;
    const float imgH = ndcW / aspect * 2.0f;  // height matching screen aspect (NDC Y range is 2)
    const float gap = 0.02f;
    float y = -0.95f;                          // bottom edge of lowest image

    if (title && preview)
    {
        sg_RenderQuad(preview, L, y + imgH, R, y);
        y += imgH + gap;
        sg_RenderQuad(title, L, y + imgH, R, y);
    }
    else if (title)
    {
        sg_RenderQuad(title, L, y + imgH, R, y);
    }
    else
    {
        sg_RenderQuad(preview, L, y + imgH, R, y);
    }
}

void gMoviepackMenuItem::LeftRight(int lr)
{
    uMenuItemSelection<int>::LeftRight(lr);
    // Preview images (title.jpg, preview.png) are loaded lazily in RenderBackground
    // via GetTitleTexture/GetPreviewTexture. No activation here — just browse.
}

void gMoviepackMenuItem::LeftRightRelease()
{
    // Browse only — preview images are displayed, but the moviepack is NOT activated.
    // Activation (shader reload, texture unload, settings change) only happens on Enter.
    // This eliminates the flashing caused by rapid activate/deactivate cycles (BUG 15).
}

void gMoviepackMenuItem::Enter()
{
    // Activate the selected moviepack (extract ZIP, apply settings, reload shaders).
    // This is the only place that triggers the heavy resource reload.
    gMoviepackManager::Get().SetActiveIndex(selectionIndex_);
}

void gMoviepackMenuItem::UpdateFromManager()
{
    selectionIndex_ = gMoviepackManager::Get().GetActiveIndex();
}

#endif // DEDICATED (end of non-dedicated section)

// =============================================================================
// --compile-moviepack: offline GLSL → SPIR-V compilation tool
// =============================================================================
// Compiles all shaders referenced by a moviepack and writes the SPIR-V back
// into the ZIP alongside the GLSL sources.  iOS/Android can then load the
// pre-compiled .spv files instead of using shaderc at runtime.
//
// Usage:  armagetronad --compile-moviepack path/to/pack.aamvp.zip
// =============================================================================

#if !defined(DEDICATED) && defined(HAVE_SHADERC_SHADERC_HPP)

#include "vulkan/rVulkanShader.h"
#include <filesystem>
#include <iostream>

namespace
{

std::vector<uint32_t> compileShaderFile(
    const std::string& src,
    rVulkanShader::Stage stage,
    const std::vector<std::string>& includePaths,
    const std::vector<std::pair<std::string, std::string>>& defines = {})
{
    std::string err;
    auto spv = rVulkanShader::CompileGLSLFromFile(src, stage, includePaths, defines, &err);
    if (spv.empty())
        std::cerr << "  FAIL  " << src << "\n" << err << "\n";
    else
        std::cout << "  OK    " << src << "  (" << spv.size() * 4 << " B)\n";
    return spv;
}

bool writeSPVFile(const std::string& path, const std::vector<uint32_t>& spv)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(spv.data()), spv.size() * sizeof(uint32_t));
    return f.good();
}

} // namespace

bool sr_CompileMoviepack(const char* zipPath)
{
    namespace fs = std::filesystem;

    std::cout << "[compile-moviepack] " << zipPath << "\n";

    // ── 1. Extract ZIP to temp dir ────────────────────────────────────────────
    char tmpl[] = "/tmp/aamvp_XXXXXX";
    char* tmpBuf = mkdtemp(tmpl);
    if (!tmpBuf)
    {
        std::cerr << "Error: cannot create temp dir\n";
        return false;
    }
    std::string tmp = tmpBuf;

    {
        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));
        if (!mz_zip_reader_init_file(&zip, zipPath, 0))
        {
            std::cerr << "Error: cannot open ZIP: " << zipPath << "\n";
            fs::remove_all(tmp);
            return false;
        }
        int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
        for (int i = 0; i < n; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
            std::string dest = tmp + "/" + st.m_filename;
            if (mz_zip_reader_is_file_a_directory(&zip, i))
                fs::create_directories(dest);
            else
            {
                fs::create_directories(fs::path(dest).parent_path());
                mz_zip_reader_extract_to_file(&zip, i, dest.c_str(), 0);
            }
        }
        mz_zip_reader_end(&zip);
    }

    // ── 2. Locate system shaders directory ────────────────────────────────────
    std::string sysShaderDir;
    {
        tString sv = tDirectories::Data().GetReadPath("shaders/uber.vert");
        if (sv.Len() > 1)
        {
            std::string p = static_cast<const char*>(sv);
            sysShaderDir = p.substr(0, p.find_last_of('/'));
        }
    }
    if (sysShaderDir.empty())
    {
        // Fallback: try "shaders" relative to CWD (convenient when run from source tree)
        if (fs::exists("shaders/uber.vert"))
            sysShaderDir = "shaders";
    }
    if (sysShaderDir.empty())
    {
        std::cerr << "Error: system shaders directory not found.\n"
                     "  Hint: use --datadir <path-to-data> to specify the data directory.\n"
                     "  Example (dev build): --datadir ../../ --compile-moviepack ...\n";
        fs::remove_all(tmp);
        return false;
    }
    std::cout << "  System shaders: " << sysShaderDir << "\n";

    // ── 3. Compile uber + shadow + compute shaders ────────────────────────────
    std::string mvShadersDir = tmp + "/shaders";
    fs::create_directories(mvShadersDir);
    std::vector<std::string> uberInc = { mvShadersDir, sysShaderDir };

    // Pick from moviepack if present, otherwise fall back to system shader.
    auto pickSrc = [&](const std::string& name) -> std::string {
        std::string mv = mvShadersDir + "/" + name;
        return fs::exists(mv) ? mv : sysShaderDir + "/" + name;
    };

    // Compile and save; silently skip if the source doesn't exist at all.
    auto compileAndSave = [&](const std::string& src,
                               rVulkanShader::Stage stage,
                               const std::vector<std::string>& inc,
                               const std::string& outRelative,
                               const std::vector<std::pair<std::string,std::string>>& defs = {})
    {
        if (!fs::exists(src)) { std::cout << "  SKIP  " << src << " (not found)\n"; return; }
        auto spv = compileShaderFile(src, stage, inc, defs);
        if (!spv.empty()) writeSPVFile(mvShadersDir + "/" + outRelative, spv);
    };

    using S = rVulkanShader::Stage;
    compileAndSave(pickSrc("uber.vert"),           S::Vertex,   uberInc, "uber.vert.spv");
    compileAndSave(pickSrc("uber_instanced.vert"), S::Vertex,   uberInc, "uber_instanced.vert.spv");
    compileAndSave(pickSrc("uber.frag"),           S::Fragment, uberInc, "uber.frag.spv");
    compileAndSave(pickSrc("uber.frag"),           S::Fragment, uberInc, "uber.frag.emissive.spv",
                   {{"USE_EMISSIVE_OUT", "1"}});
    compileAndSave(pickSrc("shadow.vert"),         S::Vertex,   uberInc, "shadow.vert.spv");
    compileAndSave(pickSrc("shadow.frag"),         S::Fragment, uberInc, "shadow.frag.spv");
    compileAndSave(pickSrc("wall_gen.comp"),       S::Compute,  uberInc, "wall_gen.comp.spv");

    // ── 4. Compile post-process shaders ───────────────────────────────────────
    std::string mvPPDir  = mvShadersDir + "/postprocess";
    std::string sysPPDir = sysShaderDir + "/postprocess";
    fs::create_directories(mvPPDir);

    // Shared fullscreen.vert (used by every PP pass)
    {
        std::string src = fs::exists(mvPPDir + "/fullscreen.vert")
                          ? mvPPDir + "/fullscreen.vert"
                          : sysPPDir + "/fullscreen.vert";
        if (fs::exists(src))
        {
            auto spv = compileShaderFile(src, S::Vertex, {mvPPDir, sysPPDir, sysShaderDir});
            if (!spv.empty()) writeSPVFile(mvPPDir + "/fullscreen.vert.spv", spv);
        }
    }

    // Per-effect .frag shaders
    if (fs::is_directory(mvPPDir))
    {
        for (const auto& effEntry : fs::directory_iterator(mvPPDir))
        {
            if (!effEntry.is_directory()) continue;
            std::string effDir    = effEntry.path().string();
            std::string effName   = effEntry.path().filename().string();
            std::string sysEffDir = sysPPDir + "/" + effName;
            std::vector<std::string> ppInc = { effDir, sysEffDir, sysPPDir, sysShaderDir };

            for (const auto& fe : fs::directory_iterator(effDir))
            {
                if (!fe.is_regular_file()) continue;
                std::string fname = fe.path().filename().string();
                if (fname.size() < 5 || fname.substr(fname.size() - 5) != ".frag") continue;

                std::string src = fe.path().string();
                auto spv = compileShaderFile(src, S::Fragment, ppInc);
                if (!spv.empty()) writeSPVFile(src + ".spv", spv);
            }
        }
    }

    // ── 5. Rebuild ZIP from temp dir ──────────────────────────────────────────
    std::string newZipPath = std::string(zipPath) + ".new";
    bool zipOk = false;
    {
        mz_zip_archive writer;
        memset(&writer, 0, sizeof(writer));
        if (!mz_zip_writer_init_file(&writer, newZipPath.c_str(), 0))
        {
            std::cerr << "Error: cannot create output ZIP\n";
        }
        else
        {
            zipOk = true;
            for (const auto& fe : fs::recursive_directory_iterator(tmp))
            {
                if (!fe.is_regular_file()) continue;
                std::string full = fe.path().string();
                std::string arc  = full.substr(tmp.size() + 1);
                if (!mz_zip_writer_add_file(&writer, arc.c_str(), full.c_str(),
                                             nullptr, 0, MZ_DEFAULT_COMPRESSION))
                {
                    std::cerr << "  Warning: failed to add " << arc << " to ZIP\n";
                    zipOk = false;
                }
            }
            if (!mz_zip_writer_finalize_archive(&writer))
            {
                std::cerr << "Error: cannot finalize ZIP\n";
                zipOk = false;
            }
            mz_zip_writer_end(&writer);
        }
    }

    // ── 6. Replace original ZIP ───────────────────────────────────────────────
    if (zipOk)
    {
        if (rename(newZipPath.c_str(), zipPath) != 0)
        {
            std::cerr << "Error: cannot replace original ZIP\n";
            zipOk = false;
        }
        else
        {
            std::cout << "[compile-moviepack] Done: " << zipPath << "\n";
        }
    }
    if (!zipOk)
        std::remove(newZipPath.c_str());

    fs::remove_all(tmp);
    return zipOk;
}

#endif // !DEDICATED && HAVE_SHADERC_SHADERC_HPP

bool sg_MoviepackHasFile(const char* relPath)
{
    // No-op when no moviepack is active. Callers can drop the explicit
    // sg_MoviePack() guard and let this be the single switch.
    if (!sg_MoviePack())
        return false;
    if (!relPath || !*relPath)
        return false;

    tString full = tString("moviepack/") + tString(relPath);
    tString resolved = tDirectories::Data().GetReadPath(full);
    // GetReadPath returns an empty / single-NUL tString when the file is
    // not found in any data search root. The same idiom is used elsewhere
    // in this file (see ScanMoviepacks) to detect missing moviepack files.
    return resolved.Len() > 1;
}
