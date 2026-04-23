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

#include "uInputQueue.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include <iostream>

#ifndef DEDICATED
#include "rSDL.h"
#include "uEventSDL.h"
#endif

#include  "tRecorder.h"

#include  "uMenu.h"

static su_TimerCallback *timer=NULL;

su_TimerCallback::su_TimerCallback(){
    timer = this;
}

su_TimerCallback::~su_TimerCallback(){
    if (timer == this)
        timer = NULL;
}

static inline REAL Time(){
    if (timer)
        return timer->GetTime();
    else
        return 0;
}

bool su_prefetchInput=false;
bool su_contInput=true;

#define MAX_PENDING_INPUT 100

static REAL times[MAX_PENDING_INPUT];
static SDL_Event tEvents[MAX_PENDING_INPUT];

static int   currentIn=0,current_out=0,next_in=1;


static inline void increase(int &i){
    i++;
    if (i>=MAX_PENDING_INPUT)
        i=0;
}


static bool input_get=false;

void su_FetchAndStoreSDLInput()
{
#ifndef DEDICATED
#ifndef WIN32
#ifndef MACOSX
    if (!tRecorder::IsRunning() )
        SDL_PumpEvents();
#endif
#endif
#endif
}


bool su_StoreSDLEvent(const SDL_Event &tEvent){
    if (next_in!=current_out && !input_get){
        //con << "Extra input!\n";
        tEvents[currentIn]=tEvent;
        times[currentIn]=Time();
        increase(currentIn);
        next_in=currentIn;
        increase(next_in);
        return false;
    }
    return true;
}

#ifndef DEDICATED
// read and write operators for keysyms
// SDL3: SDL_Keycode is now the main key type
tRECORDING_ENUM( SDL_Scancode );
tRECORDING_ENUM( SDL_Keymod );
tRECORDING_ENUM( SDL_Keycode );
#endif

static char const * recordingSection = "INPUT";

//! Read or write event data
template< class Archiver > class EventArchiver
{
public:
#ifndef DEDICATED
    // SDL3: Keyboard event structure changed - key.state → key.down, key.keysym.* → key.*
    static void ArchiveKey( Archiver & archive, SDL_KeyboardEvent & key )
    {
        // SDL3: Archive as uint8_t for backwards compatibility
        Uint8 down = key.down ? 1 : 0;
        archive.Archive(down).Archive(key.scancode).Archive(key.key).Archive(key.mod);
        key.down = (down != 0);
    }

    // SDL3: Text input archiving helper.
    // In SDL3, event.text.text is a const char* managed by SDL, not a fixed buffer.
    // During playback, this pointer is NULL, so we need to archive a length prefix
    // to know how many characters to read.
    static void ArchiveTextInput( Archiver & archive, SDL_TextInputEvent & textEvent );
#endif

    static bool Archive( SDL_Event & event, REAL & time, bool & ret )
    {
        // start archive block if archiving is active
        Archiver archive;
        if ( archive.Initialize( recordingSection ) )
        {
#ifndef DEDICATED
            archive.Archive( ret );
            if ( !ret )
                return false;

            // write or read data
            archive.Archive(time).Archive(event.type);
            switch ( event.type )
            {
            // SDL3: Window events are now separate event types, archive window ID and data
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            {
                SDL_WindowEvent & window = event.window;
                archive.Archive(window.windowID).Archive(window.data1).Archive(window.data2);
            }
            break;
            // SDL3: SDL_KEYDOWN → SDL_EVENT_KEY_DOWN
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                SDL_KeyboardEvent & key = event.key;
                ArchiveKey( archive, key );
            }
            break;
            // SDL3: SDL_MOUSEMOTION → SDL_EVENT_MOUSE_MOTION
            case SDL_EVENT_MOUSE_MOTION:
            {
                SDL_MouseMotionEvent & motion = event.motion;
                // SDL3: motion.state now contains button state
                archive.Archive(motion.state).Archive(motion.x).Archive(motion.y).Archive(motion.xrel).Archive(motion.yrel);
            }
            break;
            // SDL3: SDL_MOUSEBUTTONDOWN → SDL_EVENT_MOUSE_BUTTON_DOWN
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                SDL_MouseButtonEvent & button = event.button;
                // SDL3: button.state → button.down (bool)
                Uint8 down = button.down ? 1 : 0;
                archive.Archive(button.button).Archive(down).Archive(button.x).Archive(button.y);
                button.down = (down != 0);
            }
            break;
            // SDL3: SDL_TEXTINPUT → SDL_EVENT_TEXT_INPUT
            // SDL3: event.text.text is now const char* instead of fixed array
            case SDL_EVENT_TEXT_INPUT:
            {
                // Text input archiving needs to handle recording vs playback differently
                // because event.text.text is const char* managed by SDL and is NULL during playback.
                // We archive a length byte first, then that many characters.
                ArchiveTextInput(archive, event.text);
            }
            break;
            // SDL3: Joystick events
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            {
                SDL_JoyAxisEvent & jaxis = event.jaxis;
                archive.Archive(jaxis.which).Archive(jaxis.axis).Archive(jaxis.value);
            }
            break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
            {
                SDL_JoyButtonEvent & jbutton = event.jbutton;
                Uint8 down = jbutton.down ? 1 : 0;
                archive.Archive(jbutton.which).Archive(jbutton.button).Archive(down);
                jbutton.down = (down != 0);
            }
            break;
            case SDL_EVENT_JOYSTICK_HAT_MOTION:
            {
                SDL_JoyHatEvent & jhat = event.jhat;
                archive.Archive(jhat.which).Archive(jhat.hat).Archive(jhat.value);
            }
            break;
            case SDL_EVENT_JOYSTICK_BALL_MOTION:
            {
                SDL_JoyBallEvent & jball = event.jball;
                archive.Archive(jball.which).Archive(jball.ball).Archive(jball.xrel).Archive(jball.yrel);
            }
            break;
            // SDL3: Quit event (no additional data to archive)
            case SDL_EVENT_QUIT:
            break;
            default:
                // do nothing
                break;
            }

#endif  // DEDICATED

            return true;
        }

        return false;
    }
};

