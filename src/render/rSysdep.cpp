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

#include "defs.h"

#include "rRender.h"

#ifndef DEDICATED
#include "rSDL.h"
#endif

#include "rSysdep.h"
#include "tInitExit.h"
#include "tDirectories.h"
#include "tSysTime.h"
#include "rConsole.h"
#include "aa_config.h"
#include <iostream>
#include "rScreen.h"
#include "rTexture.h"
#include "tCommandLine.h"
#include "tConfiguration.h"
#include "tRecorder.h"
#include "tBackgroundProcess.h"
#ifndef DEDICATED
#include "rRenderQueue.h"
#include "rRenderBucket.h"
#include "rVertex.h"
#include "tLuaState.h"
#endif
#include <memory>
#include <vector>

#ifndef DEDICATED
// SDL3: Include via rSDL.h for proper header path
#include "rSDL.h"

// Use stb_image_write for PNG screenshots (replaces libpng)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <unistd.h>
#define SCREENSHOT_BYTES_PER_PIXEL 3

// SDL3: SDL_OPENGL always defined

#ifndef DEDICATED
// Modern fence sync using renderer abstraction (replaces legacy NV/APPLE fence extensions)
class rFence
{
public:
    static bool Available()
    {
        return true;  // GL 3.2+ always has fence sync
    }

    void Set()
    {
        Delete();
        fence_ = RenderCreateFence();
    }

    void Finish()
    {
        if (fence_)
        {
            RenderWaitFence(fence_);
        }
        Delete();
    }

    void Delete()
    {
        if (fence_)
        {
            RenderDeleteFence(fence_);
            fence_ = nullptr;
        }
    }

    ~rFence()
    {
        Delete();
    }
private:
    void* fence_ = nullptr;
};

// returns the fence to be used for syncing the GPU and CPU
static rFence & sr_GetFence()
{
    static rFence fence;
    return fence;
}

#endif // DEDICATED

bool sr_screenshotIsPlanned=false;
tString sr_screenshotName("screenshot");
static bool   s_videoout    =false;
static int    s_videooutDest=fileno(stdout);

static bool png_screenshot=true;
static tConfItem<bool> pns("PNG_SCREENSHOT",png_screenshot);

// Minimum seconds between on_time() Lua calls (default 1 s). Set to 0 to
// fire every frame (equivalent to on_frame). Configurable at runtime via
// aa_config_set("LUA_TIME_INTERVAL", "0.5") from Lua or config files.
static float s_luaTimeInterval = 1.0f;
static tConfItem<float> sr_luaTimeInterval("LUA_TIME_INTERVAL", s_luaTimeInterval);
static double s_lastLuaTimeHook = -1.0;

#ifndef DEDICATED

// Async screenshot saving - runs on background thread
static void SaveScreenshotAsync(std::shared_ptr<std::vector<unsigned char>> pixels,
                                 int width, int height, int channels,
                                 tString baseName, bool usePng)
{
    // Find unused filename (done in background to avoid blocking)
    int number = 0;
    bool done = false;
    while (!done)
    {
        tString fileName(baseName);
        if (number)
        {
            fileName << '_' << number;
        }
        if (usePng)
            fileName << ".png";
        else
            fileName << ".bmp";

        // Test if file exists
        std::ifstream s;
        if (tDirectories::Screenshot().Open(s, fileName))
        {
            number++;
            continue;
        }

        // Flip image vertically (OpenGL gives bottom-to-top, PNG expects top-to-bottom)
        std::vector<unsigned char> flipped(width * height * channels);
        for (int y = 0; y < height; y++)
        {
            memcpy(flipped.data() + y * width * channels,
                   pixels->data() + (height - 1 - y) * width * channels,
                   width * channels);
        }

        // Write file
        tString fullPath = tDirectories::Screenshot().GetWritePath(fileName);
        if (usePng)
        {
            stbi_write_png(fullPath.c_str(), width, height, channels,
                           flipped.data(), width * channels);
        }
        else
        {
            // For BMP, create temporary SDL surface
            SDL_Surface* temp = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGB24);
            if (temp)
            {
                memcpy(temp->pixels, flipped.data(), width * height * channels);
                SDL_SaveBMP(temp, fullPath);
                SDL_DestroySurface(temp);
            }
        }
        done = true;
    }
}

