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

#ifndef ArmageTron_uEvent_H
#define ArmageTron_uEvent_H

#include <cstdint>

//! Platform-agnostic event types
enum class uEventType : uint8_t
{
    None = 0,

    // Keyboard events
    KeyDown,
    KeyUp,

    // Mouse events
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,

    // Touch events
    TouchBegin,
    TouchEnd,
    TouchMotion,

    // Joystick events
    JoyAxisMotion,
    JoyButtonDown,
    JoyButtonUp,
    JoyHatMotion,
    JoyBallMotion,

    // Window events
    WindowEvent,

    // Text input
    TextInput,

    // System events
    Quit
};

//! Key modifier flags (platform-agnostic)
enum uKeyMod : uint16_t
{
    UMOD_NONE   = 0x0000,
    UMOD_LSHIFT = 0x0001,
    UMOD_RSHIFT = 0x0002,
    UMOD_LCTRL  = 0x0040,
    UMOD_RCTRL  = 0x0080,
    UMOD_LALT   = 0x0100,
    UMOD_RALT   = 0x0200,
    UMOD_LGUI   = 0x0400,
    UMOD_RGUI   = 0x0800,
    UMOD_NUM    = 0x1000,
    UMOD_CAPS   = 0x2000,
    UMOD_MODE   = 0x4000,

    // Convenience masks
    UMOD_CTRL  = UMOD_LCTRL | UMOD_RCTRL,
    UMOD_SHIFT = UMOD_LSHIFT | UMOD_RSHIFT,
    UMOD_ALT   = UMOD_LALT | UMOD_RALT,
    UMOD_GUI   = UMOD_LGUI | UMOD_RGUI
};

//! Window event subtypes
enum class uWindowEventType : uint8_t
{
    None = 0,
    Shown,
    Hidden,
    Exposed,
    Moved,
    Resized,
    Minimized,
    Maximized,
    Restored,
    Enter,
    Leave,
    FocusGained,
    FocusLost,
    Close
};

//! Hat position flags
enum uHatPosition : uint8_t
{
    UHAT_CENTERED  = 0x00,
    UHAT_UP        = 0x01,
    UHAT_RIGHT     = 0x02,
    UHAT_DOWN      = 0x04,
    UHAT_LEFT      = 0x08,
    UHAT_RIGHTUP   = UHAT_RIGHT | UHAT_UP,
    UHAT_RIGHTDOWN = UHAT_RIGHT | UHAT_DOWN,
    UHAT_LEFTUP    = UHAT_LEFT | UHAT_UP,
    UHAT_LEFTDOWN  = UHAT_LEFT | UHAT_DOWN
};

//! Keyboard event data
struct uKeyEvent
{
    int32_t scancode;   //!< Physical key location
    int32_t keycode;    //!< Virtual key code (platform-mapped)
    uint16_t mod;       //!< Modifier keys (uKeyMod flags)
    uint8_t state;      //!< Key state: 1 = pressed, 0 = released
    uint8_t repeat;     //!< Non-zero if this is a key repeat
};

//! Mouse motion event data
struct uMouseMotionEvent
{
    int32_t x;          //!< Absolute X position
    int32_t y;          //!< Absolute Y position
    int32_t xrel;       //!< Relative X motion
    int32_t yrel;       //!< Relative Y motion
    uint32_t state;     //!< Button state mask
};

//! Mouse button event data
struct uMouseButtonEvent
{
    int32_t x;          //!< X position at click
    int32_t y;          //!< Y position at click
    uint8_t button;     //!< Button number (1=left, 2=middle, 3=right, etc.)
    uint8_t state;      //!< 1 = pressed, 0 = released
    uint8_t clicks;     //!< Click count (1=single, 2=double, etc.)
    uint8_t padding;
};

//! Touch/finger event data
struct uTouchEvent
{
    int64_t fingerId;   //!< Unique finger identifier
    float x;            //!< Normalized X position [0..1]
    float y;            //!< Normalized Y position [0..1]
    float dx;           //!< Normalized X motion delta
    float dy;           //!< Normalized Y motion delta
    float pressure;     //!< Pressure [0..1]
    uint32_t timestamp; //!< Event timestamp
};

//! Joystick axis event data
struct uJoyAxisEvent
{
    int32_t which;      //!< Joystick instance ID
    uint8_t axis;       //!< Axis index
    uint8_t padding[3];
    int16_t value;      //!< Axis value [-32768..32767]
    int16_t padding2;
};

//! Joystick button event data
struct uJoyButtonEvent
{
    int32_t which;      //!< Joystick instance ID
    uint8_t button;     //!< Button index
    uint8_t state;      //!< 1 = pressed, 0 = released
    uint8_t padding[2];
};

//! Joystick hat event data
struct uJoyHatEvent
{
    int32_t which;      //!< Joystick instance ID
    uint8_t hat;        //!< Hat index
    uint8_t value;      //!< Hat position (uHatPosition flags)
    uint8_t padding[2];
};

//! Joystick ball event data
struct uJoyBallEvent
{
    int32_t which;      //!< Joystick instance ID
    uint8_t ball;       //!< Ball index
    uint8_t padding[3];
    int16_t xrel;       //!< Relative X motion
    int16_t yrel;       //!< Relative Y motion
};

//! Window event data
struct uWindowEvent
{
    uWindowEventType event; //!< Window event subtype
    uint8_t padding[3];
    int32_t data1;      //!< Event-dependent data
    int32_t data2;      //!< Event-dependent data
};

//! Text input event data
struct uTextInputEvent
{
    char text[32];      //!< UTF-8 encoded text
};

//! Platform-agnostic input event
//! This structure represents all input events in a platform-independent manner.
//! It is designed to be converted from/to platform-specific event types.
struct uEvent
{
    uEventType type;    //!< Event type
    uint8_t padding[3];
    uint32_t timestamp; //!< Event timestamp in milliseconds

    union
    {
        uKeyEvent key;
        uMouseMotionEvent motion;
        uMouseButtonEvent button;
        uTouchEvent touch;
        uJoyAxisEvent jaxis;
        uJoyButtonEvent jbutton;
        uJoyHatEvent jhat;
        uJoyBallEvent jball;
        uWindowEvent window;
        uTextInputEvent text;
    };

    uEvent() : type(uEventType::None), timestamp(0) {}
};

#endif // ArmageTron_uEvent_H
