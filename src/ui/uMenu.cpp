/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)
Copyright (C) 2004  Armagetron Advanced Team (http://sourceforge.net/projects/armagetronad/)

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
#   ifdef MACOSX_XCODE
#       include "uOSXPaste.h"
#       include <CoreFoundation/CoreFoundation.h>
#   elif !defined(MACOSX)
#       include "scrap.h"
#   endif
#endif

#include "tSysTime.h"
#include "uMenu.h"
#include <cmath>
#include "rSysdep.h"
#ifndef DEDICATED
#include "rFrameLifecycle.h"
#endif
#include "rScreen.h"
#include "rViewport.h"
#include "rTexture.h"
#include "tRecorder.h"
#include "tString.h"
#include "math.h"
#include "uInputQueue.h"
#include "rConsole.h"
#include "uInput.h"
#include "tDirectories.h"
//#include "tRecording.h"
#include "tToDo.h"
#include "tException.h"
#include "tRectangle.h"
#include <iterator>

#include "utf8.h"

#ifndef DEDICATED
#include "rRender.h"
#include "rSDL.h"
#include "rRenderQueue.h"
#include "rVertex.h"
#endif

#include <vector>

FUNCPTR  uMenu::idle(NULL);

bool uMenu::wrap=true;
uMenu::QuickExit uMenu::quickexit=uMenu::QuickExit_Off;
bool uMenu::exitToMain=false;

// *****************************************************

#ifdef SLOPPYLOCALE
uMenu::uMenu(const char *t="",bool exit_item)
        :exitFlag(0),spaceBelow(.4),title(t),blinkTime_(0){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 10000000;
}
#endif

uMenu::uMenu(const tOutput &t,bool exit_item)
        :exitFlag(0),spaceBelow(.4),title(t),blinkTime_(0){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 100000000;
}

uMenu::~uMenu(){
    if( selected >=0 && selected < items.Len() ) items[selected]->Deselect();
    for (int i=items.Len()-1;i>=0;i--)
        delete items[i];
}

void uMenu::ReverseItems(){
    tList<uMenuItem> dummy;
    dummy.Swap( items );

    for (int i=dummy.Len()-1; i>=0; i--){
        uMenuItem *x = dummy[i];
        dummy.Remove(x, x->idnum);
        items.Add  (x, x->idnum);
    }
}

//static REAL text_height=rCHEIGHT_NORMAL;
//static REAL text_width=rCWIDTH_NORMAL;

static REAL text_height_base=.11;

// Effective text height, scaled for touch-friendly menus on small screens
static REAL sr_MenuTextHeight()
{
    return text_height_base * sr_TouchUIScale();
}

#ifndef DEDICATED
static REAL titlefac=1.2;
#endif
int menuentries=0;

REAL uMenu::YPos(int num){
    return yOffset - sr_MenuTextHeight() * (menuentries-num);
}

// Convert normalized touch Y [0=top, 1=bottom] to the nearest item index.
// Inverse of YPos(): YPos(i) = yOffset - th*(menuentries-i)
int uMenu::TouchYToItem(float touchY) const {
    float menuY = 1.0f - 2.0f * touchY;   // touch Y+ down → menu Y+ up
    REAL th = sr_MenuTextHeight();
    int idx = (int)roundf(menuentries - (yOffset - menuY) / th);
    if (idx < 0) idx = 0;
    if (idx >= items.Len()) idx = items.Len() - 1;
    return idx;
}


#ifndef DEDICATED
static inline void arrow(REAL x,REAL y,REAL dy,REAL size){
    if (sr_glOut){
        // Get current color from renderer (array format)
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer->GetColor(color);
        uint8_t cr = static_cast<uint8_t>(color[0] * 255.0f);
        uint8_t cg = static_cast<uint8_t>(color[1] * 255.0f);
        uint8_t cb = static_cast<uint8_t>(color[2] * 255.0f);
        uint8_t ca = static_cast<uint8_t>(color[3] * 255.0f);

        // Arrow vertices in order
        float pts[7][2] = {
            {static_cast<float>(x), static_cast<float>(y+2*dy*size)},
            {static_cast<float>(x+size), static_cast<float>(y)},
            {static_cast<float>(x+.3f*size), static_cast<float>(y)},
            {static_cast<float>(x+.3f*size), static_cast<float>(y-2*dy*size)},
            {static_cast<float>(x-.3f*size), static_cast<float>(y-2*dy*size)},
            {static_cast<float>(x-.3f*size), static_cast<float>(y)},
            {static_cast<float>(x-size), static_cast<float>(y)}
        };

        // Convert line loop to individual line segments
        std::vector<rVertex20> vertices;
        vertices.reserve(14);  // 7 lines * 2 vertices
        for (int i = 0; i < 7; ++i) {
            int j = (i + 1) % 7;
            vertices.push_back(rVertex20(pts[i][0], pts[i][1], 0.0f, cr, cg, cb, ca, 0.0f, 0.0f));
            vertices.push_back(rVertex20(pts[j][0], pts[j][1], 0.0f, cr, cg, cb, ca, 0.0f, 0.0f));
        }

        rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitLines(rRenderPhase::HUD, state, vertices.data(), vertices.size());
    }
}
#endif

static bool s_globalRepeat = false;

#ifndef DEDICATED
static bool disphelp=false;
static REAL lastkey;
#endif

// inhibit console newline display while in a menu, it causes flickering
static bool su_inMenu = false;
bool uMenu::MenuActive()
{
    return su_inMenu;
}
static rNoAutoDisplayAtNewlineCallback su_noNewline( uMenu::MenuActive );
// static rSmallConsoleCallback su_smallConsole( su_InMenu );

void uMenu::OnEnter(){
#ifndef DEDICATED
    bool localRepeat = false;
    float nextrepeat = 0.0f;
    static const float repeatdelay = 0.2f;
    static const float repeatrateStart  = 0.2f;
    static const float repeatrateMin  = 0.05f;
    static float repeatrate  = repeatrateStart;
    SDL_Event tEventRepeat;
#else
    return;
#endif

    // delete stuck keys, maybe a menu item catches key release events.
    su_ClearKeys();

    uCallbackMenuEnter::MenuEnter();
    su_inMenu = true;

    if (items.Len()<=0)
        return;

    exitFlag=0;
    yOffset=menuTop;
    REAL lastt=0;
    REAL ts=0;
    bool snapScroll = false;

#ifndef DEDICATED
    lastkey=tSysTimeFloat();
    static const REAL timeout=.5;
#endif
    if( selected >= items.Len() )
    {
        // skip to actually selectable item
        bool wrapBack = wrap;
        wrap = true;
        selected = GetPrevSelectable(0);
        if( selected >=0 && selected < items.Len() ) items[selected]->Select();
        wrap = wrapBack;
    }
    while (!exitFlag && !quickexit && !exitToMain){
        st_DoToDo();
        tAdvanceFrame();

        ts=tSysTimeFloat()-lastt;
        lastt=tSysTimeFloat();
        if (ts>.2) ts=.2;
        blinkTime_ += ts;

        if(snapScroll)
        {
            if(ts * 30 < 1)
                snapScroll = false;
        }
        else
        {
            if(ts * 15 > 1)
                snapScroll = true;
        }
        auto scrollBy = [this, snapScroll, ts](REAL delta)
        {
            if(snapScroll || fabsf(delta) < 1E-6)
            {
                yOffset += delta;
            }
            else
            {
                // almost standard exponential decay; the proximity factor makes it
                // approach the target position like t -> t^2 for negative t
                REAL proximity = std::min(1.0f, 10.0f * sqrtf(fabsf(delta)));
                REAL speed = std::min(1.0f, ts * 6 / proximity);
                yOffset += speed * delta;
            }
        };

        menuentries=items.Len();

        // clamp cursor
        if (selected < 0 )
            selected = 0;
        if ( selected >= items.Len())
            selected = items.Len()-1;

#ifndef DEDICATED
        {
            SDL_Event tEvent;
            uInputProcessGuard inputProcessGuard;
            while (!exitFlag && !quickexit && !exitToMain && su_GetSDLInput(tEvent))
            {
                REAL entertime = tSysTimeFloat();

                switch (tEvent.type)
                {
                case SDL_EVENT_KEY_DOWN:
                    if ( tEvent.key.key == SDLK_UNKNOWN )
                    {
                        // don't repeat unknown syms. They come from multi-key compositions and
                        // don't send keyup events when released.
                        break;
                    }
                    localRepeat = s_globalRepeat = true;
                    memcpy( &tEventRepeat, &tEvent, sizeof( SDL_Event ) );
                    nextrepeat = tSysTimeFloat() + repeatdelay;
                    break;
                case SDL_EVENT_KEY_UP:
                    if ( ( tEvent.key.key == SDLK_LEFT || tEvent.key.key == SDLK_RIGHT )
                         && selected >= 0 && selected < items.Len() )
                    {
                        items[selected]->LeftRightRelease();
                    }
                    localRepeat = s_globalRepeat = false;
                    repeatrate = repeatrateStart;
                    break;
                }

                this->HandleEvent( tEvent );

                // quit shortcut
                if ( quickexit )
                    break;

                if ( tSysTimeFloat() - entertime > 1 )
                {
                    localRepeat = s_globalRepeat = false;
                }
            }

            if ( localRepeat && s_globalRepeat && tSysTimeFloat() > nextrepeat )
            {
                this->HandleEvent( tEventRepeat );
                nextrepeat = tSysTimeFloat() + repeatrate;
                repeatrate *= .71;
                if ( repeatrate < repeatrateMin )
                    repeatrate = repeatrateMin;
            }
        }

        // we're about to render, last chance to make changes to the menu
        OnRender();

        // clamp cursor
        if (selected < 0 )
            selected = 0;
        if ( selected >= items.Len())
            selected = items.Len()-1;
#endif
        // quit shortcut
        if ( quickexit )
            break;


        menuBot=-1+spaceBelow;

        const REAL border=.3;
        const REAL smallborder=.1;

        menuentries=items.Len();

        REAL ysel=YPos(selected);

        {
            REAL scrollUp = menuBot+border-ysel;
            if(scrollUp > 0)
                scrollBy(scrollUp);
        }
        {
            REAL scrollDown = menuTop-border-ysel;
            if(scrollDown < 0)
                scrollBy(scrollDown);
        }

        if (ysel<menuBot)
            yOffset+=(menuBot-ysel);

        if (ysel>menuTop-smallborder)
            yOffset+=(menuTop-smallborder-ysel);

        if (YPos(0)>menuBot+smallborder)
            yOffset+=menuBot+smallborder-YPos(0);

        if (YPos(menuentries-1)<menuTop-smallborder)
            yOffset+=menuTop-smallborder-YPos(menuentries-1);

#ifndef DEDICATED
        // Render menu with full frame lifecycle
        rRenderFrame([&]() {
            sr_ResetRenderState(true);
            items[selected]->RenderBackground();

            if (selected >= items.Len()) selected = items.Len()-1;
            if (items.Len() <= 0)
                return;

            if (sr_glOut && !exitFlag && !quickexit){
                REAL blinkAlpha = 0.7f + 0.3f * sinf(blinkTime_ * 6.0f);
                items[selected]->Render(center,YPos(selected),blinkAlpha,true);

                for (int i=items.Len()-1;i>=0;i--)
                    if (i!=selected){
                        REAL y=YPos(i);
                        REAL alpha=1;
                        const REAL b=.1;
                        if (y<menuBot+b)
                            alpha=(y-menuBot)/b;
                        if (y>menuTop-b)
                            alpha=(menuTop-y)/b;
                        if (y>menuBot && y<menuTop)
                        {
                            rTextField::SetDefaultColor( tColor(1,1,1,1) );
                            rTextField::SetBlendColor( tColor(1,1,1,1) );
                            items[i]->Render(center,y,alpha,false);
                        }
                    }

                rTextField::SetDefaultColor( tColor(1,1,1,1) );
                rTextField::SetBlendColor( tColor(1,1,1,1) );

                Color(.6,.6,1,1);
                ::DisplayText(0,menuTop+sr_MenuTextHeight()*titlefac
                              ,sr_MenuTextHeight()*titlefac,
                              title,sr_fontMenuTitle,0);

                RenderDisableState(rCapability::Texture2D);
                Color(1,.2,.2,.5);
                if (YPos(0)<menuBot+smallborder && (int(tSysTimeFloat()))%2)
                    arrow(.9,menuBot+.1,-1,.05);
                if (YPos(menuentries-1)>menuTop && (int(tSysTimeFloat())+1)%2)
                    arrow(.9,menuTop,1,.05);

                REAL helpAlpha = tSysTimeFloat()-lastkey-timeout;
                if( helpAlpha > 1 )
                {
                    helpAlpha = 1;
                }

                disphelp = helpAlpha > 0;
                if ( items[selected]->DisplayHelp( disphelp, menuBot, helpAlpha ) )
                {
                    if (sr_alphaBlend)
                        Color(1,.8,.8, helpAlpha);
                    else
                        Color(helpAlpha,
                              .8*helpAlpha,
                              .8*helpAlpha);

                    rTextField c(-.95f,menuBot-.04f,rCHEIGHT_NORMAL, sr_fontMenu);
                    c.SetWidth(1.9f-items[selected]->SpaceRight());
                    c.EnableLineWrap();
                    c << items[selected]->Help();
                }
            }
            else if ( !sr_glOut )
            {
                tDelay( 10000 );
            }
        });
#endif
    }

    s_globalRepeat = false;

    uCallbackMenuLeave::MenuLeave();
    su_inMenu = false;
}

void uMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    if (!items[selected]->Event(event))
    {
        // int newSelected = -1;
        switch (event.type){
        case SDL_EVENT_KEY_DOWN:
        {
            if (!disphelp)
                lastkey=tSysTimeFloat();
            switch (event.key.key){

            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                Exit();
                break;

                case(SDLK_UP):
                    lastkey=tSysTimeFloat();
                    SetSelected(GetNextSelectable(selected));
                    break;
                case(SDLK_DOWN):
                    lastkey=tSysTimeFloat();
                    SetSelected(GetPrevSelectable(selected));
                    break;

            case(SDLK_LEFT):
                items[selected]->LeftRight(-1);
                break;
            case(SDLK_RIGHT):
                items[selected]->LeftRight(1);
                break;

            case(SDLK_SPACE):
                        case(SDLK_KP_ENTER):
                            case(SDLK_RETURN):
                                    s_globalRepeat = false;
                try
        {
                    su_inMenu = false;
                    items[selected]->Enter();
                }
                catch (tException const & e)
                {
                    uMenu::SetIdle(NULL);

                    // inform user of generic errors
                    tConsole::Message( e.GetName(), e.GetDescription(), 20 );
                }
#ifdef _MSC_VER
#pragma warning ( disable : 4286 )
                // GRR. Visual C++ dones not handle generic exceptions with the above general statement.
                // A specialized version is needed. The best part: it warns about the code below being redundant.
                catch ( tGenericException const & e )
                {
                    try
                    {
                        tConsole::Message( e.GetName(), e.GetDescription(), 20 );
                    }
                    catch (...)
                    {
                    }
                }
#endif

                su_inMenu = true;

                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                break;

            default:
                // let the input subsystem handle events for later processing
                su_HandleEvent( event, true );
                break;
            }
        }
        break;

        // Touch navigation — active when ENABLE_TOUCH > 0
        //
        // Two modes determined by where the touch starts:
        //   Navigation mode (touchStartedOnSelected_=false):
        //     touch started on a NON-selected item → vertical slide scrolls selection
        //   Value-change mode (touchStartedOnSelected_=true):
        //     touch started on the ALREADY-selected item → vertical slide changes value,
        //     tap calls Enter() (or shows keyboard for text inputs)
        case SDL_EVENT_FINGER_DOWN:
            if (su_GetEnableTouch() > 0) {
                float ty = event.tfinger.y;
                // Ignore touches well outside the menu's vertical bounding box
                float menuTopTouch = (1.0f - menuTop) / 2.0f;
                float menuBotTouch = (1.0f - menuBot) / 2.0f;
                if (ty < menuTopTouch - 0.05f || ty > menuBotTouch + 0.05f) break;

                int idx = TouchYToItem(ty);
                touchStartedOnSelected_ = (idx == selected &&
                                           selected >= 0 && selected < items.Len());
                touchFingerId_ = event.tfinger.fingerID;
                touchStartX_   = touchLastX_ = event.tfinger.x;
                touchStartY_   = touchLastY_ = ty;
                touchMoved_    = false;
                // Always update selection on touch down
                SetSelected(idx);
            }
            break;

        case SDL_EVENT_FINGER_MOTION:
            if (su_GetEnableTouch() > 0 && event.tfinger.fingerID == touchFingerId_) {
                float dy = event.tfinger.y - touchLastY_;

                if (!touchStartedOnSelected_) {
                    // Navigation mode: vertical slide scrolls through items.
                    // TouchYToItem clamps to [0, items.Len()-1] so sliding past the
                    // top/bottom naturally pins to the first/last item.
                    int newIdx = TouchYToItem(event.tfinger.y);
                    if (newIdx != selected) {
                        touchMoved_ = true;
                        SetSelected(newIdx);
                    }
                } else {
                    // Value-change mode: vertical slide changes value one step per 0.04 screen.
                    // Slide up (dy < 0) = previous value; slide down (dy > 0) = next value.
                    if (fabsf(dy) > 0.04f && selected >= 0 && selected < items.Len()) {
                        touchMoved_ = true;
                        items[selected]->LeftRight(dy > 0 ? 1 : -1);
                        touchLastY_ = event.tfinger.y;  // reset anchor for continuous stepping
                    }
                }
                touchLastX_ = event.tfinger.x;
                if (touchStartedOnSelected_) {
                    // touchLastY_ already updated above when a step fired
                } else {
                    touchLastY_ = event.tfinger.y;
                }
            }
            break;

        case SDL_EVENT_FINGER_UP:
            if (su_GetEnableTouch() > 0 && event.tfinger.fingerID == touchFingerId_) {
                // Tap with no slide: Enter() if on the same item
                if (!touchMoved_ && selected >= 0 && selected < items.Len()) {
                    int upIdx = TouchYToItem(event.tfinger.y);
                    if (upIdx == selected) {
                        su_inMenu = false;
                        try { items[selected]->Enter(); }
                        catch (tException const& e) { tConsole::Message(e.GetName(), e.GetDescription(), 20); }
                        su_inMenu = true;
                        s_globalRepeat = false;
                    }
                }
                touchFingerId_ = -1;
            }
            break;

        default:
            // let the input subsystem handle events for later processing
            su_HandleEvent( event, true );
            break;
        }
    }

    su_inMenu = true;
#endif
}