static void make_screenshot(){
    // Read pixels from OpenGL (must be on main thread)
    int width = sr_screenWidth;
    int height = sr_screenHeight;
    int channels = SCREENSHOT_BYTES_PER_PIXEL;

    // Allocate pixel buffer
    auto pixels = std::make_shared<std::vector<unsigned char>>(width * height * channels);

    RenderReadPixels(0, 0, width, height, rGLConst::RGB, rGLConst::UnsignedByte, pixels->data());

    // Video output: must be synchronous (streaming)
    if (s_videoout)
    {
        // Flip and write synchronously for video streaming
        for (int y = 0; y < height; y++)
        {
            // Write rows from bottom to top (flipped)
            Ignore(write(s_videooutDest,
                         pixels->data() + (height - 1 - y) * width * channels,
                         width * channels));
        }
    }

    // Screenshot saving: async
    if (sr_screenshotIsPlanned)
    {
        tString baseName = sr_screenshotName;
        bool usePng = png_screenshot;

        // Schedule background save
        tLambdaRunner::ScheduleBackground([pixels, width, height, channels, baseName, usePng]() {
            SaveScreenshotAsync(pixels, width, height, channels, baseName, usePng);
        });
    }
}
#endif

class PerformanceCounter
{
public:
    PerformanceCounter(): count_(0){
        start_ = tRealSysTimeFloat();
    }
    unsigned int Count(){
        return count_++;
    }
    ~PerformanceCounter()
    {
        double time = tRealSysTimeFloat()-start_;
        std::stringstream s;
        s << count_ << " frames in " << time << " seconds: " << count_ / time << " fps.\n";
#ifdef WIN32
        MessageBox (NULL, s.str().c_str() , "Performance", MB_OK);
#else
        std::cout << s.str();
#endif
    }
private:
    unsigned int count_;
    double start_;
};

static double s_nextFastForwardFrameRecorded=0; // the next frame to render in recorded time
static double s_nextFastForwardFrameReal=0;     // the next frame to render in real time
#endif // DEDICATED

// settings for fast forward mode
static REAL sr_FF_Maxstep=1; // maximum step between rendered frames
static tSettingItem<REAL> c_ff( "FAST_FORWARD_MAXSTEP",
                                sr_FF_Maxstep );

static REAL sr_FF_MaxstepReal=.05; // maximum step in real time between rendered frames
static tSettingItem<REAL> c_ffre( "FAST_FORWARD_MAXSTEP_REAL",
                                  sr_FF_MaxstepReal );

static REAL sr_FF_MaxstepRel=1; // maximum step between rendered frames relative to end of FF mode
static tSettingItem<REAL> c_ffr( "FAST_FORWARD_MAXSTEP_REL",
                                 sr_FF_MaxstepRel );


static double s_fastForwardTo=0;
static bool   s_fastForward =false;
static bool   s_benchmark   =false;

class rFastForwardCommandLineAnalyzer: public tCommandLineAnalyzer
{
private:
    bool DoAnalyze( tCommandLineParser & parser, int pass ) override
    {
        if(pass > 0)
            return false;

        // get option
        tString forward;
        if ( parser.GetOption( forward, "--fastforward" ) )
        {
            // set fast forward mode
            s_fastForward = true;

            // read time
            std::stringstream str(static_cast< char const * >( forward ) );
            str >> s_fastForwardTo;

            return true;
        }

        if ( parser.GetSwitch( "--benchmark" ) )
        {
            // set benchmark mode
            s_benchmark = true;
            return true;
        }

#ifndef DEDICATED
        if ( parser.GetSwitch( "--videoout" ) )
        {
            // redirect all regular output to stderr
            if ((s_videooutDest = dup(fileno(stdout))) == -1)
                std::cout << "Warning: Failed to duplicate stdout descriptor for video\n";
            else {
                if (-1 == dup2(fileno(stderr), fileno(stdout)))
                    std::cout << "Warning: Failed to redirect default output to stderr\n";
                else
                    std::cout << "Video Output: normal output redirected to stderr\n";
            }
            // set video out mode
            s_videoout = true;
            return true;
        }
#endif

        return false;
    }

    void DoHelp( std::ostream & s ) override
    {                                      //
        s << "--fastforward <time>         : lets time run very fast until the given time is reached\n";
        s << "--benchmark                  : renders frames as they were recorded\n";
#ifndef DEDICATED
        s << "--videoout                   : writes a raw video stream of frames to stdout\n";
#endif
    }
};

static rFastForwardCommandLineAnalyzer analyzer;

