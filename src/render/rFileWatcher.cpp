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

#include "aa_config.h"

#ifndef DEDICATED

#include "rFileWatcher.h"

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !TARGET_OS_IOS
#  include <sys/event.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <libgen.h>   // dirname()
#  include <cstring>
#  include <cerrno>
#  include <iostream>
#  define FILEWATCHER_KQUEUE 1
#endif

rFileWatcher::rFileWatcher()
{
#if FILEWATCHER_KQUEUE
    kq_ = kqueue();
    if (kq_ < 0)
        std::cerr << "[FileWatcher] kqueue() failed: " << strerror(errno) << "\n";
#endif
}

rFileWatcher::~rFileWatcher()
{
    Clear();
#if FILEWATCHER_KQUEUE
    if (kq_ >= 0) { close(kq_); kq_ = -1; }
#endif
}

void rFileWatcher::Clear()
{
#if FILEWATCHER_KQUEUE
    if (dirFd_ >= 0) { close(dirFd_); dirFd_ = -1; }
    watchedDir_.clear();
#endif
    files_.clear();
}

#if FILEWATCHER_KQUEUE
void rFileWatcher::OpenDirectory(const std::string& dir)
{
    if (dir == watchedDir_) return;

    if (dirFd_ >= 0) close(dirFd_);
    watchedDir_.clear();
    dirFd_ = -1;

    dirFd_ = open(dir.c_str(), O_RDONLY | O_EVTONLY);
    if (dirFd_ < 0)
    {
        std::cerr << "[FileWatcher] Cannot watch directory '" << dir
                  << "': " << strerror(errno) << "\n";
        return;
    }

    struct kevent ev{};
    EV_SET(&ev, static_cast<uintptr_t>(dirFd_), EVFILT_VNODE,
           EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME,
           0, nullptr);
    if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0)
    {
        std::cerr << "[FileWatcher] kevent registration failed: " << strerror(errno) << "\n";
        close(dirFd_);
        dirFd_ = -1;
        return;
    }

    watchedDir_ = dir;
}
#endif

void rFileWatcher::Watch(const std::string& path, const std::string& tag)
{
    WatchedFile wf;
    wf.path = path;
    wf.tag  = tag;
    wf.mtime = 0;

#if FILEWATCHER_KQUEUE
    // Seed the initial mtime so the first Poll() doesn't fire a spurious reload.
    struct stat st{};
    if (stat(path.c_str(), &st) == 0)
        wf.mtime = st.st_mtime;

    // Derive the directory from the path and watch it (idempotent for the same dir).
    // Using a char buffer because POSIX dirname() may modify its argument.
    char buf[4096];
    std::strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    std::string dir = dirname(buf);

    if (kq_ >= 0)
        OpenDirectory(dir);
#endif

    files_.push_back(std::move(wf));
}

void rFileWatcher::Poll()
{
    if (!callback_ || files_.empty()) return;

#if FILEWATCHER_KQUEUE
    if (kq_ < 0 || dirFd_ < 0) return;

    // Non-blocking check: any directory-level events pending?
    struct kevent ev{};
    struct timespec zero{0, 0};
    int n = kevent(kq_, nullptr, 0, &ev, 1, &zero);
    if (n <= 0) return;

    // The directory changed: scan all watched files for mtime updates.
    for (auto& wf : files_)
    {
        struct stat st{};
        if (stat(wf.path.c_str(), &st) != 0) continue;
        if (st.st_mtime > wf.mtime)
        {
            wf.mtime = st.st_mtime;
            callback_(wf.tag);
        }
    }
#endif
    // On iOS, Android, and other platforms: no-op.
}

#endif // DEDICATED