// select the menu item below "start"
int uMenu::GetPrevSelectable(int start)
{
    int prev = start-1;
    while (prev!=start)
    {
        if (prev<0)
        {
            if (wrap)
                prev = items.Len()-1;
            else
                break;
        }
        if (items[prev]->IsSelectable())
        {
            return prev;
        }
        prev--;
    }
    return start;
}

#ifndef DEDICATED
static bool s_idleBackground = false;
#endif

void uMenu::SetSelected( int s )
{
    if( selected >= 0 && selected < items.Len() ) items[selected]->Deselect();
    selected = s;
    if( selected >= 0 && selected < items.Len() ) items[selected]->Select();
}

// select the menu item above "start"
int uMenu::GetNextSelectable(int start)
{
    int next = start+1;
    while (next!=start)
    {
        if (next>=items.Len())
        {
            if (this->wrap)
                next = 0;
            else
                break;
        }
        if (items[next]->IsSelectable())
        {
            return next;
        }
        next++;
    }
    return start;
}


// paints a nice background
void uMenu::GenericBackground(REAL top){
#ifndef DEDICATED
    if (idle)
    {
        s_idleBackground = true;

        try
        {
            // throw tGenericException("test"); // (test exception throw to see if error handling works right)
            (*idle)();

            // render the console so it appears behind the menu
            if( sr_con.autoDisplayAtSwap )
            {
                sr_con.Render();
            }

            // fade everything rendered so far to black
            if( sr_alphaBlend && sr_chatLayer > 0 )
            {
                sr_ResetRenderState(true);

                double time = tSysTimeFloat();
                static double lastTime = time - 100;
                static REAL alpha = 0.0f;
                double timePassed = time - lastTime;
                if( time - lastTime > 1.0 )
                {
                    alpha = 0.0f;
                }
                else
                {
                    alpha += timePassed;
                    REAL limit = sr_chatLayer;

                    if( alpha > limit )
                    {
                        alpha = limit;
                    }
                }
                lastTime = time;

                // Batch render fade overlay quad
                uint8_t alphaB = static_cast<uint8_t>(alpha * 255.0f);
                std::vector<rVertex20> fadeVerts;
                fadeVerts.reserve(6);
                // Triangle 1: bottom-left, bottom-right, top-right
                fadeVerts.push_back(rVertex20(-1.0f, -1.0f, 0.0f, 0, 0, 0, alphaB, 0.0f, 0.0f));
                fadeVerts.push_back(rVertex20(1.0f, -1.0f, 0.0f, 0, 0, 0, alphaB, 0.0f, 0.0f));
                fadeVerts.push_back(rVertex20(1.0f, top, 0.0f, 0, 0, 0, alphaB, 0.0f, 0.0f));
                // Triangle 2: bottom-left, top-right, top-left
                fadeVerts.push_back(rVertex20(-1.0f, -1.0f, 0.0f, 0, 0, 0, alphaB, 0.0f, 0.0f));
                fadeVerts.push_back(rVertex20(1.0f, top, 0.0f, 0, 0, 0, alphaB, 0.0f, 0.0f));
                fadeVerts.push_back(rVertex20(-1.0f, top, 0.0f, 0, 0, 0, alphaB, 0.0f, 0.0f));

                rRenderStateKey fadeState = rRenderStateKey::Colored(rBlendMode::Alpha);
                rRenderQueue::Instance().Submit(rRenderPhase::HUD, fadeState, fadeVerts.data(), fadeVerts.size());
                rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
            }
        }
        catch ( ... )
        {
            s_idleBackground = false;

            // the idle background function is broken. Disable it and rethrow.
            idle = 0;
            throw;
        }
        s_idleBackground = false;
    }
    else if (sr_glOut){
        uCallbackMenuBackground::MenuBackground();
    }
    else
        tDelay(100000);
#endif
    sr_ResetRenderState(true);
}

