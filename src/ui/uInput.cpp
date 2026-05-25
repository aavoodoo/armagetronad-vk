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

#include <stdarg.h>
#include "uInput.h"
#include "uEvent.h"
#ifndef DEDICATED
#include "uEventSDL.h"
#endif
#include "tMemManager.h"
#include "rScreen.h"
#include "tInitExit.h"
#include "tConfiguration.h"
#include "rConsole.h"
#include "uMenu.h"
#include "tSysTime.h"
#include "rViewport.h"

#include <vector>
#include <map>

// Touch devices support (touchscreen or touchpad)
// 0:disabled
// 1:tap zones (left=turn left, center=brake, right=turn right)
// 2:swipe gestures
// 3:cockpit widget buttons
static int su_enableTouch = 0;
static tSettingItem< int > su_enableTouchConf( "ENABLE_TOUCH", su_enableTouch );

void su_EnableTouchDefault() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (su_enableTouch == 0)
        su_enableTouch = 3;   // default to cockpit buttons on mobile
    // Disable control-key tooltips on touchscreen devices — the on-screen
    // buttons make keyboard hints pointless and they clutter the display.
    // Level below Level_Essential (0) suppresses all tooltips.
    extern uActionTooltip::Level su_helpLevel;
    su_helpLevel = static_cast<uActionTooltip::Level>(-1);
#endif
}

int su_GetEnableTouch() { return su_enableTouch; }
extern "C" void su_SetEnableTouch(int mode) { su_enableTouch = mode; }

// Per-player touch mode for split-screen (0 = inherit global su_enableTouch).
static int su_playerTouchMode[MAX_VIEWPORTS] = {};

int su_GetEnableTouchForPlayer(int playerIdx) {
    if (playerIdx < 0 || playerIdx >= MAX_VIEWPORTS) return su_enableTouch;
    int m = su_playerTouchMode[playerIdx];
    return m > 0 ? m : su_enableTouch;
}
void su_SetEnableTouchForPlayer(int playerIdx, int mode) {
    if (playerIdx >= 0 && playerIdx < MAX_VIEWPORTS)
        su_playerTouchMode[playerIdx] = mode;
}

// Returns true if any player's effective touch mode equals `mode`.
// Used to activate zone/gesture handlers when any player needs them,
// even if the global su_enableTouch is different (split-screen mixed modes).
static bool su_AnyPlayerHasTouchMode(int mode) {
    for (int i = 0; i < MAX_VIEWPORTS; i++) {
        if (su_GetEnableTouchForPlayer(i) == mode) return true;
    }
    return false;
}

// Forward declaration — eCamera.cpp provides the implementation.
// Sets absolute camera look offset in radians (yaw, pitch) with orbital
// compensation so the cycle stays at the same screen position.
extern "C" void aa_SetGyroCameraOffset(float yaw, float pitch);

// Gyro camera look — complementary filter (gyro + accelerometer).
// Gyroscope: smooth, fast response, but drifts over time.
// Accelerometer: absolute tilt from gravity, noisy but drift-free.
// Blend: 98% gyro + 2% accel correction each frame.
static int su_gyroActive = 0;
static SDL_Sensor* su_accelSensor = nullptr;
static SDL_Sensor* su_gyroSensor  = nullptr;
static float su_refAngle     = 0.0f;  // reference angle at activation
static bool  su_refCaptured  = false;
static float su_accelAngle   = 0.0f;  // latest absolute angle from accelerometer
static float su_steerAngle   = 0.0f;  // filtered steering output (radians)
static Uint64 su_lastGyroTs  = 0;     // last gyro timestamp (nanoseconds)

static SDL_SensorID su_FindSensor(SDL_SensorType type)
{
    int count = 0;
    SDL_SensorID* ids = SDL_GetSensors(&count);
    if (!ids) return 0;
    SDL_SensorID found = 0;
    for (int i = 0; i < count; i++) {
        if (SDL_GetSensorTypeForID(ids[i]) == type) {
            found = ids[i];
            break;
        }
    }
    SDL_free(ids);
    return found;
}

extern "C" void su_SetGyroActive(int active) {
    su_gyroActive = active;
    if (active) {
        if (!su_accelSensor) {
            SDL_SensorID id = su_FindSensor(SDL_SENSOR_ACCEL);
            if (id) {
                su_accelSensor = SDL_OpenSensor(id);
                if (!su_accelSensor) con << "Warning: failed to open accelerometer\n";
            }
        }
        if (!su_gyroSensor) {
            SDL_SensorID id = su_FindSensor(SDL_SENSOR_GYRO);
            if (id) {
                su_gyroSensor = SDL_OpenSensor(id);
                if (!su_gyroSensor) con << "Warning: failed to open gyroscope\n";
            }
        }
        su_refCaptured = false;
        su_steerAngle  = 0.0f;
        su_accelAngle  = 0.0f;
        su_lastGyroTs  = 0;
    } else {
        if (su_accelSensor) { SDL_CloseSensor(su_accelSensor); su_accelSensor = nullptr; }
        if (su_gyroSensor)  { SDL_CloseSensor(su_gyroSensor);  su_gyroSensor  = nullptr; }
        su_refCaptured = false;
        su_steerAngle  = 0.0f;
        aa_SetGyroCameraOffset(0.0f, 0.0f);
    }
}

bool su_IsGyroActive()  { return su_gyroActive != 0; }

// Cached — avoids SDL_GetSensors() + SDL_free() per frame.
static int su_hasGyroSensorCached = -1;
bool su_HasGyroSensor() {
    if (su_hasGyroSensorCached < 0)
        su_hasGyroSensorCached = (su_FindSensor(SDL_SENSOR_ACCEL) != 0 ||
                                   su_FindSensor(SDL_SENSOR_GYRO)  != 0) ? 1 : 0;
    return su_hasGyroSensorCached != 0;
}

bool su_mouseGrab = false;

static uAction* su_allActions[uMAX_ACTIONS];
static int     su_allActionsLen = 0;

uAction::uAction(uAction *&anchor,const char* name,
                 int priority_,
                 uInputType t)
        :tListItem<uAction>(anchor),tooltip_(NULL), shouldShowInGenericConfigurationMenu_( true ), type(t),priority(priority_),internalName(name)
{
    globalID = localID = su_allActionsLen++;

    tASSERT(localID < uMAX_ACTIONS);

    su_allActions[localID] = this;

    tString descname;
    descname << "input_" << name << "_text";
    tToLower( descname );

    const_cast<tOutput&>(description).AddLocale(descname);

    tString helpname;
    helpname << "input_" << name << "_help";
    tToLower( helpname );

    const_cast<tOutput&>(helpText).AddLocale(helpname);
}

uAction::uAction(uAction *&anchor,const char* name,
                 const tOutput& desc,
                 const tOutput& help,
                 int priority_,
                 uInputType t)
        :tListItem<uAction>(anchor),tooltip_(NULL), shouldShowInGenericConfigurationMenu_( true ), type(t),priority(priority_),internalName(name), description(desc), helpText(help)
{
    globalID = localID = su_allActionsLen++;

    tASSERT(localID < uMAX_ACTIONS);

    su_allActions[localID] = this;
}

uAction::~uAction(){
    su_allActions[localID] = NULL;
}

uAction * uAction::Find( char const * name )
{
    for (int i=su_allActionsLen-1;i>=0;i--)
        if (!strcmp(name,su_allActions[i]->internalName))
            return su_allActions[i];

    return 0;
}

// ****************************************
// uInput
// ****************************************

typedef std::vector< uInput * > uInputs;
uInputs su_inputs;
std::map< std::string, uInput * > su_inputMap; // map from persistent ID to input

// alternative map for legacy keycodes
std::map< std::string, uInput * > su_alternativeInputMap;

uInput::uInput( tString const & persistentID, tString const & name )
  : persistentID_( persistentID ), name_( name )
        , pressed_( 0 )
{
    ID_ = su_inputs.size();
    su_inputs.push_back( this );
    su_inputMap[ persistentID ] = this;
}

uInput::~uInput()
{
    su_inputs[ID_] = 0;
    // su_inputMap.erase( su_inputMap.find( persistentID_ ) );
}

static uInput * su_GetInput( tString const & persistentID )
{
    try
    {
        uInput * ret = su_inputMap[ persistentID ];
        if(!ret)
            ret = su_alternativeInputMap[ persistentID ];
        return ret;
    }
    catch (...)
    {
        return NULL;
    }
}

// class that manages unknown input
class uAutoDeleteInput
{
public:
    uAutoDeleteInput()
    {
    }

    ~uAutoDeleteInput()
    {
        for ( uInputs::iterator iter = unknowns.begin(); iter != unknowns.end(); ++iter )
        {
            delete(*iter);
            *iter = 0;
        }
    }