#ifndef DEDICATED
//! Read or write event data
template<>
void EventArchiver< tRecordingBlock >::ArchiveKey( tRecordingBlock & archive, SDL_KeyboardEvent & orig )
{
    SDL_KeyboardEvent key = orig;
    if ( uInputScrambler::Scrambled() )
    {
        // SDL3: key.keysym.sym → key.key
        switch( key.key )
        {
        case SDLK_ESCAPE:
        case SDLK_SPACE:
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
            break;
        default:
            // SDL3: KMOD_NONE → SDL_KMOD_NONE, SDLK_x → SDLK_X
            key.mod = SDL_KMOD_NONE;
            key.key = SDLK_X;
            key.scancode = SDL_SCANCODE_UNKNOWN;
        }
    }

    // SDL3: Archive as uint8_t for backwards compatibility
    Uint8 down = key.down ? 1 : 0;
    archive.Archive(down).Archive(key.scancode).Archive(key.key).Archive(key.mod);
}

// SDL3: Text input archiving - recording specialization
// Writes length prefix followed by text characters
template<>
void EventArchiver< tRecordingBlock >::ArchiveTextInput( tRecordingBlock & archive, SDL_TextInputEvent & textEvent )
{
    const char * text = textEvent.text;
    Uint8 len = 0;
    if (text)
    {
        size_t textLen = strlen(text);
        len = (textLen > 32) ? 32 : static_cast<Uint8>(textLen);
    }
    archive.Archive(len);
    for (Uint8 i = 0; i < len; ++i)
    {
        char c = text[i];
        archive.Archive(c);
    }
}

// SDL3: Text input archiving - playback specialization
// Reads length prefix, then reads that many characters (discards them since
// SDL3's text.text is a const pointer we can't populate)
template<>
void EventArchiver< tPlaybackBlock >::ArchiveTextInput( tPlaybackBlock & archive, SDL_TextInputEvent & textEvent )
{
    (void)textEvent;  // SDL3: We can't populate textEvent.text (it's const char* managed by SDL)

    Uint8 len = 0;
    archive.Archive(len);
    for (Uint8 i = 0; i < len; ++i)
    {
        char c;
        archive.Archive(c);
        // Character is read and discarded - we can't store it in SDL3's const char* text field
    }
}
#endif

static const char * su_end = "END";
static const char * su_endInput = "ENDINPUT";

// flag indicating input was made and an input start marker is needed for the next input loop
static bool su_markerRequired = false;

void su_EndGetSDLInput()
{
    if ( su_markerRequired )
    {
        // record end of input fetching
        tRecorder::Playback(su_endInput);
        tRecorder::Record(su_endInput);
        su_markerRequired = false;
    }
}

uInputProcessGuard::uInputProcessGuard()
{}
uInputProcessGuard::~uInputProcessGuard()
{
    su_EndGetSDLInput();
}

int uInputScrambler::scrambled_ = 0;

uInputScrambler::uInputScrambler()
{
    scrambled_ ++;
}

uInputScrambler::~uInputScrambler()
{
    --scrambled_;
}

bool uInputScrambler::Scrambled()
{
    return scrambled_ > 0;
}

bool su_GetSDLInput(SDL_Event &tEvent,REAL &time){
    bool ret=false;

    // clear out data
    memset( &tEvent, 0, sizeof( SDL_Event ) );

    // find end of recording in playback
    if ( tRecorder::Playback(su_end) )
    {
        tRecorder::Record(su_end);
        uMenu::quickexit=uMenu::QuickExit_Total;
    }

    // try to fetch event from playback
    if ( !EventArchiver< tPlaybackBlock >::Archive( tEvent, time, ret ) )
    {
        // get real event
        sr_LockSDL();
        input_get=true;
        if (current_out!=currentIn){
            time=times[current_out];
            tEvent=tEvents[current_out];
            increase(current_out);
            ret=true;
        }
        else{
            time=Time();
            ret=
#ifndef DEDICATED
                SDL_PollEvent(&tEvent);
#else
                false;
#endif
        }
        sr_UnlockSDL();
        input_get=false;
    }

    su_markerRequired |= ret;

    // store event in recording
    if ( ret )
        EventArchiver< tRecordingBlock >::Archive( tEvent, time, ret );

    // SDL3: Removed SDL1-specific bogus event filtering (no longer needed)

    return ret;
}

/*
int su_InputThread(void *){
    while (su_contInput){
        if (sr_screen && su_prefetchInput){
            sr_LockSDL();
#ifndef DEDICATED
            SDL_PumpEvents();
#endif
            sr_UnlockSDL();
        }
#ifndef WIN32
        usleep(100000);
#endif
    }
    return 0;
}
*/

bool su_GetInput(uEvent &event, REAL &time)
{
#ifndef DEDICATED
    SDL_Event sdlEvent;
    if (su_GetSDLInput(sdlEvent, time))
    {
        // Convert SDL_Event to uEvent
        // Recording/playback happens in su_GetSDLInput, so we maintain compatibility
        uEventSDL::ToUEvent(sdlEvent, event);
        return true;
    }
#else
    (void)time; // Suppress unused warning
#endif
    event = uEvent(); // Reset to None type
    return false;
}