// marks the menu for exit
void uMenu::OnExit(){
    if( selected >=0 && selected < items.Len() ) items[selected]->Deselect();
    exitFlag=1;
}

//! called every frame before the menu is rendered
void uMenu::OnRender()
{
}

// *****************************************************

// *******************************************************************************************
// *
// *   SetColor
// *
// *******************************************************************************************
//!
//!        @param  selected    flag indicating whether the menu item is currently selected
//!        @param  alpha       transparency to use
//!
// *******************************************************************************************

void uMenuItem::SetColor( bool selected, REAL alpha )
{
    //   rTextField::SetBlendColor( tColor(.8+.2*sin(time),.3-.1*sin(time),.3-.1*sin(time),alpha) );
    rTextField::SetDefaultColor( tColor(1,1,1,alpha) );

    if (selected)
    {
        // Map blinkAlpha [0.4, 1.0] (center 0.7) to intensity [0.7, 1.3] (center 1.0).
        // This keeps the same visual effect as the old 1+0.3*sin(time*10) but driven
        // by the menu's blinkTime_ accumulator instead of a competing raw clock.
        REAL intensity = 1.0f + (alpha - 0.7f);
        rTextField::SetDefaultColor( tColor(.8f,.3f,.3f,1.0f) );
        rTextField::SetBlendColor( tColor(intensity,intensity,intensity,1.0f) );
    }
}

void uMenuItem::DisplayText(REAL x,REAL y,const char *text,
                            bool selected,REAL alpha,
                            int center,int c,int cp, rTextField::ColorMode colorMode, float maxWidth ){
#ifndef DEDICATED
    if (sr_glOut){
        SetColor( selected, alpha );

        REAL th = sr_MenuTextHeight();
        
        REAL availw = 1.9f;
        if (center < 0) availw = (.9f-x);
        if (center > 0) availw = (x + .9f);
        if (availw > maxWidth) availw = maxWidth;

        float usedwidth=rTextField::GetTextLength(tString(text), th, colorMode == rTextField::COLOR_USE);
        if (usedwidth > availw)
        {
            th *= availw/(usedwidth);
        }

        ::DisplayText(x,y,th,text,sr_fontMenu,center,c,cp, colorMode);
    }
#endif
}