    uInput * Create( tString const & persistentID, tString const & name )
    {
        uInput * input = new uInput( persistentID, name );
        unknowns.push_back( input );
        return input;
    }

    // array of unknown inputs that should be deleted afterwards
    uInputs unknowns;
};

static uAutoDeleteInput & su_GetAutoDeleteInput()
{
    static uAutoDeleteInput filler;
    return filler;
}

static uInput * su_NewInput( tString const & ID, tString const & name )
{
    return su_GetAutoDeleteInput().Create( ID, name );
}

static uInput * su_NewInput( char const * ID, char const * name )
{
    return su_GetAutoDeleteInput().Create( tString( ID ), tString( name ) );
}

static void su_WriteSanitizedKeyname(std::ostream & s, char const * input)
{
    for ( char const * c = input; *c; ++c )
    {
        if ( isblank( *c ) )
        {
            s << "_";
        }
        else
        {
            s << char(toupper( *c ));
        }
    }
}

#ifndef DEDICATED
// class that manages keyboard input
class uKeyInput
{
public:
    uKeyInput()
    {
        // SDL3: SDL_NUM_SCANCODES → SDL_SCANCODE_COUNT
        for ( int i = 0; i <= SDL_SCANCODE_COUNT; ++i )
        {
            tString displayID;
            tString persistentID;
            tString alternativePersistentID;
            // SDL3: SDL_GetKeyFromScancode now requires modstate and key_event params
            SDL_Scancode scancode = static_cast<SDL_Scancode>(i);
            SDL_Keycode key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
            displayID = SDL_GetKeyName(key);
            switch(scancode)
            {
            default:
            {
                std::ostringstream s;
                const char * name = SDL_GetScancodeName(scancode); // probably the same as displayID for US keyboards
                if(name && name[0] >= ' ')
                {
                    s << "SCANCODE_";
                    su_WriteSanitizedKeyname(s, name);
                }
                else
                {
                    s << "UNKNOWNSCANCODE_" << i;
                }
                persistentID = s.str();
            }
            break;
            // couple of special cases
            case SDL_SCANCODE_RETURN2:
                persistentID = "SCANCODE_RETURN2";
                break;
            case SDL_SCANCODE_BACKSLASH:
                persistentID = "SCANCODE_BACKSLASH";
                break;
            case SDL_SCANCODE_APOSTROPHE:
                persistentID = "SCANCODE_APOSTROPHE";
                break;
            case SDL_SCANCODE_SLASH:
                persistentID = "SCANCODE_SLASH";
                break;
            case SDL_SCANCODE_LEFTBRACKET:
                persistentID = "SCANCODE_LEFTBRACKET";
                break;
            case SDL_SCANCODE_RIGHTBRACKET:
                persistentID = "SCANCODE_RIGHTBRACKET";
                break;
            case SDL_SCANCODE_KP_LEFTPAREN:
                persistentID = "SCANCODE_KP_LEFTPAREN";
                break;
            case SDL_SCANCODE_KP_RIGHTPAREN:
                persistentID = "SCANCODE_KP_RIGHTPAREN";
                break;
            case SDL_SCANCODE_KP_LEFTBRACE:
                persistentID = "SCANCODE_KP_LEFTBRACE";
                break;
            case SDL_SCANCODE_KP_RIGHTBRACE:
                persistentID = "SCANCODE_KP_RIGHTBRACE";
                break;
            }
            if(displayID.size() == 0)
            {
                std::ostringstream s;
                s << "UNKNOWN_" << i;
                displayID = s.str();
            }
            {
                std::ostringstream s;
                s << "KEY_";
                su_WriteSanitizedKeyname(s, displayID.c_str());
                alternativePersistentID = s.str();
            }
            if(persistentID.size() == 0)
            {
                persistentID = alternativePersistentID;
            }

            // store new input key
            uInput * pNewInput = su_NewInput( persistentID, displayID );
            sdl_keys[i] = pNewInput;

            // store in alternative map
            if(alternativePersistentID != persistentID)
                su_alternativeInputMap[ alternativePersistentID ] = pNewInput;


            tASSERT( sdl_keys[i]->ID() == i );
        }
    }

    ~uKeyInput()
    {
    }

    uInput * sdl_keys[SDL_SCANCODE_COUNT+1];
};

static uKeyInput const & su_GetKeyInput()
{
    static uKeyInput filler;
    return filler;
}
#endif // DEDICATED (uKeyInput)

#ifndef DEDICATED
class uTouchInput
{
public:
    explicit uTouchInput(int playerN = 1) {
        tString leftID("TOUCH_LEFT_TURN");
        tString rightID("TOUCH_RIGHT_TURN");
        tString brakeID("TOUCH_BRAKE");
        if (playerN > 1) {
            leftID  << "_" << playerN;
            rightID << "_" << playerN;
            brakeID << "_" << playerN;
        }
        turnLeft  = su_NewInput( leftID,  tString("touch left turn") );
        turnRight = su_NewInput( rightID, tString("touch right turn") );
        brake     = su_NewInput( brakeID, tString("touch brake") );

        uAction * actLeft  = uAction::Find( "CYCLE_TURN_LEFT" );
        uAction * actRight = uAction::Find( "CYCLE_TURN_RIGHT" );
        uAction * actBrake = uAction::Find( "CYCLE_BRAKE" );

        tJUST_CONTROLLED_PTR< uBind > bindLeft  = uBindPlayer::NewBind(actLeft,  playerN);
        tJUST_CONTROLLED_PTR< uBind > bindRight = uBindPlayer::NewBind(actRight, playerN);
        tJUST_CONTROLLED_PTR< uBind > bindBrake = uBindPlayer::NewBind(actBrake, playerN);

        turnLeft->SetBind(bindLeft);
        turnRight->SetBind(bindRight);
        brake->SetBind(bindBrake);
    }

    uInput* turnLeft;
    uInput* turnRight;
    uInput* brake;
};

// One touch-input set per player slot (playerN = 1..4, stored at index 0..3)
static uTouchInput* su_touchInputsByPlayer[4] = { nullptr, nullptr, nullptr, nullptr };

static uTouchInput& su_GetTouchInput(int playerN = 1)
{
    int idx = (playerN >= 1 && playerN <= 4) ? (playerN - 1) : 0;
    if (!su_touchInputsByPlayer[idx])
        su_touchInputsByPlayer[idx] = new uTouchInput(playerN);
    return *su_touchInputsByPlayer[idx];
}

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
// Returns viewport index (0-based) that contains screen point (x, y),
// where y=0 is top (SDL convention). Returns -1 if none.
static int su_FindViewportForTouch(float x, float y)
{
    rViewportConfiguration* vc = rViewportConfiguration::CurrentViewportConfiguration();
    if (!vc) return -1;
    float yFlipped = 1.0f - y; // convert SDL top-origin to OpenGL bottom-origin
    for (int i = 0; i < vc->num_viewports; i++) {
        rViewport* vp = vc->Port(i);
        if (!vp) continue;
        tCoord pos = vp->GetPosition();
        tCoord dim = vp->GetDimensions();
        if (x >= pos.x && x < pos.x + dim.x &&
            yFlipped >= pos.y && yFlipped < pos.y + dim.y)
            return i;
    }
    return -1;
}

// Rotate (x, y) around center (0.5, 0.5) to undo the viewport's visual rotation.
// The UV table convention means +rotDeg gives the correct inverse here.
static void su_RotateTouchCoord(float& x, float& y, int rotDeg)
{
    if (rotDeg == 0) return;
    float cx = x - 0.5f, cy = y - 0.5f;
    float rad = (float)rotDeg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    x = c * cx - s * cy + 0.5f;
    y = s * cx + c * cy + 0.5f;
}

// Rotate delta vector (dx, dy) to undo the viewport's visual rotation.
// SDL screen coordinates use y+ downward, so the correct rotation matrix
// negates the sin terms relative to standard y-up math convention.
// At 0°/180° sin=0 so the sign has no effect; ±90° is the critical case.
static void su_RotateTouchDelta(float& dx, float& dy, int rotDeg)
{
    if (rotDeg == 0) return;
    float rad = (float)rotDeg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float nx =  c * dx + s * dy; // negated sin: y-down screen convention
    float ny = -s * dx + c * dy;
    dx = nx;
    dy = ny;
}
#endif // mobile
#endif // DEDICATED

# define MOUSE_BUTTONS 7