rSysDep::rSwapOptimize rSysDep::swapOptimize_ = rSysDep::rSwap_Auto;

// random benchmarks. Median of three runs.
// rSwap_Latency          : 382.841
// rSwap_Throughput       : 405.552
// rSwap_ThrougputFlush   : 405.194
// rSwap_ThrougputFastest : 406.676
// same recording, old client:
// rSwap_glFinish         : 395.892
// rSwap_LateFinish       : 398.132
// rSwap_Fence            : 400.248
// rSwap_glFlush          : 398.037
// rSwap_Fastest          : 396.392

#ifndef DEDICATED

// determines minimum of past values
class rRollingMinimum
{
public:
    rRollingMinimum( int level, REAL value )
    :levels_( level )
    {
        for( int i = level-1; i >= 0; --i )
        {
            levels_[i].Add(value);
            levels_[i].Add(value);
        }

        min_ = value;
    }

    // returns the current minimum
    REAL GetMin() const
    {
        return min_;
    }

    // returns sublevel minimum
    REAL GetSubMin(int level) const
    {
        return levels_[level].GetMin();
    }

    int Size()
    {
        return 1 << levels_.Len();
    }

    // adds a value to the list to find a minimum of
    void Add( REAL value )
    {
        if( min_ > value )
        {
            min_ = value;
        }

        int level = levels_.Len()-1;
        while( level >= 0 && levels_[level].Add(value) )
        {
            value = levels_[level].GetMin();
            level--;
        }

        // trown out old data? Recalculate min. Actually, we just
        // can copy it.
        if( level <= 0 )
        {
            min_ = levels_[0].GetMin();
        }
    }
private:
    struct Level
    {
        REAL value[2];
        bool toggle;

        Level( REAL v=0 )
        : toggle(true)
        {
            value[0] = value[1] = v;
        }

        // adds a value, returns true on every second call
        bool Add( REAL v )
        {
            value[0] = value[1];
            value[1] = v;

            toggle = !toggle;
            return toggle;
        }

        // returns the current minimum
        REAL GetMin() const
        {
            return value[0] < value[1] ? value[0] : value[1];
        }
    };

    tArray<Level> levels_;
    REAL min_;
};

#ifdef DEBUG
// #define DEBUG_SWAP
#endif

#ifdef DEBUG_SWAP
#include "tRandom.h"
#endif

#define SWAP_TIMESCALE_LOG 8
// #define SWAP_TIMESCALE_LOG 3
#define SWAP_TIMESCALE (1 << SWAP_TIMESCALE_LOG)

/* LATENCY order:
input
simulate
render
swap
sync
delay

THROUGHPUT order:
input
simulate
render
sync
swap
*/

// flag indicating that the next call to glClear needs execution.
// sometimes, we preemptively call it after swaps.
static bool sr_needClear = true;

static REAL sr_swapDelayFactor = .5f;


// measures time wasted on waiting for swaps
class rSwapTime
{
public:
    rSwapTime()
    : frameTimes_(SWAP_TIMESCALE_LOG+6, 1/20.0)
    , frameTimesMax_(4, -1/20.0)
    , waitTimes_(SWAP_TIMESCALE_LOG+1, 0)
    , delay_(0)
    , smoothDelay_(0)
    , badFrame_( -200 )
    , counter_ ( 100 )
    , inGame_( false )
    , lowFrameTime_( 0.001 )
    {
        lastTime_ = Time();

        // clamp delay factor
        if( sr_swapDelayFactor < .2 )
        {
            sr_swapDelayFactor = .2;
        }
        if( sr_swapDelayFactor > .95 )
        {
            sr_swapDelayFactor = .95;
        }
    }

    static double Time()
    {
#ifdef DEBUG_SWAP
        tAdvanceFrame();
        return tSysTimeFloat();
#else
        return tRealSysTimeFloat();
#endif
    }

    // return the delay for next frame
    REAL GetDelay() const
    {
        return delay_;
    }

    // call while in game
    void IsInGame()
    {
        inGame_ = true;
    }

