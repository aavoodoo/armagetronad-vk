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

#include "uEventSDL.h"
#include "rSDL.h"
#include <cstring>

uint16_t uEventSDL::ConvertKeyMod(uint16_t sdlMod)
{
    uint16_t mod = UMOD_NONE;

    if (sdlMod & SDL_KMOD_LSHIFT) mod |= UMOD_LSHIFT;
    if (sdlMod & SDL_KMOD_RSHIFT) mod |= UMOD_RSHIFT;
    if (sdlMod & SDL_KMOD_LCTRL)  mod |= UMOD_LCTRL;
    if (sdlMod & SDL_KMOD_RCTRL)  mod |= UMOD_RCTRL;
    if (sdlMod & SDL_KMOD_LALT)   mod |= UMOD_LALT;
    if (sdlMod & SDL_KMOD_RALT)   mod |= UMOD_RALT;
    if (sdlMod & SDL_KMOD_LGUI)   mod |= UMOD_LGUI;
    if (sdlMod & SDL_KMOD_RGUI)   mod |= UMOD_RGUI;
    if (sdlMod & SDL_KMOD_NUM)    mod |= UMOD_NUM;
    if (sdlMod & SDL_KMOD_CAPS)   mod |= UMOD_CAPS;
    if (sdlMod & SDL_KMOD_MODE)   mod |= UMOD_MODE;

    return mod;
}

uint16_t uEventSDL::ConvertKeyModToSDL(uint16_t uMod)
{
    uint16_t sdlMod = SDL_KMOD_NONE;

    if (uMod & UMOD_LSHIFT) sdlMod |= SDL_KMOD_LSHIFT;
    if (uMod & UMOD_RSHIFT) sdlMod |= SDL_KMOD_RSHIFT;
    if (uMod & UMOD_LCTRL)  sdlMod |= SDL_KMOD_LCTRL;
    if (uMod & UMOD_RCTRL)  sdlMod |= SDL_KMOD_RCTRL;
    if (uMod & UMOD_LALT)   sdlMod |= SDL_KMOD_LALT;
    if (uMod & UMOD_RALT)   sdlMod |= SDL_KMOD_RALT;
    if (uMod & UMOD_LGUI)   sdlMod |= SDL_KMOD_LGUI;
    if (uMod & UMOD_RGUI)   sdlMod |= SDL_KMOD_RGUI;
    if (uMod & UMOD_NUM)    sdlMod |= SDL_KMOD_NUM;
    if (uMod & UMOD_CAPS)   sdlMod |= SDL_KMOD_CAPS;
    if (uMod & UMOD_MODE)   sdlMod |= SDL_KMOD_MODE;

    return sdlMod;
}