// class that manages mouse inout
class uMouseInput
{
public:
    uMouseInput()
    {
        x_plus = su_NewInput( "MOUSE_X_PLUS", "mouse right" );
        x_minus = su_NewInput( "MOUSE_X_MINUS", "mouse left" );
        y_plus = su_NewInput( "MOUSE_Y_PLUS", "mouse up" );
        y_minus = su_NewInput( "MOUSE_Y_MINUS", "mouse down" );
        z_plus = su_NewInput( "MOUSE_Z_PLUS", "mouse z up" );
        z_minus = su_NewInput( "MOUSE_Z_MINUS", "mouse z down" );

        for ( int i = 0; i < MOUSE_BUTTONS; ++i )
        {
            std::ostringstream id;
            id << "MOUSE_BUTTON_" << i+1;
            std::ostringstream name;
            name << "mouse button " << i+1;
            button[i] = su_NewInput( id.str(), name.str() );
        }
    }

    uInput * x_plus;
    uInput * x_minus;
    uInput * y_plus;
    uInput * y_minus;
    uInput * z_plus;
    uInput * z_minus;
    uInput * button[MOUSE_BUTTONS];
};

static uMouseInput const & su_GetMouseInput()
{
    static uMouseInput filler;
    return filler;
}

void su_KeyInit()
{
#ifndef DEDICATED
    su_GetKeyInput();
#endif
    su_GetMouseInput();
}

// ****************************************
// a configuration class for keyboard binds
// ****************************************

class tConfItem_key:public tConfItemBase{
public:
    tConfItem_key():tConfItemBase("KEYBOARD"){}
    ~tConfItem_key(){}

    // write the complete keymap
    virtual void WriteVal(std::ostream &s){
        int first=1;
        for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
        {
            if ( !(*i)->GetBind() )
                continue;

            std::string const & id = (*i)->PersistentID();

            if (!first)
                s << "\nKEYBOARD\t";
            else
                first=0;

            s << id << '\t';
            (*i)->GetBind()->Write(s);
        }
        if (first)
            s << "-1";
    }

    // read one keybind
    virtual void ReadVal(std::istream &s)
    {
        tString in;
        std::string id;
        s >> id;
        if ( id != "-1" )
        {
            // try to fetch the uInput belonging to the id
            uInput * input = su_GetInput( id );

            if ( !input )
            {
                // if the id is a number, the setting is in a legacy format.
                std::istringstream readValue( id );
                int value = -1;
                readValue >> value;

#ifndef DEDICATED
                // map from old SDLKey to SDL_Scancode
                switch(value)
                {
                default:
                    if(value < 127)
                    {
                        // regular keys
                        // SDL3: SDL_GetScancodeFromKey now requires modstate pointer
                        value = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(value), NULL);
                    }
                    else if( value >= 282 && value <= 293)
                    {
                        // function keys
                        value = value - 282 + SDL_SCANCODE_F1;
                    }
                    else
                    {
                        // no mapping, ignore
                        return;
                    }
                    break;
                    // special keys
                case 273: value = SDL_SCANCODE_UP; break;
                case 274: value = SDL_SCANCODE_DOWN; break;
                case 275: value = SDL_SCANCODE_RIGHT; break;
                case 276: value = SDL_SCANCODE_LEFT; break;
                case 277: value = SDL_SCANCODE_INSERT; break;
                case 278: value = SDL_SCANCODE_HOME; break;
                case 279: value = SDL_SCANCODE_END; break;
                case 280: value = SDL_SCANCODE_PAGEUP; break;
                case 281: value = SDL_SCANCODE_PAGEDOWN; break;
                    // other keys have no unique mapping
                }
#endif // DEDICATED

                if ( value >= 0 && value < (int)su_inputs.size() )
                {
                    input = su_inputs[ value ];
                }
            }

            if ( !input )
            {
                input = su_NewInput( id, tString("") );
            }

            tASSERT( input );

            s >> in;
            if (uBindPlayer::IsKeyWord(in))
            {
                tJUST_CONTROLLED_PTR< uBind > bind = uBindPlayer::NewBind(s);
                if ( bind->act )
                {
                    input->SetBind( bind );
                }
            }
        }
        char c=' ';
        while (c!='\n' && s.good() && !s.eof()) c=s.get();
    }
};

// we need just one
static tConfItem_key x;

static uAction *s_playerActions;
static uAction *s_cameraActions;
static uAction *s_globalActions;

uActionPlayer::uActionPlayer(const char *name,
                             int priority,
                             uInputType t)
    :uAction(s_playerActions,name,priority,t){}

uActionPlayer::uActionPlayer(const char *name,
                             const tOutput& desc,
                             const tOutput& help,
                             int priority,
                             uInputType t)
        :uAction(s_playerActions,name,desc,help,priority,t){}

uActionPlayer::~uActionPlayer(){}

bool uActionPlayer::operator==(const uActionPlayer &x){
    return x.globalID == globalID;
}

uActionPlayer *uActionPlayer::Find(int id){
    uAction *run = s_playerActions;

    while (run){
        if (run->ID() == id)
            return static_cast<uActionPlayer*>(run);
        run = run->Next();
    }

    return NULL;
}


uActionCamera::uActionCamera(const char *name,
                             int priority,
                             uInputType t)
    :uAction(s_cameraActions,name,priority,t){}

uActionCamera::~uActionCamera(){}

bool uActionCamera::operator==(const uActionCamera &x){
    return x.globalID == globalID;
}


// global actions
uActionGlobal::uActionGlobal(const char *name,
                             int priority,
                             uInputType t)
        :uAction(s_globalActions,name,priority,t){}

uActionGlobal::~uActionGlobal(){}

bool uActionGlobal::operator==(const uActionGlobal &x){
    return x.globalID == globalID;
}

bool uActionGlobal::IsBreakingGlobalBind(int sym){
    if ( sym >= (int)su_inputs.size() || sym < 0 )
        return false;

    uInput * input = su_inputs[ sym ];
    if ( !input )
        return false;

    uBind * bind = input->GetBind();
    if ( !bind )
        return false;

    uAction *act = bind->act;
    if (!act)
        return false;

    return uActionGlobalFunc::IsBreakingGlobalBind(act);
}

static void keyboard_init()
{
}

static void keyboard_exit()
{
}

#ifndef NOJOYSTICK

#ifndef DEDICATED
class uJoystick
{
public:
    SDL_JoystickID id;

    // joystick name
    tString name, internalName;

    // number of compotents
    int numAxes, numButtons, numBalls, numHats;

    enum Direction
    {
        Left = 0,
        Right = 1,
        Up = 2,
        Down = 3
    };

    uJoystick( SDL_JoystickID id_ ):id(id_)
    {
        // SDL3: SDL_JoystickOpen → SDL_OpenJoystick
        SDL_Joystick * stick = SDL_OpenJoystick( id );
        if (!stick)
        {
            name = "Unavailable Joystick";
            return;
        }
        // SDL3: SDL_JoystickName → SDL_GetJoystickName
        name = SDL_GetJoystickName( stick );

        std::ostringstream iName;
        iName << "JOYSTICK_";
        for ( char const * c = name.c_str(); *c; ++c )
        {
            if ( isblank( *c ) )
            {
                iName << "_";
            }
            else
            {
                iName << char(toupper( *c ));
            }
        }

        internalName = iName.str();

        // SDL3: SDL_JoystickNumAxes → SDL_GetNumJoystickAxes, etc.
        numAxes = SDL_GetNumJoystickAxes( stick );
        numButtons = SDL_GetNumJoystickButtons( stick );
        numBalls = SDL_GetNumJoystickBalls( stick );
        numHats = SDL_GetNumJoystickHats( stick );

        // populate input types
        if ( numAxes >= 1 )
        {
            axes[0].push_back( su_NewInput( GetPersistentID( "AXIS", 0, "-" ), GetName( "left" ) ) );
            axes[1].push_back( su_NewInput( GetPersistentID( "AXIS", 0, "+" ), GetName( "right" ) ) );
        }

        if ( numAxes >= 2 )
        {
            axes[0].push_back( su_NewInput( GetPersistentID( "AXIS", 1, "-" ), GetName( "up" ) ) );
            axes[1].push_back( su_NewInput( GetPersistentID( "AXIS", 1, "+" ), GetName( "down" ) ) );
        }

        for ( int axis = 2; axis < numAxes; ++axis )
        {
            axes[0].push_back( su_NewInput( GetPersistentID( "AXIS", axis, "-" ), GetName( "axis", axis, "-" ) ) );
            axes[1].push_back( su_NewInput( GetPersistentID( "AXIS", axis, "+" ), GetName( "axis", axis, "+" ) ) );
        }

        for ( int button = 0; button < numButtons; ++button )
        {
            buttons.push_back( su_NewInput( GetPersistentID( "BUTTON", button ), GetName( "button", button ) ) );
        }

        char const * directionInternal[] = { "LEFT", "RIGHT", "UP", "DOWN" };
        char const * directionHuman[] = { "left", "right", "up", "down" };

        for ( int dir = 0; dir < 4; ++dir )
        {
            char const * internal = directionInternal[dir];
            char const * human = directionHuman[dir];

            for ( int ball = 0; ball < numBalls; ++ball )
            {
                balls[dir].push_back( su_NewInput( GetPersistentID( "BALL", ball, internal ), GetName( "ball", ball, human ) ) );
            }

            for ( int hat = 0; hat < numHats; ++hat )
            {
                hats[dir].push_back( su_NewInput( GetPersistentID( "HAT", hat, internal ), GetName( "hat", hat, human ) ) );
            }
        }

        hatDirection.resize( numHats );
    }