    rSysDep::rSwapOptimize GetCurrentSwapOptimizeMode() const
    {
        switch( rSysDep::swapOptimize_ )
        {
        case rSysDep::rSwap_Auto:
            // no auto-low-latency unless vsync is explicitly on; rSwap_Throughput is a good enough safe default.
            if (currentScreensetting.vSync != ArmageTron_VSync_On)
                return rSysDep::rSwap_Throughput;

            // check for ridiculously high framerate
            if (frameTimesMax_.GetMin() > -lowFrameTime_)
            {
                lowFrameTime_ = 1/200.0;

                return rSysDep::rSwap_Throughput;
            }
            else
            {
                lowFrameTime_ = 1/400.0;
            }

            return badFrame_ > 0 ? rSysDep::rSwap_Throughput : rSysDep::rSwap_Latency;
            break;
        default:
            return rSysDep::swapOptimize_;
            break;
        }
    }

    void Swap( bool reallyDoIt ) const
    {
        // do the actual buffer swap.
        if( reallyDoIt )
        {
            if (renderer)
                renderer->SwapBuffers();

#ifdef DEBUG_SWAP_X
            {
                static int roughStart = SWAP_TIMESCALE*5;
                if( roughStart-- > 0 && (roughStart % 10) == 0 )
                {
                    tDelay( 1000 * 20 );
                }
                else if( roughStart == -1 )
                {
                    st_Breakpoint();
                }
            }
#endif

            sr_GetDrawableSize();
        }
    }

    // Clears the buffer in advance of the main code requesting it later
    void AdvanceClear()
    {
        sr_needClear = true;
        rSysDep::ClearGL();
        sr_needClear = false;
    }

    // swaps for minimum latency mode, returning the time needed to wait for the GPU
    REAL LatencySwap(bool swap)
    {
        // flush
        if( delay_ > smallDelay/10 )
        {
            // another extra finish if we're planning to to add delays.
            // we only want to measure the time spent waiting for vsync,
            // not the time waiting for rendering to finish.
            RenderFinish();
        }

        Swap(swap);

        // flush
        double start = Time();
        RenderFinish();
        REAL ret = Time() - start;

        AdvanceClear();

        return ret;
    }

    // swaps for maximum througput mode, returning the time needed to wait for the GPU
    REAL ThroughputSwap(bool swap)
    {
        REAL ret;

        if( rFence::Available() )
        {
            // oddly enough, on NVidia cards, the Swap() already
            // eats up the idle time; for the return value to
            // properly represent time waiting for stuff to finish,
            // we need to include it in the measurements.
            double start = Time();

            Swap(swap);

            static rFence & fence = sr_GetFence();

            // finish last frame's fence
            fence.Finish();

            ret = Time() - start;

            // set new fence
            fence.Set();

            AdvanceClear();
        }
        else
        {
            double start = Time();

            // flush
            RenderFinish();
            ret = Time() - start;

            Swap(swap);

            AdvanceClear();
        }

#ifdef DEBUG_SWAP_X
        static int count = 0;
        if( count-- < 0 )
        {
            count = 60;
            con << "SwapTime " << ret*1000 << "\n";
        }
#endif

        return ret;
    }

    // call after swapping buffers with an argument of true
    // and once just before rendering with an argument
    void Finish( bool swap = false )
    {
        switch( rSysDep::swapOptimize_ )
        {
        case rSysDep::rSwap_ThroughputFastest:
            Swap(swap);
            AdvanceClear();
            break;
        case rSysDep::rSwap_ThroughputFlush:
            Swap(swap);
            RenderFlush();
            AdvanceClear();
            break;
        case rSysDep::rSwap_Throughput:
            ThroughputSwap(swap);
            break;
        case rSysDep::rSwap_Auto:
            // some special cases. In vsync off situations, don't optimize
            // for latency. The high framerate already takes care of that.
            switch( currentScreensetting.vSync )
            {
            case ArmageTron_VSync_Off:
            case ArmageTron_VSync_MotionBlur:
                ThroughputSwap(swap);
                return;
            default:
                break;
            }
	    // falltrhough intentional
        default:
            FinishComplicated( swap );
        }
    }

    // call after swapping buffers with an argument of true
    // and once just before rendering with an argument
    void FinishComplicated( bool swap = false )
    {
#ifdef DEBUG_SWAP_X
        {
            static tRandomizer randomDelay;
            static int counter = -1000;
            if( counter++ > 0 )
            {
                tDelay( 1+randomDelay.Get( counter ) );
            }
        }
#endif

        rSysDep::rSwapOptimize opt = GetCurrentSwapOptimizeMode();

        // Low latency mode adds delays before polling input to avoid delays waiting for vsync later.
        // Naturally, that is nonsense if vsync is off. Switch to the lowest latency throughput mode instead.
        if(opt == rSysDep::rSwap_Latency && currentScreensetting.vSync >= ArmageTron_VSync_Off)
            opt = rSysDep::rSwap_Throughput;

        REAL neededToWait = ( opt == rSysDep::rSwap_Throughput ) ? ThroughputSwap(swap) : LatencySwap(swap);
        StopSwap( neededToWait, opt );

        // delay
        if( opt == rSysDep::rSwap_Latency && delay_ > smallDelay )
        {
            tDelay( int(delay_ * 1000 * 1000) );
        }
    }
protected:
    // one frame every so many seconds is tolarated