void uMenuItem::DisplayTextSpecial(REAL x,REAL y,const char *text,
                                   bool selected,
                                   REAL alpha,int center){
    DisplayText(x,y,text,selected,alpha,center);
}

// *************************************

const tOutput& uMenuItemExit::ExitText()
{
    static tOutput exitText("$menuitem_exit_text");

    return exitText;
}

const tOutput& uMenuItemExit::ExitHelp()
{
    static tOutput exitHelp("$menuitem_exit_help");

    return exitHelp;
}

// *************************************

void uMenuItemToggle::NewChoice(uSelectItem<bool> *){}
void uMenuItemToggle::NewChoice(const char *,bool ){}

#ifdef SLOPPYLOCALE
uMenuItemToggle::uMenuItemToggle(uMenu *m,
                                 const char *tit,
                                 const char *help,
                                 bool &targ)
        :uMenuItemSelection<bool>(m,tit,help,targ){
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_on","",true);
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_off","",false);
}
#endif

uMenuItemToggle::uMenuItemToggle(uMenu *m,
                                 const tOutput& tit,
                                 const tOutput& help,
                                 bool &targ)
        :uMenuItemSelection<bool>(m,tit,help,targ){
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_on","",true);
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_off","",false);
}

uMenuItemToggle::~uMenuItemToggle(){}

void uMenuItemToggle::LeftRight(int){
    select=1-select;
    *target=!(*target);
}

void uMenuItemToggle::Enter(){
    LeftRight(0);
}
// *****************************************
//               Integer Choose
// *****************************************

#ifdef SLOPPYLOCALE
uMenuItemInt::uMenuItemInt
(uMenu *m,const char *tit,const char *help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}
#endif

uMenuItemInt::uMenuItemInt
(uMenu *m,const tOutput &tit,const tOutput &help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}


void uMenuItemInt::LeftRight(int dir){
    target+=dir*Step;
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}

void uMenuItemInt::Render(REAL x,REAL y,REAL alpha,
                          bool selected){
    DisplayText(x-.02,y,title,selected,alpha,1);

    tString s;
    s << target;
    DisplayText(x+.02,y,s,selected,alpha,-1);
}

// *****************************************
//               Float Choose
// *****************************************

#ifdef SLOPPYLOCALE
uMenuItemReal::uMenuItemReal
(uMenu *m,const char *tit,const char *help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}
#endif

uMenuItemReal::uMenuItemReal
(uMenu *m,const tOutput &tit,const tOutput &help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}


void uMenuItemReal::LeftRight(int dir){
    target+=dir*Step;
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}

void uMenuItemReal::Render(REAL x,REAL y,REAL alpha,
                          bool selected){
    DisplayText(x-.02,y,title,selected,alpha,1);

    tString s;
    s << target;
    DisplayText(x+.02,y,s,selected,alpha,-1);
}


// *****************************************************

uMenuItemString::uMenuItemString(uMenu *M,
                                 const tOutput& de,
                                 const tOutput& help,
                                 tString &c,
                                 int maxLength )
        :uMenuItem(M,help),description(de),content(&c),realCursorPos(0), maxLength_( maxLength ){
    // int len=content->Len();
    // if (len==0 || (*content)(len-1)!=0)
    //    (*content)[len]=0;
    realCursorPos=content->size();
    colorMode_ = rTextField::COLOR_SHOW;
}

void uMenuItemString::Render(REAL x,REAL y,
                             REAL alpha,bool selected){
#ifndef DEDICATED
    static int counter=0;
    counter++;

    int cmode=0;
    if (selected){
        cmode=1;
        if (counter & 32) cmode=2;
    }

    // unslected items with COLOR_SHOW should be rendered with COLOR_USE
    rTextField::ColorMode colorMode = colorMode_;
    if ( colorMode == rTextField::COLOR_SHOW && !selected )
        colorMode = rTextField::COLOR_USE;

    DisplayText(x-.02,y,description,selected,alpha,1);
    DisplayText(x+.02,y,*content,selected,alpha,-1,cmode,realCursorPos,colorMode);
#endif
}

bool uMenuItemString::Event(SDL_Event &e){
#ifndef DEDICATED
    bool ret =  false;
    if (e.type==SDL_EVENT_KEY_DOWN) {
        ret=true;
        // SDL3: keysym structure removed, access key/mod directly
        SDL_Keymod mod = e.key.mod;
        SDL_Keycode sym = e.key.key;
        bool moveWordLeft, moveWordRight, deleteWordLeft, deleteWordRight, moveBeginning, moveEnd, killForwards;
        moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards = false;

#if defined (MACOSX)
        // For moving over/deleting words
        if (mod & SDL_KMOD_ALT) {
            if (sym == SDLK_LEFT) {
                moveWordLeft = true;
            }
            else if (sym == SDLK_RIGHT) {
                moveWordRight = true;
            }
            else if (sym == SDLK_DELETE) {
                deleteWordRight = true;
            }
            else if (sym == SDLK_BACKSPACE) {
                deleteWordLeft = true;
            }
        }
        // For moving to extremes of the line
        else if (mod & SDL_KMOD_GUI) {
            if (sym == SDLK_LEFT) {
                moveBeginning = true;
            }
            else if (sym == SDLK_RIGHT) {
                moveEnd = true;
            }
        }
        // Linux and Windows
#else
        // Word operations
        if (mod & SDL_KMOD_CTRL) {
            if (sym == SDLK_LEFT) {
                moveWordLeft = true;
            }
            else if (sym == SDLK_RIGHT) {
                moveWordRight = true;
            }
            else if (sym == SDLK_DELETE) {
                deleteWordRight = true;
            }
            else if (sym == SDLK_BACKSPACE) {
                deleteWordLeft = true;
            }
        }
        else if (sym == SDLK_HOME) {
            moveBeginning = true;
        }
        else if (sym == SDLK_END) {
            moveEnd = true;
        }
#endif
        // "bash" keys
        if (mod & SDL_KMOD_CTRL) {
            if (sym == SDLK_A) {
                moveBeginning = true;
            }
            else if (sym == SDLK_E) {
                moveEnd = true;
            }
            else if (sym == SDLK_K) {
                killForwards = true;
            }
        }
        // moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards

        if (moveWordLeft) {
            realCursorPos += content->PosWordLeft(realCursorPos);
        }
        else if (moveWordRight) {
            realCursorPos += content->PosWordRight(realCursorPos);
        }
        else if (deleteWordLeft) {
            realCursorPos += content->RemoveWordLeft(realCursorPos);
        }
        else if (deleteWordRight) {
            content->RemoveWordRight(realCursorPos);
        }
        else if (moveBeginning) {
            realCursorPos = 0;
        }
        else if (moveEnd) {
            realCursorPos = content->size();
        }
        else if (killForwards) {
            content->RemoveSubStr(realCursorPos,content->size()-realCursorPos);
        }
        else if (sym == SDLK_LEFT) {
            if (realCursorPos > 0) {
                while(((*content)[--realCursorPos]&0xc0) == 0x80) ;
            }
        }
        else if (sym == SDLK_RIGHT) {
            if ( realCursorPos < content->size() ) {
                while(++realCursorPos < content->size() && ((*content)[realCursorPos]&0xc0) == 0x80) ;
            }
        }
        else if (sym == SDLK_DELETE) {
            if (realCursorPos < content->size() ) {
                content->RemoveSubStrUtf8(realCursorPos,1);
            }
        }
        else if (sym == SDLK_BACKSPACE) {
            if (realCursorPos > 0) {
                realCursorPos -= content->RemoveSubStrUtf8(realCursorPos,-1);
            }
        }
        else if (sym == SDLK_KP_ENTER || sym == SDLK_RETURN || sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_ESCAPE ) {
            ret = false;
//            sym = SDLK_DOWN;
        }
#ifdef MACOSX_XCODE
        // SDL3: Use SDL_KMOD_GUI for Command key on Mac
        else if (sym == SDLK_V && mod & SDL_KMOD_GUI) {
            CFDataRef data;
            if (su_OSXPastePasteboardData(data)) {
                const UInt8 *bytes = CFDataGetBytePtr(data);
                CFIndex bytesLength = CFDataGetLength(data);

                for (int i = 0; i < bytesLength; i++) {
                    if (!InsertChar(bytes[i], false))
                        break;
                }

                CFRelease(data);
            }
            else {
                ret = false;
            }
        }
#elif !defined(MACOSX)
        else if (sym == SDLK_V && mod & SDL_KMOD_CTRL) {
            char *scrap = 0;
            int scraplen;
            static bool initialized_scrap = false;

            ret = false;

            if(!initialized_scrap && init_scrap() >= 0) {
                initialized_scrap = true;
            }
            if(initialized_scrap) {
                get_scrap(SCRAP_TEXT, &scraplen, &scrap);
                if(scraplen > 0) {
                    bool convToUnicode = (!utf8::is_valid(scrap,scrap+scraplen));
                    if(!convToUnicode) std::cerr << "utf8 ";
                    std::cerr << "scrap: " << scrap << std::endl;
                    for(unsigned char *c = (unsigned char *)scrap; *c; ++c) {
                        if(!InsertChar(*c,convToUnicode)) {
                            break; // we hit a newline or were trying to
                                  // paste binary stuff
                        }
                        ret = true;
                    }
                    //free(scrap);
                }
            }
        }
#endif
        else
        {
            // only the top row of keys counts as unhandled
            // SDL3: c.scancode → e.key.scancode
            if(e.key.scancode >= SDL_SCANCODE_F1 && e.key.scancode <= SDL_SCANCODE_PAUSE)
                ret = false;
        }
    }
    // SDL3: SDL_TEXTINPUT → SDL_EVENT_TEXT_INPUT
    else if (e.type==SDL_EVENT_TEXT_INPUT) {
        ret = Insert(tString(e.text.text)); // just insert input text as utf8 string
    }
    // SDL3: SDL_TEXTEDITING → SDL_EVENT_TEXT_EDITING
    else if (e.type==SDL_EVENT_TEXT_EDITING) {
//        fprintf(stderr, "text editing \"%s\", selected range (%d, %d)\n",
//                e.edit.text, e.edit.start, e.edit.length);
    }
    if( realCursorPos > content->size()) {
        realCursorPos=content->size();
    }

    return ret;
#else
    return false;
#endif
}