bool uEventSDL::ToUEvent(const SDL_Event& sdlEvent, uEvent& outEvent)
{
    outEvent = uEvent();  // Use value initialization instead of memset

    switch (sdlEvent.type)
    {
    case SDL_EVENT_KEY_DOWN:
        outEvent.type = uEventType::KeyDown;
        // SDL3: timestamps are in nanoseconds
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.key.scancode = AA_KEY_SCANCODE(sdlEvent);
        outEvent.key.repeat = sdlEvent.key.repeat;
        outEvent.key.keycode = AA_KEY_SYM(sdlEvent);
        outEvent.key.mod = ConvertKeyMod(AA_KEY_MOD(sdlEvent));
        outEvent.key.state = AA_KEY_DOWN(sdlEvent) ? 1 : 0;
        return true;

    case SDL_EVENT_KEY_UP:
        outEvent.type = uEventType::KeyUp;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.key.scancode = AA_KEY_SCANCODE(sdlEvent);
        outEvent.key.repeat = 0;
        outEvent.key.keycode = AA_KEY_SYM(sdlEvent);
        outEvent.key.mod = ConvertKeyMod(AA_KEY_MOD(sdlEvent));
        outEvent.key.state = AA_KEY_DOWN(sdlEvent) ? 1 : 0;
        return true;

    case SDL_EVENT_MOUSE_MOTION:
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.type = uEventType::MouseMotion;
        outEvent.motion.x = static_cast<int>(sdlEvent.motion.x);
        outEvent.motion.y = static_cast<int>(sdlEvent.motion.y);
        outEvent.motion.xrel = static_cast<int>(sdlEvent.motion.xrel);
        outEvent.motion.yrel = static_cast<int>(sdlEvent.motion.yrel);
        outEvent.motion.state = sdlEvent.motion.state;
        return true;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        outEvent.type = uEventType::MouseButtonDown;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.button.clicks = sdlEvent.button.clicks;
        outEvent.button.button = sdlEvent.button.button;
        outEvent.button.state = sdlEvent.button.down ? 1 : 0;
        outEvent.button.x = sdlEvent.button.x;
        outEvent.button.y = sdlEvent.button.y;
        return true;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        outEvent.type = uEventType::MouseButtonUp;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.button.clicks = sdlEvent.button.clicks;
        outEvent.button.button = sdlEvent.button.button;
        outEvent.button.state = sdlEvent.button.down ? 1 : 0;
        outEvent.button.x = static_cast<int>(sdlEvent.button.x);
        outEvent.button.y = static_cast<int>(sdlEvent.button.y);
        return true;

    // SDL3: Touch events
    case SDL_EVENT_FINGER_DOWN:
        outEvent.type = uEventType::TouchBegin;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.touch.fingerId = sdlEvent.tfinger.fingerID;
        outEvent.touch.x = sdlEvent.tfinger.x;
        outEvent.touch.y = sdlEvent.tfinger.y;
        outEvent.touch.dx = sdlEvent.tfinger.dx;
        outEvent.touch.dy = sdlEvent.tfinger.dy;
        outEvent.touch.pressure = sdlEvent.tfinger.pressure;
        outEvent.touch.timestamp = AA_GetEventTimestampMs(sdlEvent);
        return true;

    case SDL_EVENT_FINGER_UP:
        outEvent.type = uEventType::TouchEnd;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.touch.fingerId = sdlEvent.tfinger.fingerID;
        outEvent.touch.x = sdlEvent.tfinger.x;
        outEvent.touch.y = sdlEvent.tfinger.y;
        outEvent.touch.dx = sdlEvent.tfinger.dx;
        outEvent.touch.dy = sdlEvent.tfinger.dy;
        outEvent.touch.pressure = sdlEvent.tfinger.pressure;
        outEvent.touch.timestamp = AA_GetEventTimestampMs(sdlEvent);
        return true;

    case SDL_EVENT_FINGER_MOTION:
        outEvent.type = uEventType::TouchMotion;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.touch.fingerId = sdlEvent.tfinger.fingerID;
        outEvent.touch.x = sdlEvent.tfinger.x;
        outEvent.touch.y = sdlEvent.tfinger.y;
        outEvent.touch.dx = sdlEvent.tfinger.dx;
        outEvent.touch.dy = sdlEvent.tfinger.dy;
        outEvent.touch.pressure = sdlEvent.tfinger.pressure;
        outEvent.touch.timestamp = AA_GetEventTimestampMs(sdlEvent);
        return true;

    // SDL3: Window events are now separate event types
    case SDL_EVENT_WINDOW_SHOWN:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.data1 = sdlEvent.window.data1;
        outEvent.window.data2 = sdlEvent.window.data2;
        outEvent.window.event = uWindowEventType::Shown;
        return true;

    case SDL_EVENT_WINDOW_HIDDEN:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Hidden;
        return true;

    case SDL_EVENT_WINDOW_EXPOSED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Exposed;
        return true;

    case SDL_EVENT_WINDOW_MOVED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.data1 = sdlEvent.window.data1;
        outEvent.window.data2 = sdlEvent.window.data2;
        outEvent.window.event = uWindowEventType::Moved;
        return true;

    case SDL_EVENT_WINDOW_RESIZED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.data1 = sdlEvent.window.data1;
        outEvent.window.data2 = sdlEvent.window.data2;
        outEvent.window.event = uWindowEventType::Resized;
        return true;

    case SDL_EVENT_WINDOW_MINIMIZED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Minimized;
        return true;

    case SDL_EVENT_WINDOW_MAXIMIZED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Maximized;
        return true;

    case SDL_EVENT_WINDOW_RESTORED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Restored;
        return true;

    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Enter;
        return true;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Leave;
        return true;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::FocusGained;
        return true;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::FocusLost;
        return true;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        outEvent.type = uEventType::WindowEvent;
        outEvent.timestamp = sdlEvent.window.timestamp / 1000000;
        outEvent.window.event = uWindowEventType::Close;
        return true;

    case SDL_EVENT_TEXT_INPUT:
        outEvent.type = uEventType::TextInput;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        strncpy(outEvent.text.text, sdlEvent.text.text, sizeof(outEvent.text.text) - 1);
        outEvent.text.text[sizeof(outEvent.text.text) - 1] = '\0';
        return true;

    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.type = uEventType::JoyAxisMotion;
        outEvent.jaxis.which = sdlEvent.jaxis.which;
        outEvent.jaxis.axis = sdlEvent.jaxis.axis;
        outEvent.jaxis.value = sdlEvent.jaxis.value;
        return true;

    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        outEvent.type = uEventType::JoyButtonDown;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.jbutton.which = sdlEvent.jbutton.which;
        outEvent.jbutton.button = sdlEvent.jbutton.button;
        outEvent.jbutton.state = sdlEvent.jbutton.down ? 1 : 0;
        return true;

    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        outEvent.type = uEventType::JoyButtonUp;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.jbutton.which = sdlEvent.jbutton.which;
        outEvent.jbutton.button = sdlEvent.jbutton.button;
        outEvent.jbutton.state = sdlEvent.jbutton.down ? 1 : 0;
        return true;

    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.type = uEventType::JoyHatMotion;
        outEvent.jhat.which = sdlEvent.jhat.which;
        outEvent.jhat.hat = sdlEvent.jhat.hat;
        outEvent.jhat.value = sdlEvent.jhat.value;
        return true;

    case SDL_EVENT_JOYSTICK_BALL_MOTION:
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        outEvent.type = uEventType::JoyBallMotion;
        outEvent.jball.which = sdlEvent.jball.which;
        outEvent.jball.ball = sdlEvent.jball.ball;
        outEvent.jball.xrel = sdlEvent.jball.xrel;
        outEvent.jball.yrel = sdlEvent.jball.yrel;
        return true;

    case SDL_EVENT_QUIT:
        outEvent.type = uEventType::Quit;
        outEvent.timestamp = AA_GetEventTimestampMs(sdlEvent);
        return true;

    default:
        // Unsupported event type
        outEvent.type = uEventType::None;
        return false;
    }
}