    // call after swapping
    void StopSwap( REAL timeSpentWaiting, rSysDep::rSwapOptimize opt )
    {
        double now = Time();
        REAL timeSpent = now - lastTime_;

        lastTime_ = now;
        frameTimesMax_.Add( -timeSpent );
        frameTimes_.Add( -frameTimesMax_.GetMin() );

        // check for unusually long frames
        REAL referenceFrameTime = 1/10.0;
        REAL minFrameTime = frameTimes_.GetMin();
        if( minFrameTime > referenceFrameTime )
        {
            minFrameTime = referenceFrameTime;
        }

        // tolerance factor for dropped frames
        static const REAL frameDropTolerance = 1.5;
        static const int framePenaltyMax = 1200;
        static const int framePenaltySingle = framePenaltyMax/10;
        static const int framePenaltyMin = -framePenaltySingle*3;

        // the real time spent waiting, not doing anything, during the last frame
        timeSpentWaiting += delay_;

        bool frameDrop = inGame_ &&
            (
                timeSpent > frameDropTolerance * minFrameTime
                ||
                timeSpentWaiting < smallDelay/10
                );
        inGame_ = false;

        if( frameDrop )
        {
            REAL waitTime = waitTimes_.GetMin();

            // forge the wait time so the next delays get lower.
            if( delay_ > smallDelay/10 )
            {
                timeSpentWaiting = waitTime * .8f;

                // and lower the delay factor for longterm reduction of the delay;
                // count rapid fire framedrops as less important, as well as isolated
                // drops.
                static double lastDrop = now-1;
                REAL weight = (now - lastDrop)/3.0f;
                weight = weight * exp(-3*weight/60.0f);
                lastDrop = now;
                if( weight > 1 )
                {
                    weight = 1;
                }
#ifdef DEBUG_SWAP
                con << "delayFactor " << sr_swapDelayFactor;
#endif

                sr_swapDelayFactor *= (1-delayFactorPenalty*weight);

#ifdef DEBUG_SWAP
                con << " -> " << sr_swapDelayFactor << "\n";
#endif
            }

#ifdef DEBUG_SWAP
            if( badFrame_ < framePenaltyMax/2 && opt == rSysDep::rSwap_Latency )
            {
                con << "Framedrop! " << timeSpent*1000 << " > " << frameTimes_.GetMin()*1000 << ", minWait=" << waitTime*1000 << " at " << counter_ << "\n";
            }
#endif

            if( badFrame_ < framePenaltyMax )
            {
                badFrame_ += framePenaltySingle;

                // hysteresis: if we are just disabling latency mode, go all the way and some
                if ( badFrame_ > 0 && badFrame_ <= framePenaltySingle )
                {
#ifdef DEBUG_SWAP
                    con << "Too many framedrops.\n";
#endif
                    badFrame_ = framePenaltyMax*2;
                }
            }
        }
        else
        {
            badFrame_--;

            if( badFrame_ == 0 )
            {
#ifdef DEBUG_SWAP
                con << "Rendering fine again.\n";
#endif
                badFrame_ = framePenaltyMin;

                // just dropping out of throughput mode; better still be a bit careful
                smoothDelay_ = delay_ = 0;
                counter_ = 0;
            }
            else if( badFrame_ < framePenaltyMin )
            {
                badFrame_ = framePenaltyMin;

#ifdef DEBUG_SWAP
                if ( counter_ == 20 )
                {
                    con << "Rendering OK:" << timeSpent*1000 << " < " << frameTimes_.GetMin()*frameDropTolerance*1000 << ", delay " << delay_*1000 << "\n";
                }
#endif
            }
        }

        waitTimes_.Add( timeSpentWaiting );

        // calculate optimal delay
        REAL newDelay = waitTimes_.GetMin() * sr_swapDelayFactor;

        // clear artificial delay if the current swap mode says so
        if( opt == rSysDep::rSwap_Latency )
        {
            // let delay factor recover so that there will be about at
            // most one dropped frame every so many
            REAL recovery=2*timeSpent/60.0f;
            sr_swapDelayFactor = 1 - (1-sr_swapDelayFactor)*(1-(1-sr_swapDelayFactor)*delayFactorPenalty*recovery);
        }
        else
        {
            delay_ = newDelay = 0;
        }


        // smooth out delay changes. Lowering is immediate
        if( newDelay < smoothDelay_ )
        {
            smoothDelay_ = newDelay;
        }
        else
        {
            // while increases are smoothed with a sub-second timescale.
            REAL w = timeSpent * 3;
            smoothDelay_ = ( smoothDelay_ + newDelay * w )/(1 + w );
        }
        delay_ = smoothDelay_;

        // clear artificial delay if not in latency mode
        if( opt != rSysDep::rSwap_Latency )
        {
            delay_ = 0;
        }

        // every so many frames, do a test frame with lower delay
        if( counter_-- <= 0 )
        {
            // con << "d=" << delay_ << ", bf=" << badFrame_ << "\n";

            counter_ = waitTimes_.Size();

            // this reduction is enough to get rendering stuck at 30 FPS back
            // to 60 FPS, if that's at all possible.
            delay_ *= .4;
        }
    }
private:
    // minimum total frame time
    rRollingMinimum frameTimes_;