    uInput * GetAxis( int index, int dir )
    {
        tASSERT( index >= 0 && index < numAxes );
        tASSERT( 0 == dir || 1 == dir );

        axes[1-dir][index]->SetPressed(0);
        return axes[dir][index];
    }

    uInput * GetButton( int index )
    {
        tASSERT( index >= 0 && index < numButtons );

        return buttons[index];
    }

    uInput * GetBall( int index, Direction dir )
    {
        tASSERT( index >= 0 && index < numBalls );

        balls[dir ^ 1][index]->SetPressed(0);
        return balls[dir][index];
    }

    uInput * GetHat( int index, Direction dir )
    {
        tASSERT( index >= 0 && index < numHats );

        hats[dir ^ 1][index]->SetPressed(0);
        return hats[dir][index];
    }

    int & GetHatDirection( int index, int axis )
    {
        tASSERT( index >= 0 && index < numHats );
        tASSERT( 0 == axis || 1 == axis );

        return hatDirection[index].dir[axis];
    }
private:
    // various input arrays
    uInputs axes[2], buttons, balls[4], hats[4];

    struct HatDirections
    {
        int dir[2];
        HatDirections(){
            dir[0] = dir[1] = 0;
        }
    };

    std::vector< HatDirections > hatDirection;

    // returns an uInput unique name
    tString GetPersistentID( char const * type, int subID, char const * suffix = NULL ) const
    {
        std::ostringstream o;
        o << internalName << "_" << type << "_" << subID;
        if ( suffix )
        {
            o << "_" << suffix;
        }
        return o.str();
    }

    // same for the public name
    tString GetName( char const * type, int subID, char const * suffix = NULL ) const
    {
        std::ostringstream o;
        o << "Joystick " << id+1 << " " << type << " " << subID+1;
        if ( suffix )
        {
            o << " " << suffix;
        }
        return o.str();
    }

    // special axes: x and y
    tString GetName( char const * type ) const
    {
        std::ostringstream o;
        o << "Joystick " << id+1 << " " << type;
        return o.str();
    }
};

// class that manages joysticks
class uJoystickInput
{
public:
    uJoystickInput()
    {
        // create joysticks
        // SDL3: SDL_NumJoysticks() → SDL_GetJoysticks()
        int numJoysticks = 0;
        SDL_JoystickID* joystickIDs = SDL_GetJoysticks(&numJoysticks);
        if (joystickIDs)
        {
            for ( int i = 0; i < numJoysticks; ++i )
            {
                joysticks.push_back( new uJoystick( joystickIDs[i] ) );  // Pass ID, not index!
            }
            SDL_free(joystickIDs);
        }
    }

    ~uJoystickInput()
    {
        // delete joysticks
        for ( std::vector< uJoystick * >::iterator iter = joysticks.begin(); iter != joysticks.end(); ++iter )
        {
            delete(*iter);
            *iter = 0;
        }
    }

    // array of joysticks
    std::vector< uJoystick * > joysticks;
};

static uJoystickInput & su_GetJoystickInput()
{
    static uJoystickInput filler;
    return filler;
}

static uJoystick * su_GetJoystick( int id )
{
    uJoystickInput & joysticks = su_GetJoystickInput();
    if(id >= 0 && id < (int)joysticks.joysticks.size() )
        return joysticks.joysticks[id];
    else
        return NULL;
}

void su_JoystickInit()
{
    su_GetJoystickInput();
    // SDL3: SDL_JoystickEventState(SDL_ENABLE) → SDL_SetJoystickEventsEnabled(true)
    SDL_SetJoystickEventsEnabled(true);
}
#endif
#endif

static tInitExit keyboard_ie(&keyboard_init, &keyboard_exit);

// *********************************************
// generic keypress/mouse movement binding class
// *********************************************

uBind::~uBind(){}

uBind::uBind(uAction *a ):lastValue_(0), delayedValue_(0), lastInput_(0), lastTime_(-1), act(a){}

uBind::uBind(std::istream &s): lastValue_(0), delayedValue_(0), lastInput_(0), lastTime_(-1), act(NULL)
{
    std::string name;
    s >> name;
    act = uAction::Find( name.c_str() );
}

void uBind::Write(std::ostream &s){
    s << act->internalName << '\t';
}

bool GlobalAct(uAction *act,REAL x){
    return uActionGlobalFunc::GlobalAct(act,x);
}

bool uBind::Activate(REAL x, bool delayed )
{
    delayedValue_ = x;

    if ( !delayed || !Delayable() )
    {
        lastValue_ = x;
        return this->DoActivate( x );
    }

    return true;
}

void uBind::HanldeDelayed()
{
    if ( lastValue_ != delayedValue_ )
    {
        lastValue_ = delayedValue_;
        this->DoActivate( delayedValue_ );
    }
}

REAL su_doubleBindTimeout=-10.0f;

bool uBind::IsDoubleBind( uInput const * input )
{
    double currentTime = tSysTimeFloat();

    // if a different key was used for this action a short while ago, give alarm.
    bool ret = ( su_doubleBindTimeout > 0 && input != lastInput_ && currentTime - lastTime_ < su_doubleBindTimeout );

    // store last usage
    lastInput_ = input;
    lastTime_ = currentTime;

    // return result
    return ret;
}

// *******************
// player config
// *******************

static int nextid = 0;

uPlayerPrototype* uPlayerPrototype::PlayerConfig(int i){
    tASSERT(i>=0 && i<uMAX_PLAYERS);
    return playerConfig[i];
}


uPlayerPrototype::uPlayerPrototype(){
    static bool inited=false;
    if (!inited)
    {
        for (int i=uMAX_PLAYERS-1; i >=0; i--)
            playerConfig[i] = NULL;

        inited = true;
    }

    id = nextid++;
    tASSERT(id < uMAX_PLAYERS);
    playerConfig[id] = this;


}

uPlayerPrototype::~uPlayerPrototype(){
    playerConfig[id] = NULL;
}

uPlayerPrototype* uPlayerPrototype::playerConfig[uMAX_PLAYERS];

int uPlayerPrototype::Num(){
    return nextid;
}

// *******************
// Input configuration
// *******************

REAL mouse_sensitivity=REAL(.1);

// number of transformed events per SDL event
#define su_TRANSFORM_PASSES 2

struct uTransformEventInfo
{
    uInput * input;   // the input event type received
    float value;      // the strenght of the event
    bool needsRepeat; // if the event should trigger a continuous action (camera movement), should it be repeated?

    uTransformEventInfo(): input(0), value(0), needsRepeat(false){}
    uTransformEventInfo( uInput * input_, float value_ = 1 , bool needsRepeat_ = true ): input(input_), value(value_), needsRepeat(needsRepeat_){}
};

int GetPlayerCameraClosestDirection(int player);
int GetPlayerWindingNumber(int player);

#ifndef DEDICATED
// Forward declaration: implemented in cCockpit.cpp — routes touch to widget buttons (mode 3)
bool cCockpit_ProcessTouch(float x, float y, uint32_t type, int64_t fingerId);
#endif