bool uEventSDL::ToSDLEvent(const uEvent& event, SDL_Event& outSDLEvent)
{
    memset(&outSDLEvent, 0, sizeof(SDL_Event));

    switch (event.type)
    {
    case uEventType::KeyDown:
        outSDLEvent.type = SDL_EVENT_KEY_DOWN;
        outSDLEvent.key.down = (event.key.state != 0);
        outSDLEvent.key.timestamp = static_cast<Uint64>(event.timestamp) * 1000000;
        outSDLEvent.key.scancode = static_cast<SDL_Scancode>(event.key.scancode);
        outSDLEvent.key.repeat = event.key.repeat;
        outSDLEvent.key.key = static_cast<SDL_Keycode>(event.key.keycode);
        outSDLEvent.key.mod = static_cast<SDL_Keymod>(ConvertKeyModToSDL(event.key.mod));
        return true;

    case uEventType::KeyUp:
        outSDLEvent.type = SDL_EVENT_KEY_UP;
        outSDLEvent.key.down = (event.key.state != 0);
        outSDLEvent.key.timestamp = static_cast<Uint64>(event.timestamp) * 1000000;
        outSDLEvent.key.scancode = static_cast<SDL_Scancode>(event.key.scancode);
        outSDLEvent.key.key = static_cast<SDL_Keycode>(event.key.keycode);
        outSDLEvent.key.mod = static_cast<SDL_Keymod>(ConvertKeyModToSDL(event.key.mod));
        return true;

    case uEventType::MouseMotion:
        outSDLEvent.type = SDL_EVENT_MOUSE_MOTION;
        outSDLEvent.motion.timestamp = static_cast<Uint64>(event.timestamp) * 1000000;
        outSDLEvent.motion.x = static_cast<float>(event.motion.x);
        outSDLEvent.motion.y = static_cast<float>(event.motion.y);
        outSDLEvent.motion.xrel = static_cast<float>(event.motion.xrel);
        outSDLEvent.motion.yrel = static_cast<float>(event.motion.yrel);
        outSDLEvent.motion.state = event.motion.state;
        return true;

    case uEventType::MouseButtonDown:
        outSDLEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        outSDLEvent.button.timestamp = static_cast<Uint64>(event.timestamp) * 1000000;
        outSDLEvent.button.clicks = event.button.clicks;
        outSDLEvent.button.button = event.button.button;
        outSDLEvent.button.down = (event.button.state != 0);
        outSDLEvent.button.x = static_cast<float>(event.button.x);
        outSDLEvent.button.y = static_cast<float>(event.button.y);
        return true;

    case uEventType::MouseButtonUp:
        outSDLEvent.type = SDL_EVENT_MOUSE_BUTTON_UP;
        outSDLEvent.button.timestamp = static_cast<Uint64>(event.timestamp) * 1000000;
        outSDLEvent.button.clicks = event.button.clicks;
        outSDLEvent.button.button = event.button.button;
        outSDLEvent.button.down = (event.button.state != 0);
        outSDLEvent.button.x = static_cast<float>(event.button.x);
        outSDLEvent.button.y = static_cast<float>(event.button.y);
        return true;

    case uEventType::Quit:
        outSDLEvent.type = SDL_EVENT_QUIT;
        outSDLEvent.quit.timestamp = static_cast<Uint64>(event.timestamp) * 1000000;
        return true;

    // Touch, joystick, and other events can be added as needed
    default:
        return false;
    }
}

#endif // DEDICATED