    // maximum short-time frame time (contains negative frametimes so rRollingMinimum
    // can be abused to get the maximum frame time)
    rRollingMinimum frameTimesMax_;

    // minimum wait time per frame
    rRollingMinimum waitTimes_;

    // time sync is started
    // double startTime_;

    // last time swap was complete
    double lastTime_;

    // current pre-simulation delay
    REAL delay_;

    // smoothed pre-simulation delay
    REAL smoothDelay_;

    // if a frame dropped, this is set to a finite value and counted down
    int badFrame_;

    // random frame counter
    int counter_;

    // flag indicating whether we're in a game
    bool inGame_;

    // ridiculously low frametime limit
    mutable REAL lowFrameTime_;

    // a frame swap delay that is considered small
    static const REAL smallDelay;

    // reduction in the delay factor whenever a frame is dropped
    static const REAL delayFactorPenalty;
};

const REAL rSwapTime::smallDelay = 1E-3f;
const REAL rSwapTime::delayFactorPenalty = .05;

static rSwapTime & sr_SwapTime()
{
    static rSwapTime ret;
    return ret;
}

#endif // DEDICATED

// buffer swap:
#ifndef DEDICATED
// for setting breakpoints in optimized mode, too
static void breakpoint(){}

static bool sr_netSyncThreadGoOn = true;
static rSysDep::rNetIdler * sr_netIdler = NULL;
int sr_NetSyncThread(void *lockVoid)
{
    // SDL3: SDL_mutex → SDL_Mutex, SDL_mutexP → SDL_LockMutex, SDL_mutexV → SDL_UnlockMutex
    SDL_Mutex *lock = (SDL_Mutex *)lockVoid;

    SDL_LockMutex(lock);

    while ( sr_netSyncThreadGoOn )
    {
        SDL_UnlockMutex(lock);
        // wait for network data
        bool toDo = sr_netIdler->Wait();
        SDL_LockMutex(lock);

        if ( toDo )
        {
            // disable rendering (during auto-scrolling of console, for example)
            bool glout = sr_glOut;
            sr_glOut = false;

            // new network data arrived, handle it
            sr_netIdler->Do();

            // enable rendering again
            sr_glOut = glout;
        }
    }

    SDL_UnlockMutex(lock);

    return 0;
}

static SDL_Thread * sr_netSyncThread = NULL;
static SDL_Mutex * sr_netLock = NULL;
void rSysDep::StartNetSyncThread( rNetIdler * idler )
{
    sr_netIdler = idler;

    // can't use thrading trouble while recording
    if ( tRecorder::IsRunning() )
        return;

    if ( sr_netSyncThread )
        return;

    // create lock
    if ( !sr_netLock )
        sr_netLock = SDL_CreateMutex();

    // start thread
    sr_netSyncThread = SDL_CreateThread( sr_NetSyncThread, "net_sync", sr_netLock );
    if ( !sr_netSyncThread )
        return;

    // lock mutex, the thread should only do work while the main thread is waiting for the refresh
    SDL_LockMutex( sr_netLock );
}