void uMenuItemString::Select() {
#ifndef DEDICATED
    // SDL3: SDL_StartTextInput takes window parameter
    SDL_StartTextInput(sr_screen);
#endif
}

void uMenuItemString::Deselect() {
#ifndef DEDICATED
    // SDL3: SDL_StopTextInput takes window parameter
    SDL_StopTextInput(sr_screen);
#endif
}

bool uMenuItemString::Insert(const tString &insertion)
{
	// Len() includes the trailing \0
	if ( insertion.Len() > 0 && content->Len() + insertion.Len() <= maxLength_ + 2 )
    {
        *content = content->SubStr( 0, realCursorPos ) + insertion + content->SubStr( realCursorPos );
        realCursorPos += insertion.Len()-1;
        return true;
    }
    else
    {
        return false;
    }
}

inline bool IsReservedCodePoint(int unicode)
{
    bool reserved = unicode < 32;

#ifdef MACOSX
    /*
     Function keys code points. See the “Function-Key Unicodes” section.
     http://developer.apple.com/DOCUMENTATION/Cocoa/Reference/ApplicationKit/Classes/NSEvent_Class/Reference/Reference.html
     */

    reserved = reserved || (unicode >= 0xF700 && unicode <= 0xF747);
#endif

    return reserved;
}

bool uMenuItemString::InsertChar(int unicode, bool convert) {
#ifndef DEDICATED
    if (!IsReservedCodePoint(unicode))
    {
        // insert character if there is room
        if ( content->LenUtf8() < maxLength_ )
        {
            tString utf8string;

            if (convert)
            {
                unsigned short utf16string[1];
                utf16string[0] = unicode;
                utf8::utf16to8(utf16string, utf16string+1, back_inserter(utf8string));
            }
            else
            {
                utf8string.push_back(unicode);
            }

            content->insert(realCursorPos, utf8string);
            realCursorPos+=utf8string.size();
        }

        return true;
    }
    else {
        return false;
    }
#endif
    return false;
}

//! @param words a deque containing the possible words that can be used for completion
uAutoCompleter::uAutoCompleter(std::deque<tString> &words) :
        m_PossibleWords(words),
        m_LastCompletion(-1),
        m_ignorecase(true)
{}

//! @param string the string that should be tested
//! @param pos    the cursor position within the string
//! @return -1 if the cursor isn't at the end of a word, the number of characters to the previous space if it is
int uAutoCompleter::FindLengthOfLastWord(tString &string, unsigned pos) {
    int charright = ' ', charleft  = ' ';
    if(string.size() >= 1) {
        if (pos == string.size()) {
            charleft=string.at(pos-1);
        }
        else if (pos < string.size()) {
            charright=string.at(pos);
            if(pos>1) {
                charleft=string.at(pos-1);
            }
        }
    }
    if(charright != ' ' && charleft != ' ')
        return -1; //no completion possible
    if(charleft == ' ')
        return 0;
    else
        return pos - string.find_last_of(' ', pos - 1) - 1;
}

//! @param word       the part of the word that is searched for
//! @param results    the list where the possible completions will be saved
void uAutoCompleter::FindPossibleWords(tString word, std::deque<tString> &results) {
    if(m_ignorecase)
        word = Simplify(word);
    for(std::deque<tString>::iterator i=m_PossibleWords.begin(); i!=m_PossibleWords.end(); ++i) {
        size_t pos;
        if((pos = (m_ignorecase ? Simplify(*i) : *i).find(word)) != tString::npos) {
            if(isalpha((*i)[pos]) && pos != 0 && isalpha((*i)[pos-1]))
                continue; //both the char we're at and the one before is alphanumeric; we're in the middle of a word
            results.push_back(*i);
        }
    }
}

//! @param word       the part of the word that is searched for
//! @param results    the list of possible completions
//! @return the match, if any
tString uAutoCompleter::FindClosestMatch(tString &word, std::deque<tString> &results) {
    tString ret(m_ignorecase?Simplify(word):word);
    unsigned int len = ret.size();
    while(true) { // complete right side
        std::deque<tString>::iterator i;
        i = results.begin();
        bool found=true;
        tString::size_type pos=(m_ignorecase?Simplify(*i):*i).find(ret);
        if(pos!=tString::npos && pos+len < i->size()) {
            found=false;
            ret+=(m_ignorecase ? tolower(i->at(pos+len)) : i->at(pos+len));
            ++i;
            for(; i!=results.end(); ++i) {
                if((m_ignorecase?Simplify(*i):*i).find(ret) == tString::npos) {
                    found=true;
                    ret.erase(ret.length()-1);
                    break;
                }
            }
        }
        if (found) //we found a mismatch
            break;
        else
            len++;
    }
    while(true) { // something similar, but now for the left side of the word
        std::deque<tString>::iterator i;
        i = results.begin();
        bool found=true;
        tString::size_type pos=(m_ignorecase?Simplify(*i):*i).find(ret);
        if(pos!=tString::npos && pos > 0) {
            found=false;
            ret.insert(ret.begin(),(m_ignorecase ? tolower(i->at(pos-1)) : i->at(pos-1)));
            ++i;
            for(; i!=results.end(); ++i) {
                if((m_ignorecase?Simplify(*i):*i).find(ret) == tString::npos) {
                    found=true;
                    ret.erase(0,1);
                    break;
                }
            }
        }
        if (found)  //we found a mismatch
            break;
        else
            len++;
    }
    return ret;
}

//! @param results    the list of possible completions
//! @param word       the word that was already typed, it will be highlighted
void uAutoCompleter::ShowPossibilities(std::deque<tString> &results, tString &word) {
    if(results.size() > 10) {
        con << tOutput("$tab_completion_toomanyresults");
    }
    else {
        con << tOutput("$tab_completion_results");
        tString::size_type len=word.length();
        for(std::deque<tString>::iterator i=results.begin(); i!=results.end(); ++i) {
            tString result = m_ignorecase ? Simplify( *i ) : *i;
            tString::size_type pos= result.find(word);
            con << result.SubStr(0,pos)
            << "0xff8888"
            << result.SubStr(pos, len)
            << "0xffffff"
            << result.SubStr(pos+len)
            << "\n";
        }
    }
}