// transform SDL event into vector of abstract events
#ifndef DEDICATED
static void su_TransformEvent( SDL_Event & e, std::vector< uTransformEventInfo > & info )
{
    switch (e.type)
    {
    // SDL3: SDL_MOUSEMOTION → SDL_EVENT_MOUSE_MOTION
    case SDL_EVENT_MOUSE_MOTION:
        // Desktop testing for the touch overlay: synthesise a finger MOTION
        // event from mouse motion when the LEFT button is held. SDL3 maps
        // touches → mouse on mobile via SDL_HINT_MOUSE_TOUCH_EVENTS; this
        // is the reverse for desktop. Uses a fixed synthetic finger ID.
        // Skip synthesis when mouse motion was synthesized from a touch by SDL
        // (which == SDL_TOUCH_MOUSEID): real FINGER_MOTION events handle it.
        if (su_enableTouch >= 1 && e.motion.which != SDL_TOUCH_MOUSEID)
        {
            static bool s_mouseHeld = false;
            // Track LMB held state across motion events.
            s_mouseHeld = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
            if (s_mouseHeld) {
                const long kMouseFingerId = -42;
                int winW = sr_screenWidth, winH = sr_screenHeight;
                if (sr_screen) SDL_GetWindowSize(sr_screen, &winW, &winH);
                const float fx = e.motion.x / static_cast<float>(winW);
                const float fy = e.motion.y / static_cast<float>(winH);
                if (cCockpit_ProcessTouch(fx, fy, SDL_EVENT_FINGER_MOTION, kMouseFingerId))
                    break; // consumed by a cockpit touch button drag
            }
        }
        if (!su_enableTouch || su_gyroActive)
        {
            // ignore events generated by mouse grabbing
            if ( su_mouseGrab &&
                    e.motion.x==sr_screenWidth/2 && e.motion.x==sr_screenHeight/2)
            {
                return;
            }

            // we generate up to four events per mouse movement
            info.reserve(4);

            REAL xrel=e.motion.xrel;
            if (xrel > 0) // right
            {
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().x_minus,
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().x_plus,
                                    xrel * mouse_sensitivity,
                                    false ) );
            }

            if (xrel < 0) // left
            {
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().x_plus,
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().x_minus,
                                    -xrel * mouse_sensitivity,
                                    false ) );
            }

            // invert mouse movement
            REAL yrel=-e.motion.yrel;
            if (yrel>0) // up
            {
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().y_minus,
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().y_plus,
                                    yrel * mouse_sensitivity,
                                    false ) );
            }
            if (yrel<0) // down
            {
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().y_plus,
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().y_minus,
                                    -yrel * mouse_sensitivity,
                                    false ) );
            }
        }
        break;
    // SDL3: SDL_MOUSEBUTTONDOWN → SDL_EVENT_MOUSE_BUTTON_DOWN
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // Desktop testing: synthesise a finger DOWN/UP from the primary
        // mouse button so overlay buttons can be tapped without a touch
        // device. Right/middle clicks fall through to the original handler.
        // Skip synthesis when the mouse event was itself synthesized from a
        // touch by SDL (which == SDL_TOUCH_MOUSEID): on iOS, real FINGER_DOWN
        // events arrive for the same touch and would conflict with the -42 id.
        if (su_enableTouch >= 1 && e.button.button == SDL_BUTTON_LEFT
            && e.button.which != SDL_TOUCH_MOUSEID)
        {
            const long kMouseFingerId = -42;
            const uint32_t ft = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                              ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
            // SDL3 mouse coords are in logical (window) pixels; sr_screenWidth
            // is the swapchain physical extent — 2× on Retina. Use the window's
            // logical size so the fraction matches how touch events are reported.
            int winW = sr_screenWidth, winH = sr_screenHeight;
            if (sr_screen) SDL_GetWindowSize(sr_screen, &winW, &winH);
            const float fx = e.button.x / static_cast<float>(winW);
            const float fy = e.button.y / static_cast<float>(winH);
            if (cCockpit_ProcessTouch(fx, fy, ft, kMouseFingerId))
                break; // consumed by a cockpit touch button
        }
        {
            int button=e.button.button;
            if (button<=MOUSE_BUTTONS)
            {
                info.push_back( uTransformEventInfo(
                                    su_GetMouseInput().button[ button ],
                                    ( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) ? 1 : 0 ) );
            }
        }
        break;
    // SDL3: SDL_FINGERDOWN → SDL_EVENT_FINGER_DOWN, etc.
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
        // Cockpit-driven touch buttons get first crack. This includes the
        // primary cockpit's TouchButtons and the optional touch-overlay
        // cockpit appended on top of it (Phase 3 of the cockpit redesign).
        // Buttons gate themselves on ENABLE_TOUCH mode via touchMode="…".
        if (su_enableTouch >= 1 &&
            cCockpit_ProcessTouch(e.tfinger.x, e.tfinger.y, e.type, e.tfinger.fingerID))
        {
            break; // consumed by a cockpit button
        }
        if (su_enableTouch==1 || su_AnyPlayerHasTouchMode(1))
        {
            static SDL_FingerID finger = 0;
            static int finger_player = 1; // 1-based player for tracked brake finger
            // Reset brake tracking on viewport config change.
            static int lastConf1 = -1;
            { int c = rViewportConfiguration::CurrentConfNum();
              if (c != lastConf1) { lastConf1 = c; finger = 0; finger_player = 1; } }

            if (e.type == SDL_EVENT_FINGER_DOWN) {
                float tx = e.tfinger.x;
                float ty = e.tfinger.y;
                int playerN = 1;
                int effectiveMode = su_enableTouch;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
                int vpIdx = su_FindViewportForTouch(tx, ty);
                if (vpIdx >= 0) {
                    int pid = sr_viewportBelongsToPlayer[vpIdx];
                    playerN = pid + 1; // 0-based → 1-based
                    effectiveMode = su_GetEnableTouchForPlayer(pid);
                    // Get viewport-local x (0..1) for zone detection
                    rViewport* vp = rViewportConfiguration::CurrentViewportConfiguration()->Port(vpIdx);
                    tCoord pos = vp->GetPosition(); tCoord dim = vp->GetDimensions();
                    float lx = (tx - pos.x) / (dim.x > 0 ? dim.x : 1.0f);
                    float ly = (1.0f - ty - pos.y) / (dim.y > 0 ? dim.y : 1.0f);
                    int rot = sr_GetViewportRotationDeg(rViewportConfiguration::CurrentConfNum(), vpIdx);
                    su_RotateTouchCoord(lx, ly, rot);
                    tx = lx; // use rotated local x for zone test below
                }
#endif
                if (effectiveMode == 1) {
                    if (tx<0.33)      info.push_back( uTransformEventInfo( su_GetTouchInput(playerN).turnLeft, 1 ) );
                    else if (tx>0.67) info.push_back( uTransformEventInfo( su_GetTouchInput(playerN).turnRight, 1 ) );
                    else if (!finger) {
                        finger = e.tfinger.fingerID;
                        finger_player = playerN;
                        info.push_back( uTransformEventInfo( su_GetTouchInput(playerN).brake, 1 ) );
                    }
                }
            } else if (e.type == SDL_EVENT_FINGER_UP) {
                if (finger == e.tfinger.fingerID) {
                    finger = 0;
                    info.push_back( uTransformEventInfo( su_GetTouchInput(finger_player).brake, 0 ) );
                }
            }
        }
        if (su_enableTouch==2 || su_AnyPlayerHasTouchMode(2))
        {
            // Per-viewport state (one slot per viewport, up to MAX_VIEWPORTS)
            struct VpState {
                SDL_FingerID finger = 0;
                float dx = 0, dy = 0;
                char count = 0;
                int previous_dir = -1;
                Uint64 last_time = 0;
                int playerN = 1;
                int lockedVpIdx = -1; // viewport index locked at touch-down
            };
            static VpState vpStates[MAX_VIEWPORTS];
            // Reset stale slots when viewport config changes.
            static int lastConfNum = -1;
            int curConfNum = rViewportConfiguration::CurrentConfNum();
            if (curConfNum != lastConfNum) {
                lastConfNum = curConfNum;
                for (int j = 0; j < MAX_VIEWPORTS; j++) vpStates[j] = VpState{};
            }

            // Find which viewport slot to use
            auto findSlot = [&](SDL_FingerID fid, int* outVp) -> VpState* {
                for (int i = 0; i < MAX_VIEWPORTS; i++)
                    if (vpStates[i].finger == fid) { if (outVp) *outVp = i; return &vpStates[i]; }
                return nullptr;
            };
            auto freeSlot = [&]() -> VpState* {
                for (int i = 0; i < MAX_VIEWPORTS; i++)
                    if (!vpStates[i].finger) return &vpStates[i];
                return nullptr; // all slots occupied
            };

            if (e.type == SDL_EVENT_FINGER_DOWN) {
                if (!findSlot(e.tfinger.fingerID, nullptr)) {
                    // Resolve which player this touch belongs to, and check
                    // that their effective mode is 2 before allocating a slot.
                    int gesturePlayerN = 1;
                    int gesturePid = 0;
                    int gestureVpIdx = -1;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
                    gestureVpIdx = su_FindViewportForTouch(e.tfinger.x, e.tfinger.y);
                    if (gestureVpIdx >= 0) {
                        gesturePid = sr_viewportBelongsToPlayer[gestureVpIdx];
                        gesturePlayerN = gesturePid + 1;
                    }
#endif
                    if (su_GetEnableTouchForPlayer(gesturePid) != 2) break; // player uses a different mode

                    VpState* s = freeSlot();
                    if (!s) break; // all slots occupied, ignore this finger
                    s->finger = e.tfinger.fingerID;
                    s->count = 0; s->dx = 0; s->dy = 0;
                    s->last_time = e.tfinger.timestamp;
                    s->playerN = gesturePlayerN;
                    s->lockedVpIdx = gestureVpIdx;
                    s->previous_dir = -1;
                }
            } else if (e.type == SDL_EVENT_FINGER_UP) {
                VpState* s = findSlot(e.tfinger.fingerID, nullptr);
                if (s) s->finger = 0;
            } else if (e.type == SDL_EVENT_FINGER_MOTION) {
                int vpIdx = -1; (void)vpIdx;
                VpState* s = findSlot(e.tfinger.fingerID, &vpIdx);
                if (s && s->finger == e.tfinger.fingerID) {
                    if (e.tfinger.timestamp - s->last_time > 50000000) {
                        s->count = 0; s->dx = 0; s->dy = 0;
                        s->last_time = e.tfinger.timestamp;
                    }
                    s->last_time = e.tfinger.timestamp;

                    float fdx = e.tfinger.dx, fdy = e.tfinger.dy;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
                    if (s->lockedVpIdx >= 0) {
                        int rot = sr_GetViewportRotationDeg(rViewportConfiguration::CurrentConfNum(), s->lockedVpIdx);
                        su_RotateTouchDelta(fdx, fdy, rot);
                    }
#endif
                    s->dx += fdx;
                    s->dy += fdy;
                    ++s->count;

                    if (s->count == 4) {
                        if (s->dx*s->dx + s->dy*s->dy > 0.0001f) {
                            float a = atan2f(s->dx, -s->dy);
                            int player0 = s->playerN - 1; // 0-based
                            int axes = GetPlayerWindingNumber(player0);
                            if (axes < 2) axes = 4; // fallback
                            a = (a<0 ? fabsf(a+(float)M_PI*2.0f) : a) * axes / ((float)M_PI*2.0f);
                            int dir = static_cast<int>(roundf(a)) % axes;
                            int side = (a - roundf(a)) < 0 ? 0 : 1;
                            if (s->previous_dir != dir) {
                                int diff_dir = s->previous_dir == -1 ? dir : dir - s->previous_dir;
                                diff_dir = diff_dir>axes/2 ? diff_dir-axes : diff_dir<-axes/2 ? axes+diff_dir : diff_dir;
                                if (diff_dir != 0) {
                                    if ((diff_dir==axes/2&&side==1)||(diff_dir==-axes/2&&side==0)) diff_dir=-diff_dir;
                                    uInput* input = diff_dir<0 ? su_GetTouchInput(s->playerN).turnLeft
                                                               : su_GetTouchInput(s->playerN).turnRight;
                                    for (int i = abs(diff_dir); i > 0; --i)
                                        info.push_back( uTransformEventInfo( input, 1 ) );
                                }
                                s->previous_dir = dir;
                            }
                        }
                        s->count = 0; s->dx = 0; s->dy = 0;
                    }
                }
            }
        }
        // Mode 3 with no button hit falls through here (no zone/swipe handling)
        break;
    // SDL3 sensor: complementary filter (gyro + accel) → steering offset.
    case SDL_EVENT_SENSOR_UPDATE:
        if (!su_gyroActive) break;

        // ── Accelerometer: absolute angle from gravity (noisy, drift-free) ──
        if (su_accelSensor && e.sensor.which == SDL_GetSensorID(su_accelSensor))
        {
            // In landscape-right, device held upright: gravity ≈ (-g, 0, 0).
            // Steering = rotation around Z-axis → gravity shifts onto Y.
            // Steering angle = atan2(ay, -ax).
            float ax = e.sensor.data[0];
            float ay = e.sensor.data[1];
            float angle = atan2f(ay, -ax);
            if (!su_refCaptured) {
                su_refAngle    = angle;
                su_refCaptured = true;
                su_steerAngle  = 0.0f;
            }
            su_accelAngle = angle - su_refAngle;
            // Wrap to [-π, π].
            if (su_accelAngle >  3.14159f) su_accelAngle -= 6.28318f;
            if (su_accelAngle < -3.14159f) su_accelAngle += 6.28318f;

            // If no gyro sensor available, fall back to filtered accel only.
            if (!su_gyroSensor) {
                su_steerAngle += (su_accelAngle - su_steerAngle) * 0.15f;
                aa_SetGyroCameraOffset(su_steerAngle * 1.5f, 0.0f);
            }
        }

        // ── Gyroscope: smooth angular velocity → integrate + correct ──
        if (su_gyroSensor && e.sensor.which == SDL_GetSensorID(su_gyroSensor))
        {
            Uint64 now = e.sensor.sensor_timestamp;
            if (su_lastGyroTs != 0 && now > su_lastGyroTs) {
                float dt = static_cast<float>(now - su_lastGyroTs) * 1e-9f;
                if (dt > 0.0f && dt < 0.25f) {
                    // data[2] = Z-axis angular velocity (steering = rotation
                    // around the axis pointing out of the screen).
                    float gyroRate = -e.sensor.data[2];
                    // Complementary filter: 98% gyro (smooth) + 2% accel (absolute).
                    constexpr float kAlpha = 0.98f;
                    su_steerAngle = kAlpha * (su_steerAngle + gyroRate * dt)
                                  + (1.0f - kAlpha) * su_accelAngle;
                    // Scale: ±90° tilt → ±135° camera.
                    aa_SetGyroCameraOffset(su_steerAngle * 1.5f, 0.0f);
                }
            }
            su_lastGyroTs = now;
        }
        break;
    // SDL3: SDL_KEYDOWN → SDL_EVENT_KEY_DOWN, keysym → direct key access
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        {
            if (e.key.repeat) break;

            info.push_back( uTransformEventInfo(
                                su_GetKeyInput().sdl_keys[ e.key.scancode ],
                                ( e.type == SDL_EVENT_KEY_DOWN ) ? 1 : 0 ) );

            break;
        }