void rSysDep::StopNetSyncThread()
{
    // stop and delete thread
    if ( sr_netSyncThread )
    {
        SDL_UnlockMutex(  sr_netLock );
        sr_netSyncThreadGoOn = false;
        SDL_WaitThread( sr_netSyncThread, NULL );
        sr_netSyncThread = NULL;
        sr_netIdler = NULL;
    }

    // delete lock
    if ( sr_netLock )
    {
        SDL_DestroyMutex( sr_netLock );
        sr_netLock = NULL;
    }
}

int NextPowerOfTwo( int in )
{
    int x = 1;
    while ( x * 32 <= in )
        x <<= 5;
    while ( x < in )
        x <<= 1;

    return x;
}

// Motion blur was implemented on top of GL1/2-era FBOs via
// rTextureRenderTarget (accumulation by blending the previous frame back
// into the current one on a texture-render-target ping-pong). It has no
// Vulkan equivalent and the underlying GL scaffolding is gone. The kept
// symbol still owns the MOTION_BLUR_TIME config item (so saved user.cfg
// files don't start flagging unknown keys) and returns true so callers
// keep swapping normally.
static REAL sr_motionBlurTime = .0075;
static tSettingItem<REAL> c_mb( "MOTION_BLUR_TIME",
                                sr_motionBlurTime );

bool sr_MotionBlur( double /*time*/ )
{
    return true;
}

int sr_maxFPS = 0;
static tConfItem<int> sr_maxFPSConf("MAX_FPS", sr_maxFPS,
                                    [](const int& val) { return (val >= 0); });

void sr_LimitFPS()
{
    if (sr_maxFPS > 0 && !tRecorder::IsPlayingBack())
    {
        static double last_time = 0;

        const double now_time = tRealSysTimeFloat();
        const double SPF = 1.0 / sr_maxFPS;

        const double target_now_time = last_time + SPF;
        if (now_time < target_now_time)
        {
            SDL_Delay(round(1000 * (target_now_time - now_time)));
            last_time = target_now_time;
        }
        else
        {
            last_time = now_time;
        }
    }
}