//! @param string     the string in which the completion should take place
//! @param pos        the position in strind where the replaced word ends
//! @param len        the length of the word that is already there
//! @param match      the string that will be inserted into the other one
//! @return the new cursor position
int uAutoCompleter::DoCompletion(tString &string, int pos, int len, tString &match) {

    string.erase(pos-len, len);
    string.insert(pos-len, match);
    return pos - len + match.Len();
}

//! @param string     the string in which the completion should take place
//! @param pos        the position in string where the replaced word ends
//! @param len        the length of the word that is already there
//! @param match      the string that will be inserted into the other one
//! @return the new cursor position
int uAutoCompleter::DoFullCompletion(tString &string, int pos, int len, tString &match) {
    tString actualString = match + " ";
    return DoCompletion(string, pos, len, actualString);
}

//! @param string     the string in which the completion should take place
//! @param pos        the cursor position
//! @param len	      the length of the word
//! @return the new position on success (something was found), -1 on failure
int uAutoCompleter::TryCompletion(tString &string, unsigned pos, unsigned len) {
    tString word(string.SubStr(pos-len, len));
    std::deque<tString> results;
    FindPossibleWords(word, results);
    if(results.size() > 1){
        tString match(FindClosestMatch(word, results));
        if(match == word) { //no completion took place
            ShowPossibilities(results, word);
            return pos;
        }
        else { //do the completion
            return DoCompletion(string, pos, len, match);
        }
    }
    else if(!results.empty()){
        return DoFullCompletion(string, pos, len, results.front());
    }
    return -1;
}

//! @param string     the string in which the completion should take place
//! @param pos        the cursor position
//! @return the new cursor position
int uAutoCompleter::Complete(tString &string, unsigned pos) {
    if(m_LastCompletion != -1 && (unsigned)m_LastCompletion < pos) {
        int res = TryCompletion(string, pos, pos - m_LastCompletion);
        if(res != -1) return res;
    }
    int len = FindLengthOfLastWord(string, pos);
    if(len == -1) return pos; //in the middle of a word...
    int res = TryCompletion(string, pos, len);
    if(res != -1) {
        m_LastCompletion = pos - len;
        return res;
    }
    return pos;
}

//! @param ignorecase ignore upper and lower case?
void uAutoCompleter::SetIgnorecase(bool ignorecase) {
    m_ignorecase = ignorecase;
}

//! @param str the string to simplify
//! @returns the simplified string(in this case everything converted to lowercase)
tString uAutoCompleter::Simplify(tString const &str) {
    return str.ToLower();
}

//! @param M         passed on to uMenuItemString
//! @param desc      passed on to uMenuItemString
//! @param help      passed on to uMenuItemString
//! @param c         passed on to uMenuItemString
//! @param maxLength passed on to uMenuItemString
//! @param history   a reference to the history of commands
//! @param limit     the limit for the history's size
//! @param completer the autocompleter to be used, if 0, no completion will take place
uMenuItemStringWithHistory::uMenuItemStringWithHistory(uMenu *M,const tOutput& desc, const tOutput& help,tString &c, int maxLength, history_t &history, int limit, uAutoCompleter *completer ):
        uMenuItemString(M, desc,help,c, maxLength ),
        m_History(history),
        m_HistoryPos(0),
        m_HistoryLimit(limit),
        m_Completer(completer),
m_Searchmode(false) {
    m_History.push_front(tString());
}

//! This destructor will write the currently displayed string into the history if it isn't already in it
uMenuItemStringWithHistory::~uMenuItemStringWithHistory() {
    if(content->Len() > 1){
        for(history_t::iterator i=m_History.begin(); i!=m_History.end(); ++i) {
            if(*i == *content) {
                m_History.erase(i);
                break;
            }
        }
        m_History.front() = *content;
    } else {
        m_History.pop_front();
    }
    if(m_History.size() > m_HistoryLimit)
        m_History.pop_back();
}

//! @param e the event to process
/// @returns true if the event was handled, false if it wasn't
bool uMenuItemStringWithHistory::Event(SDL_Event &e){
#ifndef DEDICATED
    if (e.type==SDL_EVENT_KEY_DOWN &&
            (e.key.key==SDLK_UP && !m_Searchmode )){
        if (m_History.size() - 1 > m_HistoryPos)
        {
            if(m_HistoryPos == 0)  //the new entry... save it before overwriting it
                m_History.front() = *content;
            m_HistoryPos++;
            *content = m_History[m_HistoryPos];
            realCursorPos = content->size();
        }

        return true;
    }
    else if (e.type==SDL_EVENT_KEY_DOWN &&
             (e.key.key==SDLK_DOWN && !m_Searchmode)){
        if (m_HistoryPos > 0)
        {
            m_HistoryPos--;
            *content = m_History[m_HistoryPos];
            realCursorPos = content->size();
        }

        return true;
    }
    else if (e.type==SDL_EVENT_KEY_DOWN && e.key.mod & SDL_KMOD_CTRL && e.key.key == SDLK_R ) {
        m_Searchmode = true;
        if(m_HistoryPos == 0 && !content->empty()) {  //the new entry... save it before overwriting it
            m_History.front() = *content;
            m_History.push_front(tString());
        }
        content->erase();
        realCursorPos=0;
        m_HistoryPos++;
        m_SearchFailing=false;
        return true;
    }
    else if (e.type==SDL_EVENT_KEY_DOWN &&
             (e.key.key==SDLK_TAB && !m_Searchmode)){
        if(m_Completer != 0) {
            realCursorPos = m_Completer->Complete(*content, realCursorPos);
        }

        return true;
    }
    else if (e.type==SDL_EVENT_KEY_DOWN && m_Searchmode) {
        if(e.key.key==SDLK_LEFT || e.key.key==SDLK_RIGHT) {
            *content = m_History[m_HistoryPos];
            m_Searchmode = false;
            realCursorPos=0;
            return true;
        }
        bool ret = uMenuItemString::Event(e);
        tString searchstring = content->ToLower();
        unsigned int pos = 0;
        for(history_t::iterator iter = m_History.begin(); iter != m_History.end(); ++iter, ++pos) {
            if (iter->ToLower().find(searchstring) != tString::npos) {
                m_HistoryPos = pos;
                m_SearchFailing=false;
                return ret;
            }
        }
        m_SearchFailing=true;
        return ret;
    }

    else
        return uMenuItemString::Event(e);
#endif
    return false;
}

void uMenuItemStringWithHistory::Render(REAL x,REAL y,REAL alpha,bool selected) {
    if(m_Searchmode) {
        DisplayText(-.88, y-.1, ((m_SearchFailing ? "Failing reverse search: " : "Reverse search: ") + m_History[m_HistoryPos]).c_str(), selected, alpha, -1);
    }
    uMenuItemString::Render(x, y, alpha, selected);
}

//! @param filename the name of the file (in var/) to be loaded and saved to
uMenuItemStringWithHistory::history_t::history_t(char const *filename) : std::deque<tString>(), m_filename(filename) {
    tTextFileRecorder the_file ( tDirectories::Var(), m_filename );
    while(!the_file.EndOfFile()) {
        push_front(the_file.GetLine());
    }
}

uMenuItemStringWithHistory::history_t::~history_t() {
    std::ofstream the_file;
    if(tDirectories::Var().Open( the_file, m_filename)) {
        for(reverse_iterator i = rbegin(); i != rend(); ++i) {
            the_file << *i << "\n";
        }
    }
}


// *****************************************************
//  Submenu
// *****************************************************


uMenuItemSubmenu::uMenuItemSubmenu(uMenu *M,
                                   uMenu *s,
                                   const tOutput& help)
        :uMenuItem(M,help),submenu(s){}


void uMenuItemSubmenu::Render(REAL x,REAL y,REAL alpha,bool selected){
    DisplayTextSpecial(x,y,submenu->title,selected,alpha,0);
}

void uMenuItemSubmenu::Enter(){
    submenu->Enter();
}

// *****************************************************
//  action
// *****************************************************


uMenuItemAction::uMenuItemAction(uMenu *M,
                                 const tOutput& n, const tOutput& help )
        :uMenuItem(M,help),name_(n){}


void uMenuItemAction::Render(REAL x,REAL y,REAL alpha,bool selected){
    DisplayTextSpecial(x,y,name_,selected,alpha,0);
}


void uMenuItemAction::Enter()
{
    tASSERT( 0 )
}


// *****************************************************
//  function
// *****************************************************


