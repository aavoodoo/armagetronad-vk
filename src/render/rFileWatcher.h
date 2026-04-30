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

#ifndef RFILEWATCHER_H
#define RFILEWATCHER_H

#ifndef DEDICATED

#include <functional>
#include <string>
#include <vector>
#include <ctime>

//! Non-blocking file watcher. Notifies when watched files change on disk.
//!
//! Designed for development-time hot-reload of Lua effect scripts. Must be
//! polled each frame via Poll() — it never spins a background thread.
//!
//! Platform support:
//!   macOS  — kqueue + EVFILT_VNODE (O_EVTONLY). Handles atomic editor saves
//!            (vim, Xcode) by watching the *directory* for NOTE_WRITE events
//!            and verifying mtime on individual files.
//!   iOS    — no-op (app bundle is read-only at runtime).
//!   Android— no-op (inotify integration is a future enhancement).
//!   Other  — no-op.
//!
//! Callback receives the tag string passed to Watch() for the changed file
//! (typically the effect name, not the full path).
class rFileWatcher
{
public:
    using Callback = std::function<void(const std::string& tag)>;

    rFileWatcher();
    ~rFileWatcher();

    //! Register a file to watch. `tag` is passed back in the callback when
    //! the file changes.  Silently ignored if the file doesn't exist yet
    //! (the first mtime check will pick it up once it appears).
    void Watch(const std::string& path, const std::string& tag);

    //! Remove all watched files and release platform resources.
    void Clear();

    //! Set the callback invoked when a watched file changes.
    void SetCallback(Callback cb) { callback_ = std::move(cb); }

    //! Poll for changes. Call once per frame. Non-blocking.
    //! Fires the callback synchronously for each changed file found.
    void Poll();

    // Non-copyable
    rFileWatcher(const rFileWatcher&) = delete;
    rFileWatcher& operator=(const rFileWatcher&) = delete;

private:
    struct WatchedFile
    {
        std::string path;
        std::string tag;
        time_t      mtime = 0;
    };

    Callback              callback_;
    std::vector<WatchedFile> files_;

#if defined(__APPLE__)
    // kqueue fd (-1 if not open)
    int kq_ = -1;
    // Watched directory fd (-1 if not open)
    int dirFd_ = -1;
    std::string watchedDir_;

    void OpenDirectory(const std::string& dir);
#endif
};

#endif // DEDICATED
#endif // RFILEWATCHER_H