#ifndef NOJOYSTICK
    // SDL3: SDL_JOYAXISMOTION → SDL_EVENT_JOYSTICK_AXIS_MOTION
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        {
            uJoystick * joystick = su_GetJoystick( e.jaxis.which );
            if(!joystick) break;
            int dir = e.jaxis.value > 0 ? 1 : 0;

            info.push_back( uTransformEventInfo(
                                joystick->GetAxis( e.jaxis.axis, 1-dir ),
                                0 ) );
            info.push_back( uTransformEventInfo(
                                joystick->GetAxis( e.jaxis.axis, dir ),
                                fabs( e.jaxis.value )/32768 ) );

            break;
        }
    // SDL3: SDL_JOYBUTTONDOWN → SDL_EVENT_JOYSTICK_BUTTON_DOWN
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    {
        uJoystick * joystick = su_GetJoystick( e.jbutton.which );
        if(!joystick) break;
        info.push_back( uTransformEventInfo(
                            joystick->GetButton( e.jbutton.button ),
                            ( e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ) ? 1 : 0 ) );
    }
    break;
    // SDL3: SDL_JOYHATMOTION → SDL_EVENT_JOYSTICK_HAT_MOTION
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        {
            info.reserve(4);

            uJoystick * joystick = su_GetJoystick( e.jhat.which );
            if(!joystick) break;
            int hat = e.jhat.hat;
            int hatDirection = e.jhat.value;

            // left/right hat motion
            {
                int & lastDir = joystick->GetHatDirection( hat, 0 );
                int newDir =
                    ( ( hatDirection & SDL_HAT_LEFT ) ? -1 : 0 ) +
                    ( ( hatDirection & SDL_HAT_RIGHT ) ? +1 : 0 );

                // negate previous events
                if ( lastDir < 0 && newDir >= 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Left ),
                                        0 ) );
                }

                if ( lastDir > 0 && newDir <= 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Right ),
                                        0 ) );
                }

                // create new events
                if ( lastDir >= 0 && newDir < 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Left ),
                                        1 ) );
                }

                if ( lastDir <= 0 && newDir > 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Right ),
                                        1 ) );
                }

                lastDir = newDir;
            }

            // up/down hat motion
            {
                int & lastDir = joystick->GetHatDirection( hat, 1 );
                int newDir =
                    ( ( hatDirection & SDL_HAT_UP ) ? -1 : 0 ) +
                    ( ( hatDirection & SDL_HAT_DOWN ) ? +1 : 0 );

                // negate previous events
                if ( lastDir < 0 && newDir >= 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Up ),
                                        0 ) );
                }

                if ( lastDir > 0 && newDir <= 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Down ),
                                        0 ) );
                }

                // create new events
                if ( lastDir >= 0 && newDir < 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Up ),
                                        1 ) );
                }

                if ( lastDir <= 0 && newDir > 0 )
                {
                    info.push_back( uTransformEventInfo(
                                        joystick->GetHat( hat, uJoystick::Down ),
                                        1 ) );
                }

                lastDir = newDir;
            }
        }
        break;
    // SDL3: SDL_JOYBALLMOTION → SDL_EVENT_JOYSTICK_BALL_MOTION
    case SDL_EVENT_JOYSTICK_BALL_MOTION:
        {
            uJoystick * joystick = su_GetJoystick( e.jball.which );
            if(!joystick) break;

            int ball = e.jball.ball;
            info.reserve(4);


            REAL xrel=e.jball.xrel;
            if (xrel > 0) // right
            {
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Left ),
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Right ),
                                    xrel,
                                    false ) );
            }

            if (xrel < 0) // left
            {
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Right ),
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Left ),
                                    -xrel,
                                    false ) );
            }

            // invert ball movement
            REAL yrel=-e.jball.yrel;
            if (yrel>0) // up
            {
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Down ),
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Up ),
                                    yrel,
                                    false ) );
            }
            if (yrel<0) // down
            {
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Up ),
                                    0,
                                    false ) );
                info.push_back( uTransformEventInfo(
                                    joystick->GetBall( ball, uJoystick::Down ),
                                    -yrel,
                                    false ) );
            }
        }
        break;