uMenuItemFunction::uMenuItemFunction(uMenu *M,
                                     const tOutput& n, const tOutput& help,
                                     FUNCPTR f)
        :uMenuItemAction(M,n,help),func(f){}

void uMenuItemFunction::Enter(){
    (*func)();
}



uMenuItemFunctionInt::uMenuItemFunctionInt(uMenu *M,
        const tOutput& n,
        const tOutput& help,
        INTFUNCPTR f,int a)
        :uMenuItemAction(M,n,help),func(f),arg(a){}


void uMenuItemFunctionInt::Enter(){
    (*func)(arg);
}

// *****************************************************
//  File Selection (added by k)
// *****************************************************

void uMenuItemFileSelection::NewChoice( uSelectItem<bool> * ) {}
void uMenuItemFileSelection::NewChoice( char *, bool ) {}

void uMenuItemFileSelection::Reload()
{
    Clear();
    if ( defaultFileName_.Len() > 1 && defaultFilePath_.Len() > 1 )
        AddFile( defaultFileName_, defaultFilePath_, formatName_ );
    LoadDirectory( dir_, fileSpec_, formatName_ );
}

void uMenuItemFileSelection::LoadDirectory( const char *dir, const char *fileSpec,
        bool formatName /*= true*/ )
{
    tArray <tString> files;
    tString filePath ( dir );
    tDirectories::GetFiles( tString( dir ), tString( fileSpec ), files, getFilesFlag_ );
    for ( int i = 0; i < files.Len(); i++ )
    {
        AddFile( files( i ), filePath + files( i ), formatName );
    }
}

void uMenuItemFileSelection::AddFile( const char *fileName, const char *filePath,
                                      bool formatName /*= true*/ )
{
    tString menuName ( fileName );
    if ( formatName )
        tDirectories::FileNameToMenuName( fileName, menuName );
    uMenuItemSelection<tString>::NewChoice( menuName, "", tString( filePath ) );
}

// *****************************************************
// Menu Enter/Leave-Callback
// *****************************************************

static tCallback *enter_anchor=NULL,*leave_anchor=NULL, *background_anchor=NULL;

uCallbackMenuEnter::uCallbackMenuEnter(AA_VOIDFUNC *f)
        :tCallback(enter_anchor,f){}

void uCallbackMenuEnter::MenuEnter(){
    Exec(enter_anchor);
}

uCallbackMenuLeave::uCallbackMenuLeave(AA_VOIDFUNC *f)
        :tCallback(leave_anchor,f){}

void uCallbackMenuLeave::MenuLeave(){
    Exec(leave_anchor);
}

uCallbackMenuBackground::uCallbackMenuBackground(AA_VOIDFUNC *f)
        :tCallback(background_anchor,f){}

void uCallbackMenuBackground::MenuBackground(){
    Exec(background_anchor);
}

// poll input, return true if ESC was pressed
bool uMenu::IdleInput( bool processInput )
{
#ifndef DEDICATED
    if( !processInput )
    {
        sr_LockSDL();
        SDL_PumpEvents();
        sr_UnlockSDL();
        return uMenu::quickexit != uMenu::QuickExit_Off;
    }

    // Render a frame so blocking network operations show console output
    // (e.g. "Connecting to master server…") instead of a frozen screen.
    rRenderFrame([](){});

    SDL_Event event;
    uInputProcessGuard inputProcessGuard;
    while (!s_idleBackground && su_GetSDLInput(event))
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.key)
            {
            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                return true;
                break;
            default:
                break;
            }
            break;
        case SDL_EVENT_FINGER_UP:
            // On touchscreen, a tap cancels the blocking network operation
            // (equivalent to pressing ESC on desktop).
            if (su_GetEnableTouch() > 0)
            {
                s_globalRepeat = false;
                return true;
            }
            break;
        default:
            break;
        }
    }

    return uMenu::quickexit != uMenu::QuickExit_Off;
#endif

    return false;
}

// ***************************************
// * uAnimation
// ***************************************

//! crude animation class
uAnimationFrame::uAnimationFrame( float duration_ )
: duration( duration_ )
{
}

uAnimationFrame::uAnimationFrame( char const * texture, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture );
}

uAnimationFrame::uAnimationFrame( char const * texture1, char const * texture2, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture1 );
    textures.push_back( texture2 );
}

uAnimationFrame::uAnimationFrame( char const * texture1, char const * texture2, char const * texture3, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture1 );
    textures.push_back( texture2 );
    textures.push_back( texture3 );
}

uAnimationFrame::uAnimationFrame( char const * texture1, char const * texture2, char const * texture3, char const * texture4, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture1 );
    textures.push_back( texture2 );
    textures.push_back( texture3 );
    textures.push_back( texture4 );
}

//! loads an animation from a file
bool uAnimationFrame::Load( std::vector< uAnimationFrame > & animation, char const * filename )
{
    std::ifstream f;
    if( !tDirectories::Data().Open( f, filename ) )
    {
        return false;
    }

    while( f.good() )
    {
        tString l;
        l.ReadLine( f );

        // ignore comments
        if( l[0]=='#' )
        {
            continue;
        }

        std::istringstream s(l);

        uAnimationFrame frame;
        s >> frame.duration;
        if( s.eof() )
        {
            continue;
        }
        tString texture;
        while( s.good() )
        {
            s >> texture;
            frame.textures.push_back( texture );
        }
        if( !s.eof() )
        {
            return false;
        }
        animation.push_back( frame );
    }
    if( !f.eof() )
    {
        return false;
    }

    return true;
}

#ifndef DEDICATED

// ***************************************
// * uAnimationPlayer
// ***************************************

uAnimationPlayer::uAnimationPlayer( std::vector< uAnimationFrame > const & animation )
: animation_( animation ), current_( -1 )
{
}

uAnimationPlayer::~uAnimationPlayer()
{
    ClearTextures();
}

void uAnimationPlayer::ClearTextures()
{
    for( std::vector< rITexture * >::iterator iter = textures_.begin();
         iter != textures_.end(); ++iter )
    {
        delete (*iter);
    }

    textures_.clear();
}

void uAnimationPlayer::Render( tRectangle & drawArea )
{
    // no animation? screw this.
    if( animation_.size() == 0 )
    {
        return;
    }

    bool change = false;

    // start from the beginning
    if( current_ < 0 )
    {
        current_ = 0;
        lastChange_ = tSysTimeFloat();
        change = true;
    }

    // advance
    REAL completion = ( tSysTimeFloat() - lastChange_ )/animation_[current_].duration;
    if( completion >= 1 )
    {
        current_++;
        if( current_ >= (int)animation_.size() )
        {
            current_ = 0;
        }
        lastChange_ = tSysTimeFloat();
        change = true;
        completion = 0;
    }
    if( completion < .01 )
        completion = 0.01;

    // get current frame
    uAnimationFrame const & frame = animation_[current_];

    // load textures
    if( change )
    {
        ClearTextures();
        for( std::vector< std::string >::const_iterator iter = frame.textures.begin();
             iter != frame.textures.end(); ++iter )
        {
            textures_.push_back( tNEW(rFileTexture)( rTextureGroups::TEX_FONT, (*iter).c_str(), false, false, true ) );
        }
    }

    REAL xl = drawArea.GetLow().x;
    REAL yl = drawArea.GetLow().y;
    REAL xh = drawArea.GetHigh().x;
    REAL yh = drawArea.GetHigh().y;

    // Set up identity matrices for HUD rendering
    ModelMatrix();
    IdentityMatrix();
    ProjMatrix();
    IdentityMatrix();

    RenderEnableState(rCapability::AlphaTest);
    RenderDisableState(rCapability::Blend);
    for( std::vector< rITexture * >::iterator iter = textures_.begin();
         iter != textures_.end(); ++iter )
    {
        (*iter)->Select(true);
        RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureMagFilter, rGLConst::Nearest);

        bool end = ( iter+1 == textures_.end() );
        if ( end )
        {
            RenderAlphaFunc(rCompareFunc::Greater,1-completion);
        }
        else
        {
            RenderAlphaFunc(rCompareFunc::Greater,0);
        }

        // Get texture ID for batch rendering
        unsigned int textureId = RenderGetBoundTexture2D();

        // Create textured quad for animation frame
        std::vector<rVertex20> animVerts;
        animVerts.reserve(6);
        // Triangle 1: bottom-left, top-left, top-right
        animVerts.push_back(rVertex20(xl, yl, 0.0f, 255, 255, 255, 255, 0.0f, 1.0f));
        animVerts.push_back(rVertex20(xl, yh, 0.0f, 255, 255, 255, 255, 0.0f, 0.0f));
        animVerts.push_back(rVertex20(xh, yh, 0.0f, 255, 255, 255, 255, 1.0f, 0.0f));
        // Triangle 2: bottom-left, top-right, bottom-right
        animVerts.push_back(rVertex20(xl, yl, 0.0f, 255, 255, 255, 255, 0.0f, 1.0f));
        animVerts.push_back(rVertex20(xh, yh, 0.0f, 255, 255, 255, 255, 1.0f, 0.0f));
        animVerts.push_back(rVertex20(xh, yl, 0.0f, 255, 255, 255, 255, 1.0f, 1.0f));

        // Use Opaque blend mode since we're using alpha test
        rRenderStateKey animState = rRenderStateKey::HUD(textureId, rBlendMode::Opaque);
        animState.flags |= rRenderStateKey::AlphaTest;
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, animState, animVerts.data(), animVerts.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
    }
    RenderAlphaFunc(rCompareFunc::Greater,0);
}