void rSysDep::SwapGL(){
    if ( s_benchmark )
    {
        static PerformanceCounter counter;
        counter.Count();
    }

    double time = tSysTimeFloat();
    double realTime = tRealSysTimeFloat();

    bool next_glOut = sr_glOut;

    /* static double mytime = time; //ljr
    if (false && time < mytime + 1. / 29.97) {
        printf("skipping! %f %f\n", time, mytime);
        next_glOut = false;
    } else {
        printf("rendering %f %f\n", time, mytime);
        mytime = time;
        next_glOut = true;
    } */

    // adapt playback speed to recorded speed
    if ( !s_benchmark && !s_fastForward && tRecorder::IsPlayingBack() )
    {
        static double timeOffset=0;
        static double lastRendered=0;

        // calculate how much we're behind the rendering schedule
        double behind = - time + realTime + timeOffset;
        // std::cout << behind << " " << sr_glOut << "\n";

        // large delays can only be caused by breakpoints or map downloads; ignore them
        if ( behind > .5 || realTime > lastRendered + .2 )
        {
            timeOffset -= behind;
            next_glOut = true;
        }
        else
        {
            // we're a bit behind, skip the next frame
            if ( behind > .1 )
            {
                next_glOut = false;
            }
            else if ( sr_glOut )
            {
                lastRendered=realTime;
                // we're ahead, pause a bit
                if  ( behind < -.5 )
                    timeOffset -= behind;
                else if ( behind < -.1 )
                {
                    int delay = int( -( behind + .1 ) * 1000000 );
                    // std::cout << behind << ":" << delay << "\n";
                    tDelayForce( delay );
                }
            }
            else
            {
                // we're not behind any more. Reactivate rendering.
                next_glOut = true;
            }
        }

        if ( next_glOut )
            lastRendered=realTime;
    }

    if (!sr_glOut)
    {
        // display next frame in fast foward mode
        if ( ( s_fastForward && ( time > s_nextFastForwardFrameRecorded || realTime > s_nextFastForwardFrameReal ) ) || next_glOut )
        {
            sr_glOut = true;
            rSysDep::ClearGL();
        }

        // in playback or recording mode, always execute frame tasks, they may be improtant for consistency
        if ( tRecorder::IsRunning() ) {
            rPerFrameTask::DoPerFrameTasks();
        }


        return;
    }


    rPerFrameTask::DoPerFrameTasks();

    // Fire Lua render hooks if defined.
    if (tLuaState::Instance().IsAlive()) {
        lua_State* L = tLuaState::Instance().View().raw();

        // on_time(timestamp): fires at most every LUA_TIME_INTERVAL seconds
        // (default 1 s).  Safer than on_frame for work that doesn't need
        // per-frame granularity (stats, data polling, score updates, etc.).
        // timestamp is the game simulation time in seconds.
        double interval = (double)s_luaTimeInterval;
        if (s_lastLuaTimeHook < 0.0 || (time - s_lastLuaTimeHook) >= interval) {
            s_lastLuaTimeHook = time;
            lua_getglobal(L, "on_time");
            if (lua_isfunction(L, -1)) {
                lua_pushnumber(L, time);
                lua_pcall(L, 1, 0, 0);
            } else {
                lua_pop(L, 1);
            }
        }

        // on_frame(): fires every rendered frame. Use sparingly — heavy work
        // here costs per-frame CPU time. Prefer on_time for anything that
        // doesn't need sub-second granularity.
        lua_getglobal(L, "on_frame");
        if (lua_isfunction(L, -1))
            lua_pcall(L, 0, 0, 0);
        else
            lua_pop(L, 1);
    }

    // Reset viewport to fullscreen for any remaining global HUD elements
    // (console, text fields). Per-viewport cockpit HUD was already flushed
    // by display_cockpit_lucifer at each player's sub-viewport.
    RenderViewport(0, 0, sr_screenWidth, sr_screenHeight);
    rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);

    // unlock the mutex while waiting for the swap operation to finish
    // SDL_mutexV(  sr_netLock );
    // sr_LockSDL();

    if (sr_screenshotIsPlanned){
        make_screenshot();
        sr_screenshotIsPlanned=false;
    }
    else if (s_videoout)
        make_screenshot();

    // Motion blur is a legacy GL1/2 effect that no longer functions under
    // the Vulkan renderer (see sr_MotionBlur above). Always swap.
    (void)sr_MotionBlur( time );
    sr_SwapTime().Finish( true );

    // sr_UnlockSDL();
    // lock mutex again
    // SDL_mutexP(  sr_netLock );


    // disable output in fast forward mode
    if ( s_fastForward && tRecorder::IsPlayingBack() )
    {
        if ( time < s_fastForwardTo )
        {
            // next displayed frame should be ten percent closer to the target, but at most 10 seconds
            s_nextFastForwardFrameRecorded = ( s_fastForwardTo - time ) * sr_FF_MaxstepRel;
            if ( s_nextFastForwardFrameRecorded > sr_FF_Maxstep )
                s_nextFastForwardFrameRecorded = sr_FF_Maxstep ;
            s_nextFastForwardFrameRecorded += time;
            s_nextFastForwardFrameReal = realTime + sr_FF_MaxstepReal ;

            next_glOut = false;
        }
        else
        {
            std::cout << "End of fast forward mode.\n";
            st_Breakpoint();
            s_fastForward = false;
        }
    }

    //#ifdef DEBUG
    if ( !s_fastForward )
    {
        breakpoint();
    }
    //#endif

    sr_LimitFPS();

    sr_glOut = next_glOut;
}

#endif // dedicated

#ifndef DEDICATED
// SDL3: SDL_mutex → SDL_Mutex
static SDL_Mutex *mut;

static void stuff_init(){
    mut=SDL_CreateMutex();
}

static tInitExit stuff_ie(&stuff_init);
#endif

#ifndef DEDICATED
void sr_LockSDL(){
    //std::cerr << "locking...";
#ifndef WIN32
    //SDL_mutexP(mut);
#endif
    //std::cerr << " locked!\n";
}

void sr_UnlockSDL(){
    //std::cerr << "unlocking...";
#ifndef WIN32
    //SDL_mutexV(mut);
#endif
    //std::cerr << " unlocked!\n";
}
#endif // DEDICATED

#ifndef DEDICATED
void  rSysDep::ClearGL(){
    if (sr_glOut && sr_needClear )
    {
        RenderClearColor(0.0,0.0,0.0,1.0);
        RenderClear(true, true);
    }
    sr_needClear = true;
}

bool rSysDep::IsBenchmark()
{
    return s_benchmark;
}

 //!< call while game content is rendered
void rSysDep::IsInGame()
{
    sr_SwapTime().IsInGame();
}
#endif