#endif
    default:
        break;
    }
}
#endif // DEDICATED


namespace
{
class Input_Comparator
{
public:
    static int Compare( const uAction* a, const uAction* b )
    {
        if ( a->priority < b->priority )
            return 1;
        else if ( a->priority > b->priority )
            return -1;
        return tString::CompareAlphaNumerical( a->internalName, b->internalName );
    }
};
}

#ifndef DEDICATED

static void s_InputConfigGeneric(int ePlayer, uAction *&actions,const tOutput &title){
    uMenu input_menu(title);

    uActionTooltip::Disable(ePlayer+1);

    uAction::Sort<Input_Comparator>(actions);

    std::vector< uMenuItemInput * > inputs;
    for ( uAction *a = actions; a; a = a->Next() )
    {
        if ( a->ShowInGenericConfigurationMenu() )
            inputs.push_back( new uMenuItemInput( &input_menu, a, ePlayer + 1 ) );
    }

    input_menu.ReverseItems();
    input_menu.Enter();

    for ( std::vector< uMenuItemInput * >::const_iterator it = inputs.begin(); it != inputs.end(); ++it )
        delete *it;
}

void su_InputConfig(int ePlayer){

    tOutput name;
    name.SetTemplateParameter(1, ePlayer+1);
    name.SetTemplateParameter(2, uPlayerPrototype::PlayerConfig(ePlayer)->Name());
    name << "$input_for_player";

    s_InputConfigGeneric(ePlayer,s_playerActions,name);
}

void su_InputConfigCamera(int player){

    tOutput name;
    name.SetTemplateParameter(1, uPlayerPrototype::PlayerConfig(player)->Name());
    name << "$camera_controls";

    s_InputConfigGeneric(player,s_cameraActions,name);
}

void su_InputConfigGlobal(){
    s_InputConfigGeneric(-1,s_globalActions,"$input_items_global");
}

#endif // !DEDICATED input configuration menus


REAL key_sensitivity=40;
static double lastTime=0;
static REAL ts=0;

static bool su_delayed = false;

void su_HandleDelayedEvents ()
{
    // nothing to do
    if ( !su_delayed )
    {
        return;
    }

    su_delayed = false;

    for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
    {
        uBind * bind = (*i)->GetBind();
        if ( bind )
        {
            bind->HanldeDelayed();
        }
    }
}

// Internal implementation using SDL_Event (will be refactored in future sprint)
static bool su_HandleEventInternal(SDL_Event &e, bool delayed ){
#ifndef DEDICATED
    if ( su_delayed && !delayed )
    {
        su_HandleDelayedEvents();
    }

    su_delayed = delayed;

    // transform events
    bool ret = false;

    std::vector< uTransformEventInfo > events;
    su_TransformEvent( e, events );

    for ( std::vector< uTransformEventInfo >::const_iterator i = events.begin(); i != events.end(); ++i )
    {
        uTransformEventInfo info = *i;

        if ( info.input )
        {
            // store input state for later repeat
            if ( info.needsRepeat )
            {
                info.input->SetPressed( info.value > 0 ? info.value : 0 );
            }

            // activate binding
            uBind * bind = info.input->GetBind();
            if ( bind )
            {
                if ( info.value > 0 && bind->IsDoubleBind( info.input ) )
                {
                    return true;
                }

                if ( info.needsRepeat && bind->act && bind->act->type == uAction::uINPUT_ANALOG )
                {
                    info.value *= ts*key_sensitivity;
                }

                ret |= bind->Activate( info.value, delayed );
            }
        }
    }

    return ret;
#endif
    return false;
}

// Platform-agnostic event handler (preferred API)
bool su_HandleEvent(const uEvent &e, bool delayed)
{
#ifndef DEDICATED
    // Convert uEvent back to SDL_Event for now
    // Future sprint will refactor su_TransformEvent to use uEvent directly
    SDL_Event sdlEvent;
    if (uEventSDL::ToSDLEvent(e, sdlEvent))
    {
        return su_HandleEventInternal(sdlEvent, delayed);
    }
#else
    (void)e;
    (void)delayed;
#endif
    return false;
}

// SDL event handler (backward compatibility)
bool su_HandleEvent(SDL_Event &e, bool delayed)
{
    return su_HandleEventInternal(e, delayed);
}

void su_InputSync(){
    double time=tSysTimeFloat();
    ts=REAL(time-lastTime);

    //static REAL tsSmooth=0;
    //tsSmooth+=REAL(ts*.1);
    //tsSmooth/=REAL(1.1);
    lastTime=time;

    // key repeat
    for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
    {
        uInput * input = *i;
        uBind * bind = input->GetBind();
        if ( input->GetPressed() > 0 && bind &&
                bind->act && bind->act->type == uAction::uINPUT_ANALOG )
        {
            bind->Activate( ts * key_sensitivity * input->GetPressed(), su_delayed );
        }
    }
}

void su_ClearKeys()
{
    // clears stored keypresses
    for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
    {
        uInput * input = *i;
        uBind * bind = input->GetBind();
        if ( input->GetPressed() > 0 && bind )
        {
            bind->Activate( -1, su_delayed );
            input->SetPressed( 0 );
        }
    }

#ifndef DEDICATED
    // Flush any pending text input events so the key that triggered a menu
    // (e.g. the console hotkey ~) doesn't land in the text field when the
    // menu opens. SDL3 does not flush these automatically.
    SDL_FlushEvent( SDL_EVENT_TEXT_INPUT );
#endif
}

// *****************
// Player binds
// *****************

static char const * Player_keyword="PLAYER_BIND";

uBindPlayer::uBindPlayer(uAction *a,int p):uBind(a),ePlayer(p){}

uBindPlayer::~uBindPlayer(){}

uBindPlayer * uBindPlayer::NewBind(std::istream &s)
{
    // read action
    std::string actionName;
    s >> actionName;
    uAction * act = uAction::Find( actionName.c_str() );

    // read player ID
    int player;
    s >> player;

    // delegate
    return NewBind( act, player );
}

uBindPlayer * uBindPlayer::NewBind( uAction * action, int player )
{
    // see if the bind has an alias
    for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
    {
        uInput * input = *i;
        uBind * old = input->GetBind();

        // compare action
        if ( old && old->act == action )
        {
            uBindPlayer * oldPlayer = dynamic_cast< uBindPlayer * >( old );
            if ( oldPlayer && oldPlayer->ePlayer == player )
                return oldPlayer;
        }
    }

    // no alias found, return new bind
    return tNEW(uBindPlayer)( action, player );
}

bool uBindPlayer::IsKeyWord(const char *n){
    return !strcmp(n,Player_keyword);
}

bool uBindPlayer::CheckPlayer(int p){
    return p==ePlayer;
}

void uBindPlayer::Write(std::ostream &s){
    s << Player_keyword << '\t';
    uBind::Write(s);
    s << ePlayer;
}

bool uBindPlayer::Delayable()
{
    return ( ePlayer!=0 );
}

bool uBindPlayer::DoActivate(REAL x){
    bool ret = false;
    if (ePlayer==0)
        ret = GlobalAct(act,x);
    else
        ret = uPlayerPrototype::PlayerConfig(ePlayer-1)->Act(act,x);

    if( ret && act && act->GetTooltip() && x > 0 )
    {
        act->GetTooltip()->Count(ePlayer);
    }

    return ret;
}


// *****************
// Global actions
// *****************

static uActionGlobalFunc *uActionGlobal_anchor=NULL;