#endif

// print a big title, small description and animation
bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to, std::vector< uAnimationFrame > const & animation )
{
    bool ret = true;
#ifdef DEDICATED
    con << message << ":\n";
    con << interpretation << '\n';
#else
    uAnimationPlayer player( animation );

    // Note: the original GL code called rITexture::UnloadAll() here to free VRAM.
    // In Vulkan, this destroys the font atlas without proper reload, causing blank text.

    tAdvanceFrame();

    bool textOutBack = sr_textOut;
    sr_textOut = false;

    FUNCPTR idle_back = idle;
    uMenu::SetIdle(NULL);

    rTextField::SetDefaultColor( tColor(1,1,1,1) );
    rTextField::SetBlendColor( tColor(1,1,1,1) );

    double timeout = tSysTimeFloat() + to;
    double ignoreInput = tSysTimeFloat() + .5;
    SDL_Event tEvent;

    // catch some keyboard input
    {
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent)) ;
    }

    {
        uInputProcessGuard inputProcessGuard;

        unsigned offset = 0; //amount of scrolling taking place
        //convert to an array for scrolling
        tString interpretationString;
        interpretationString << interpretation << "\n";
        std::vector<tString> lines;
        int lastNewline = 0;
        for (int i = 0; i < interpretationString.Len() - 1; ++i) {
            if (interpretationString[i] == '\n' && i != 0) {
                lines.push_back(interpretationString.SubStr(lastNewline, i - lastNewline));
                lastNewline = i + 1;
            }
        }

        while (  !quickexit &&
                 (to < 0 || tSysTimeFloat() < timeout)){
            //while(  !quickexit && ( !su_GetSDLInput(tEvent) || tEvent.type!=SDL_EVENT_KEY_DOWN) &&
            //        (to < 0 || tSysTimeFloat() < timeout)){
            if ( su_GetSDLInput(tEvent) &&
                 (tEvent.type==SDL_EVENT_KEY_DOWN || tEvent.type==SDL_EVENT_FINGER_DOWN) ) {
                if (tEvent.type == SDL_EVENT_FINGER_DOWN) {
                    // Touch anywhere dismisses the message (after ignore window)
                    if (tSysTimeFloat() > ignoreInput) break;
                } else {
                switch (tEvent.key.key) {
                case SDLK_UP:
                    if (offset > 0)
                        offset -= 1;
                    continue;
                case SDLK_DOWN:
                    offset += 1;
                    continue;
                case SDLK_ESCAPE:
                    ret = false;
                    break;
                default:
                    break;
                }
                if( tSysTimeFloat() > ignoreInput )
                {
                    // exit on any key
                    break;
                }
                else
                {
                    // esc may have been pressed in error
                    ret = true;
                }
                }
            }
            if ( sr_glOut )
            {
                rRenderFrame([&]() {
                    sr_ResetRenderState(true);
                    rViewport::s_viewportFullscreen.Select();

                    // GenericBackground();
                    static rFileTexture background( rTextureGroups::TEX_FONT, "textures/message_background.png" );
                    background.Select();

                    // Batch render background quad
                    unsigned int textureId = RenderGetBoundTexture2D();

                    std::vector<rVertex20> bgVerts;
                    bgVerts.reserve(6);
                    // Triangle 1: top-left, top-right, bottom-right
                    bgVerts.push_back(rVertex20(-1.0f, 1.0f, 0.0f, 255, 255, 255, 255, 0.0f, 0.0f));
                    bgVerts.push_back(rVertex20(1.0f, 1.0f, 0.0f, 255, 255, 255, 255, 1.0f, 0.0f));
                    bgVerts.push_back(rVertex20(1.0f, -1.0f, 0.0f, 255, 255, 255, 255, 1.0f, 1.0f));
                    // Triangle 2: top-left, bottom-right, bottom-left
                    bgVerts.push_back(rVertex20(-1.0f, 1.0f, 0.0f, 255, 255, 255, 255, 0.0f, 0.0f));
                    bgVerts.push_back(rVertex20(1.0f, -1.0f, 0.0f, 255, 255, 255, 255, 1.0f, 1.0f));
                    bgVerts.push_back(rVertex20(-1.0f, -1.0f, 0.0f, 255, 255, 255, 255, 0.0f, 1.0f));

                    rRenderStateKey bgState = rRenderStateKey::Textured(textureId, rBlendMode::Alpha);
                    rRenderQueue::Instance().Submit(rRenderPhase::HUD, bgState, bgVerts.data(), bgVerts.size());

                //16*3/640.0, 32*3/480.0
                REAL w=0.1*(REAL(sr_screenHeight)/sr_screenWidth),h=0.2;

                    //REAL middle=-.6;

                    tString m(message);
                    int len = tColoredString::RemoveColors(m).Len();
                    float maxWidth = 4.8;
                    if (w * len > maxWidth)
                    {
                        h = h * maxWidth / (w * len);
                        w = maxWidth / len;
                    }

                    Color(1,1,1);
                    DisplayText(0,.8,w,message,sr_fontError);

                    //16/640.0
                    w = 1/30.0*(REAL(sr_screenHeight)/sr_screenWidth);
                    h = 32/480.0;

                    REAL center = .4;
                    if (offset >= lines.size()) offset = lines.size() - 1;
                    {
                        rTextField c(-.9,.6, h, sr_fontError);
                        c.EnableLineWrap();
                        c.SetWidth(1.8);

                        for (unsigned i = offset; i < lines.size(); ++i)
                            c << lines[i] << "\n";

                        center = (c.GetBottom()+1)/4;
                    }

                    // determite best display size for animation, asuming it is 64 pixels high
                    {
                        int middleY = int( sr_screenHeight * center );
                        int maxHeight = middleY - 5 - sr_screenHeight/20;
                        int maxWidth = sr_screenWidth/2 - 10;
                        int height = 32;
                        int width = height*2;
                        int scale = 1;
                        while( ( scale + 1) * height + 1 < maxHeight && ( scale + 1 ) * width < maxWidth )
                        {
                            scale++;
                        }
                        height *= scale;
                        width  *= scale;
                        REAL wr = 2*width/REAL(sr_screenWidth);
                        REAL hr = 2*height/REAL(sr_screenHeight);
                        REAL c = (center*2)-1;

                        tRectangle ani = tRectangle( tCoord(-wr, c-hr), tCoord(wr, c+hr) );
                        player.Render( ani );
                    }

                    // Execute HUD phase to render all batched geometry
                    rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
                });
            }
            else
            {
                rSysDep::SwapGL();
            }
            tAdvanceFrame();
        }
    }

    // catch some keyboard input
    {
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent)) ;
    }

    uMenu::SetIdle(idle_back);

    sr_textOut = textOutBack;
#endif

    return ret;
}

bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to, char const * animation )
{
    std::vector< uAnimationFrame > frames;
    uAnimationFrame::Load( frames, animation );
    return Message( message, interpretation, to, frames );
}

// return value: false only if the user pressed ESC
bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to)
{
    std::vector< uAnimationFrame > noAnimation;
    return Message( message, interpretation, to, noAnimation );
}