uActionGlobalFunc::uActionGlobalFunc(uActionGlobal *a, ACTION_FUNC *f,
                                     bool rebind )
        :tListItem<uActionGlobalFunc>(uActionGlobal_anchor), func (f), act(a),
rebindable(rebind){}

bool uActionGlobalFunc::IsBreakingGlobalBind(uAction *act){
    for (uActionGlobalFunc *run = uActionGlobal_anchor; run ; run = run->Next())
        if (run->act == act && !run->rebindable)
            return true;

    return false;
}

bool uActionGlobalFunc::GlobalAct(uAction *act, REAL x){
    for (uActionGlobalFunc *run = uActionGlobal_anchor; run ; run = run->Next())
        if (run->act == act && run->func(x))
            return true;

    return false;
}

static uActionGlobal mess_up("MESS_UP",1);

static uActionGlobal mess_down("MESS_DOWN",2);

static uActionGlobal mess_end("MESS_END",3);

static bool messup_func(REAL x){
    if (x>0){
        sr_con.Scroll(-1);
    }
    return true;
}

static bool messdown_func(REAL x){
    if (x>0){
        sr_con.Scroll(1);
    }
    return true;
}

static bool messend_func(REAL x){
    if (x>0){
        sr_con.End(2);
    }
    return true;
}

static uActionGlobalFunc mu(&mess_up,&messup_func);
static uActionGlobalFunc md(&mess_down,&messdown_func);
static uActionGlobalFunc me(&mess_end,&messend_func);

// ********
// tooltips
// ********

uActionTooltip::Level su_helpLevel = uActionTooltip::Level_Expert;

uActionTooltip::uActionTooltip( Level level, uAction & action, int numHelp, VETOFUNC * veto )
: tConfItemBase(action.internalName + "_TOOLTIP")
  , action_( action )
  , veto_(veto)
  , level_( level )
{
    help_ = tString("$input_") + action.internalName + "_tooltip";
    tToLower( help_ );

    // initialize array holding the number of help attempts to give left
    for( int i = uMAX_PLAYERS; i >= 0; --i )
    {
        activationsLeft_[i] = 0; // numHelp;
    }

    action.tooltip_ = this;
}

uActionTooltip::~uActionTooltip()
{
    if( action_.tooltip_ == this )
        action_.tooltip_ = NULL;

}

bool uActionTooltip::Help( int player )
{
#ifndef DEDICATED
    if( rConsole::CenterDisplayActive() )
    {
        return false;
    }

    if(player < 0 || player > uMAX_PLAYERS)
        return false;

    // find most needed tooltip
    uActionTooltip * mostWanted{};

    // keys bound to the action of the tooltip that needs help
    tString maps;
    tString last;

    // run through binds
    for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
    {
        uBind * bind = (*i)->GetBind();
        if( !bind ||!bind->CheckPlayer(player) )
            continue;
        uAction * action = bind->act;
        if( !action )
            continue;
        uActionTooltip * tooltip = action->GetTooltip();
        if( !tooltip || ( tooltip->veto_ && (*tooltip->veto_)(player) ) || ( su_helpLevel < tooltip->level_ ) )
        {
            continue;
        }

        int activationsLeft = tooltip->activationsLeft_[player];
        if( activationsLeft > 0 &&
            ( !mostWanted || mostWanted->activationsLeft_[player] < activationsLeft ) )
        {
            mostWanted = tooltip;
            maps = "";
            last = "";
        }

        // build up key list
        if( mostWanted == tooltip )
        {
            if ( maps.Len() > 1 )
            {
                maps << ", ";
            }
            if ( last.Len() > 1 )
            {
                maps << last;
            }
            last = tString("<") + (*i)->Name() + ">";
        }
    }

    if( mostWanted )
    {
        // notice repeats, hint at how to silence them
        {
            static uActionTooltip * lastMostWanted{};
            static int identicalTooltipCount{};

            if(mostWanted == lastMostWanted)
            {
                identicalTooltipCount++;
                if(identicalTooltipCount >= 3)
                {
                    identicalTooltipCount-=2;
                    con.CenterDisplay(tString(tOutput("$tooltip_how_to_get_rid_of")));
                    return true;
                }
            }
            else
            {
                identicalTooltipCount = 0;
            }

            lastMostWanted = mostWanted;
        }


        if( last.Len() > 1 )
        {
            if( maps.Len() > 1 )
                maps << " " << tOutput("$input_or") << " " << last;
            else
                maps = last;
        }

        con.CenterDisplay(tString(tOutput(mostWanted->help_, maps)));

        return true;
    }
#endif
    return false;
}

void uActionTooltip::Disable(int player)
{
    if(player < 0 || player > uMAX_PLAYERS)
        return;

    // run through binds
    for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
    {
        uBind * bind = (*i)->GetBind();
        if( !bind ||!bind->CheckPlayer(player) )
            continue;
        uAction * action = bind->act;
        if( !action )
            continue;
        uActionTooltip * tooltip = action->GetTooltip();
        if( !tooltip )
        {
            continue;
        }

        tooltip->activationsLeft_[player] = 0;
    }
}

void uActionTooltip::Count( int player )
{
    if ( activationsLeft_[player] > 0 )
    {
        activationsLeft_[player]--;
        Help(player);
    }
}

//! call to show the tooltip one more time
void uActionTooltip::ShowAgain()
{
    for( int i = uMAX_PLAYERS; i >= 0; --i )
    {
        if( 0 == activationsLeft_[i] )
        {
            activationsLeft_[i] = 1;
        }
    }
}

void uActionTooltip::WriteVal(std::ostream & s )
{
    for( int i = 0; i <= uMAX_PLAYERS; ++i )
    {
        s << activationsLeft_[i] << " ";
    }
}

void uActionTooltip::ReadVal(std::istream & s )
{
    for( int i = 0; i <= uMAX_PLAYERS; ++i )
    {
        s >> activationsLeft_[i];
    }
}

// *****************************************************
//  Menuitem for input selection (graphical client only)
// *****************************************************

#ifndef DEDICATED

uMenuItemInput::uMenuItemInput(uMenu *M,uAction *a,int p)
    :uMenuItem(M,a->helpText),act(a),ePlayer(p),active(0)
{
}

void uMenuItemInput::Render(REAL x,REAL y,REAL alpha,bool selected)
{
    DisplayText(REAL(x-.02),y,act->description,selected,alpha,1);

    if (active)
    {
        tString s;
        s << tOutput("$input_press_any_key");
        DisplayText(REAL(x+.02),y,s,selected,alpha,-1);
    }
    else
    {
        tString s;

        bool first=1;

        for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
        {
            uBind * bind = (*i)->GetBind();
            if ( bind && (*i)->Name().size() > 0 &&
                    bind->act==act &&
                    bind->CheckPlayer(ePlayer) )
            {
                if (!first)
                    s << ", ";
                else
                    first=0;

                s << (*i)->Name();
            }
        }
        if (!first)
        {
            DisplayText(REAL(x+.02),y,s,selected,alpha,-1);
        }
        else
        {
            DisplayText(REAL(x+.02),y,tOutput("$input_items_unbound"),selected,alpha,-1);
        }
    }
}

void uMenuItemInput::Enter()
{
    active=1;
}


bool uMenuItemInput::Event(SDL_Event &e)
{
    // SDL3: SDL_KEYDOWN → SDL_EVENT_KEY_DOWN, keysym removed
    if ( e.type == SDL_EVENT_KEY_DOWN )
    {
        if (!active)
        {
            if (e.key.key==SDLK_DELETE || e.key.key==SDLK_BACKSPACE)
            {
                // clear all bindings
                for ( uInputs::const_iterator i = su_inputs.begin(); i != su_inputs.end(); ++i )
                {
                    uBind * bind = (*i)->GetBind();
                    if ( bind &&
                            bind->act==act &&
                            bind->CheckPlayer(ePlayer) )
                    {
                        (*i)->SetBind( NULL );
                    }
                }
                return true;
            }
            return false;
        }

        // ignore escape
        if ( e.key.key == SDLK_ESCAPE )
        {
            return false;
        }
    }

    // transform events
    std::vector< uTransformEventInfo > events;
    su_TransformEvent( e, events );

    for ( std::vector< uTransformEventInfo >::const_iterator i = events.begin(); i != events.end(); ++i )
    {
        uTransformEventInfo const & info = *i;
        if ( info.input && info.value > 0.5 && active )
        {
            uBind * bind = info.input->GetBind();
            if ( bind &&
                    bind->act==act &&
                    bind->CheckPlayer(ePlayer))
            {
                info.input->SetBind( NULL );
            }
            else
            {
                info.input->SetBind( uBindPlayer::NewBind(act,ePlayer) );
            }

            active = false;
            return true;
        }
    }
    return false;
}

#endif // !DEDICATED uMenuItemInput methods
