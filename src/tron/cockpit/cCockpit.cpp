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

#include "cockpit/cCockpit.h"
#include <algorithm>
#include "tValue.h"
#include "values/vParser.h"
#include "cockpit/cGauges.h"
#include "cockpit/cLabel.h"
#include "cockpit/cMap.h"
#include "cockpit/cCamview.h"
#include "cockpit/cRectangle.h"
#include "cockpit/cTouchButton.h"
#include "nConfig.h"
#include "tResourceManager.h"

#ifndef DEDICATED

#include "tToDo.h"

#include "ePlayer.h"
#include "gCycle.h"
#include "eTimer.h"
#include "eTeam.h"
#include "eCamera.h"
#include "rRender.h"
#include "rFont.h"
#include "rScreen.h"
#include "rHUDRenderer.h"
#include "rRendererState.h"
#include "rRenderQueue.h"
#include "eSensor.h"
#include <iostream>
#include "eSoundMixer.h"
#include "eGrid.h"

#include <time.h>

// Forward: ensure cockpit pack ZIP is extracted before loading
extern void sr_EnsureCockpitPackExtracted();

// Forward: the COCKPIT_FILE-backing tString lives further down in this file
// because tConfItem's static-initialization order depends on it. parsecockpit
// references it for top-level dedup, so declare it here.
extern tString cockpit_file;

static void parsecockpit () {
    // Top-level dedup: COCKPIT_FILE callback fires on every assignment,
    // even when the cfg layer re-assigns the same value (which the pack-
    // switch path does — once to reset to default, once to apply the
    // pack's value, frequently identical). Without this guard we get a
    // (2 × N cockpits) fan-out per pack switch. Skip silently when the
    // value hasn't actually changed since our last fan-out.
    static tString sg_lastFile;
    if (cockpit_file == sg_lastFile) return;
    sg_lastFile = cockpit_file;

    // One log per actual file change (instead of one per cCockpit
    // instance inside ProcessCockpit) — keeps the console quiet during
    // moviepack switches and matches the cache-pack-extracted message
    // style.
    SDL_Log("[Cockpit] loading '%s'", static_cast<const char*>(cockpit_file));

    sr_EnsureCockpitPackExtracted();
    FOREACH_COCKPIT(i) {
        (*i)->ProcessCockpit();
    }
}

static void readjust_cockpit () {
    FOREACH_COCKPIT(i) {
        (*i)->Readjust();
    }
}

static rCallbackAfterScreenModeChange reloadft(&readjust_cockpit);

tString cockpit_file("AATeam/mobile-0.0.1.aacockpit.xml");
static tConfItem<tString> cf("COCKPIT_FILE",cockpit_file,&parsecockpit);

typedef std::pair<tString, tValue::Callback<cCockpit>::cb_ptr> cbpair;
static const cbpair cbarray[] = {
                                    cbpair(tString("player_rubber")       , &cCockpit::cb_CurrentRubber),
                                    cbpair(tString("player_acceleration") , &cCockpit::cb_CurrentAcceleration),
                                    cbpair(tString("current_ping")        , &cCockpit::cb_CurrentPing),
                                    cbpair(tString("player_speed")        , &cCockpit::cb_CurrentSpeed),
                                    cbpair(tString("max_speed")           , &cCockpit::cb_MaxSpeed),
                                    cbpair(tString("player_brakes")       , &cCockpit::cb_CurrentBrakingReservoir),
                                    cbpair(tString("enemies_alive")       , &cCockpit::cb_AliveEnemies),
                                    cbpair(tString("friends_alive")       , &cCockpit::cb_AliveTeammates),
                                    cbpair(tString("current_framerate")   , &cCockpit::cb_Framerate),
                                    cbpair(tString("time_since_start")    , &cCockpit::cb_RunningTime),
                                    cbpair(tString("current_minutes")     , &cCockpit::cb_CurrentTimeMinutes),
                                    cbpair(tString("current_hours")       , &cCockpit::cb_CurrentTimeHours),
                                    cbpair(tString("current_hours12h")    , &cCockpit::cb_CurrentTimeHours12h),
                                    cbpair(tString("current_seconds")     , &cCockpit::cb_CurrentTimeSeconds),
                                    cbpair(tString("current_score")       , &cCockpit::cb_CurrentScore),
                                    cbpair(tString("top_score")           , &cCockpit::cb_TopScore),
                                    cbpair(tString("top_other_score")     , &cCockpit::cb_TopOtherScore),
                                    cbpair(tString("current_score_team")  , &cCockpit::cb_CurrentScoreTeam),
                                    cbpair(tString("top_score_team")      , &cCockpit::cb_TopScoreTeam),
                                    cbpair(tString("top_other_score_team"), &cCockpit::cb_TopOtherScoreTeam),
                                    cbpair(tString("fastest_speed")       , &cCockpit::cb_FastestSpeed),
                                    cbpair(tString("fastest_name")        , &cCockpit::cb_FastestName),
                                    cbpair(tString("fastest_speed_round") , &cCockpit::cb_FastestSpeedRound),
                                    cbpair(tString("fastest_name_round")  , &cCockpit::cb_FastestNameRound),
                                    cbpair(tString("time_to_impact_front"), &cCockpit::cb_TimeToImpactFront),
                                    cbpair(tString("time_to_impact_right"), &cCockpit::cb_TimeToImpactRight),
                                    cbpair(tString("time_to_impact_left") , &cCockpit::cb_TimeToImpactLeft),
                                    cbpair(tString("current_song")        , &cCockpit::cb_CurrentSong),
                                    cbpair(tString("current_name")        , &cCockpit::cb_CurrentName),
                                    cbpair(tString("current_colored_name"), &cCockpit::cb_CurrentColoredName),
                                    cbpair(tString("current_pos_x")       , &cCockpit::cb_CurrentPosX),
                                    cbpair(tString("current_pos_y")       , &cCockpit::cb_CurrentPosY)
                                };
std::map<tString, tValue::Callback<cCockpit>::cb_ptr> const stc_callbacks(cbarray, cbarray+sizeof(cbarray)/sizeof(cbpair));

std::set<tString> stc_forbiddenCallbacks;
#endif
static tString stc_forbiddenCallbacksString;
#ifndef DEDICATED

std::list<cCockpit *> cCockpit::m_Cockpits;

static void reparseforbiddencallbacks(void) {
    stc_forbiddenCallbacks.clear();

    tString callbacks = stc_forbiddenCallbacksString + ":"; //add the extra separator, makes things easier

    size_t pos = 0;
    size_t next;
    while((next = callbacks.find(':', pos)) != tString::npos) {
        tString callback = callbacks.SubStr(pos, next - pos);
        if(stc_callbacks.count(callback)) {
            stc_forbiddenCallbacks.insert(callback);
        }
        pos = next+1;
    }
    parsecockpit();
}

static nSettingItem<tString> fcs("FORBID_COCKPIT_DATA", stc_forbiddenCallbacksString,&reparseforbiddencallbacks);
#else
static nSettingItem<tString> fcs("FORBID_COCKPIT_DATA", stc_forbiddenCallbacksString);
#endif
#ifndef DEDICATED


cCockpit::~cCockpit() {
    ClearWidgets();
    m_Cockpits.remove(this);
}
cCockpit::cCockpit(cockpit_type type) :
        m_Type(type),
        m_Cam(all),
        m_Player(0),
        m_FocusPlayer(0),
        m_ViewportPlayer(0),
m_FocusCycle(0) {
    m_Cockpits.push_back(this);
    ProcessCockpit();
}

// Forward-declared helper; implementation lives next to s_activeFingers
// below. Scrubs any in-flight finger bindings that point at a touch button
// we're about to free, avoiding a use-after-free on the next FINGER_MOTION
// or FINGER_UP event if a cockpit reload happens with a finger held.
namespace { void sg_DropFingerBindings(cWidget::TouchButton* btn); }

void cCockpit::ClearWidgets(void) {
    //while(!m_Widgets_perplayer.empty()) {
    //    delete m_Widgets_perplayer.front();
    //    m_Widgets_perplayer.pop_front();
    //}
    //while(!m_Widgets_rootwindow.empty()) {
    //    delete m_Widgets_rootwindow.front();
    //    m_Widgets_rootwindow.pop_front();
    //}
    for (cWidget::TouchButton* btn : m_TouchButtons) sg_DropFingerBindings(btn);
    m_Widgets.clear();
    //m_Widgets_perplayer.clear();
    //m_Widgets_cycles.clear();
    m_EventHandlers.clear();
    m_TouchButtons.clear();
}

void cCockpit::SetPlayer(ePlayer *player) {
    m_Player = player;
    m_ViewportPlayer = m_FocusPlayer = m_Player->netPlayer;
    if (player->cam) {
        for(int i =0 ; i< se_PlayerNetIDs.Len(); i++){
            ePlayerNetID *testPlayer = se_PlayerNetIDs[i];
            if(const eGameObject *testCycle = testPlayer->Object()) {
                if(player->cam->Center() == testCycle) {
                    m_FocusPlayer = testPlayer;
                }
            }
        }
    }
    if(m_FocusPlayer != 0) {
        m_FocusCycle = dynamic_cast<gCycle *>(m_FocusPlayer->Object());
    } else {
        m_FocusCycle = 0;
    }
}

//callbacks
tValue::BasePtr cCockpit::cb_CurrentRubber(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Float(m_FocusCycle->GetRubber()));
}
tValue::BasePtr cCockpit::cb_CurrentAcceleration(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Float(m_FocusCycle->GetAcceleration()));
}
tValue::BasePtr cCockpit::cb_CurrentPing(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Int((int)(m_ViewportPlayer->ping*1000)));
}
tValue::BasePtr cCockpit::cb_CurrentSpeed(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Float(m_FocusCycle->Speed()));
}
tValue::BasePtr cCockpit::cb_MaxSpeed(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Int( static_cast<int>(ceil( m_FocusCycle->MaximalSpeed() / 10.) *10)));
}
tValue::BasePtr cCockpit::cb_CurrentBrakingReservoir(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Float(m_FocusCycle->GetBrakingReservoir()));
}
tValue::BasePtr cCockpit::cb_AliveEnemies(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    int aliveenemies=0;
    eTeam *curr = GetCurrentOrFocusedPlayer()->CurrentTeam();
    unsigned short int max = se_PlayerNetIDs.Len();
    for(unsigned short int i=0;i<max;i++){
        ePlayerNetID *p=se_PlayerNetIDs(i);
        if(p->Object() && p->Object()->Alive() && p->CurrentTeam() != curr)
            aliveenemies++;
    }
    return tValue::BasePtr(new tValue::Int(aliveenemies));
}
tValue::BasePtr cCockpit::cb_AliveTeammates(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    int alivemates=0;
    eTeam *curr = GetCurrentOrFocusedPlayer()->CurrentTeam();
    unsigned short int max = se_PlayerNetIDs.Len();
    for(unsigned short int i=0;i<max;i++){
        ePlayerNetID *p=se_PlayerNetIDs(i);
        if(p->Object() && p->Object()->Alive() && p->CurrentTeam() == curr)
            alivemates++;
    }
    return tValue::BasePtr(new tValue::Int(alivemates));
}

tValue::BasePtr cCockpit::cb_Framerate(void){

    int fps = se_FPS();

    return tValue::BasePtr(new tValue::Int(fps));
}

tValue::BasePtr cCockpit::cb_RunningTime(void){
    return tValue::BasePtr(new tValue::Float(tSysTimeFloat()));
}

tValue::BasePtr cCockpit::cb_CurrentTimeHours(void){
    struct tm* thisTime;
    time_t rawtime;

    time ( &rawtime );
    thisTime = localtime ( &rawtime );

    return tValue::BasePtr(new tValue::Int(thisTime->tm_hour));
}

tValue::BasePtr cCockpit::cb_CurrentTimeHours12h(void){
    struct tm* thisTime;
    time_t rawtime;

    time ( &rawtime );
    thisTime = localtime ( &rawtime );

    return tValue::BasePtr(new tValue::Int((thisTime->tm_hour+11)%12+1));
}

tValue::BasePtr cCockpit::cb_CurrentTimeMinutes(void){
    struct tm* thisTime;
    time_t rawtime;

    time ( &rawtime );
    thisTime = localtime ( &rawtime );

    return tValue::BasePtr(new tValue::Int(thisTime->tm_min));
}

tValue::BasePtr cCockpit::cb_CurrentTimeSeconds(void){
    struct tm* thisTime;
    time_t rawtime;

    time ( &rawtime );
    thisTime = localtime ( &rawtime );

    return tValue::BasePtr(new tValue::Int(thisTime->tm_sec));
}

REAL stc_fastestSpeedRound = .0;
REAL stc_fastestSpeed;
tString stc_fastestNameRound;
tString stc_fastestName;
int stc_topScore;

tValue::BasePtr cCockpit::cb_CurrentScore(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Int(GetCurrentOrFocusedPlayer()->TotalScore()));
}
tValue::BasePtr cCockpit::cb_TopScore(void){
    return tValue::BasePtr(new tValue::Int(stc_topScore));
}
tValue::BasePtr cCockpit::cb_TopOtherScore(void){
    int top = 0;
    ePlayerNetID *curr = GetCurrentOrFocusedPlayer();
    for(int i = se_PlayerNetIDs.Len() - 1;i >= 0; i--){
        ePlayerNetID *p = se_PlayerNetIDs(i);
        if(p != curr && p->Score() > top){
            top = p->Score();
        }
    }
    return tValue::BasePtr(new tValue::Int(top));
}

tValue::BasePtr cCockpit::cb_CurrentScoreTeam(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Int(GetCurrentOrFocusedPlayer()->CurrentTeam()->Score()));
}
tValue::BasePtr cCockpit::cb_TopScoreTeam(void){
    int max = 0;
    for(int i=0;i<eTeam::teams.Len();++i){
        eTeam *t = eTeam::teams(i);
        if(t->Score() > max) max = t->Score();
    }
    return tValue::BasePtr(new tValue::Int(max));
}
tValue::BasePtr cCockpit::cb_TopOtherScoreTeam(void){
    int top = 0;
    eTeam *curr = GetCurrentOrFocusedPlayer()->CurrentTeam();
    for(int i = eTeam::teams.Len() - 1;i >= 0; i--){
        eTeam *t = eTeam::teams(i);
        if(t != curr && t->Score() > top){
            top = t->Score();
        }
    }
    return tValue::BasePtr(new tValue::Int(top));
}

static void stc_updateFastest() {
    stc_fastestSpeed = 0.;
    stc_topScore = 0;
    for(int i =0 ; i< se_PlayerNetIDs.Len(); i++){
        ePlayerNetID *p = se_PlayerNetIDs[i];

        if(gCycle *h = (gCycle*)(p->Object())){
            if (h->Speed()>stc_fastestSpeedRound && h->Alive()){
                stc_fastestSpeedRound =  (float) h->Speed();  // changed to float for more accuracy in reporting top speed
                stc_fastestNameRound = p->GetName();
            }
            if (h->Speed()>stc_fastestSpeed && h->Alive()){
                stc_fastestSpeed =  (float) h->Speed();  // changed to float for more accuracy in reporting top speed
                stc_fastestName = p->GetName();
            }
        }
        int thisscore = se_PlayerNetIDs[i]->TotalScore();
        if(thisscore>stc_topScore)
            stc_topScore=thisscore;
    }
}

static rPerFrameTask stcuf(&stc_updateFastest);

tValue::BasePtr cCockpit::cb_FastestSpeedRound(void){
    return tValue::BasePtr(new tValue::Float(stc_fastestSpeedRound));
}

tValue::BasePtr cCockpit::cb_FastestNameRound(void){
    return tValue::BasePtr(new tValue::String(stc_fastestNameRound));
}

tValue::BasePtr cCockpit::cb_FastestSpeed(void){
    return tValue::BasePtr(new tValue::Float(stc_fastestSpeed));
}

tValue::BasePtr cCockpit::cb_FastestName(void){
    return tValue::BasePtr(new tValue::String(stc_fastestName));
}

tValue::BasePtr cCockpit::cb_TimeToImpactFront(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    eSensor test(m_FocusCycle, m_FocusCycle->Position(), m_FocusCycle->Direction());
    test.detect(5.*m_FocusCycle->Speed());
    return tValue::BasePtr(new tValue::Float(test.hit/m_FocusCycle->Speed()));
}
tValue::BasePtr cCockpit::cb_TimeToImpactRight(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    eSensor test(m_FocusCycle, m_FocusCycle->Position(), m_FocusCycle->Direction().Turn(0,-1));
    test.detect(5.*m_FocusCycle->Speed());
    return tValue::BasePtr(new tValue::Float(test.hit/m_FocusCycle->Speed()));
}
tValue::BasePtr cCockpit::cb_TimeToImpactLeft(void){
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    eSensor test(m_FocusCycle, m_FocusCycle->Position(), m_FocusCycle->Direction().Turn(0,1));
    test.detect(5.*m_FocusCycle->Speed());
    return tValue::BasePtr(new tValue::Float(test.hit/m_FocusCycle->Speed()));
}

tValue::BasePtr cCockpit::cb_CurrentSong(void){
    return tValue::BasePtr(new tValue::String(eSoundMixer::GetMixer().GetCurrentSong()));
}

tValue::BasePtr cCockpit::cb_CurrentName(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::String(tString(m_FocusPlayer->GetName())));
}

tValue::BasePtr cCockpit::cb_CurrentColoredName(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    tColoredString ret;
    ret << *m_FocusPlayer;
    return tValue::BasePtr(new tValue::String(ret));
}

tValue::BasePtr cCockpit::cb_CurrentPosX(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Float(m_FocusCycle->Position().x));
}

tValue::BasePtr cCockpit::cb_CurrentPosY(void) {
    if(m_Type == VIEWPORT_TOP) return tValue::BasePtr(new tValue::Base());
    return tValue::BasePtr(new tValue::Float(m_FocusCycle->Position().y));
}

cCockpit* cCockpit::_instance = 0;

void cCockpit::ProcessCockpit(void) {
    // Skip if we've already loaded this exact path. parsecockpit() fans
    // out across every cCockpit instance whenever COCKPIT_FILE is
    // (re-)assigned, and the cockpit-pack switch reassigns it twice with
    // the same value — without this short-circuit the touch cockpit XML
    // loaded 4× (2 reassignments × 2 instances). Skip silently here so
    // there's no behaviour change for an actual file change.
    if (m_LoadedFile == cockpit_file && !m_Widgets.empty())
        return;

    ClearWidgets();

    // Single "loading 'X'" log is emitted by parsecockpit() once per actual
    // value change; per-instance success is silent. Failures still log
    // below — they're real and need to be visible.
    if (!LoadWithParsing(cockpit_file))
    {
        SDL_Log("[Cockpit] FAILED to load '%s'", static_cast<const char*>(cockpit_file));
        // If loading fails and it's not the default cockpit, fall back to default
        // and try to extract the pack on next menu access.
        static const char* defaultCockpit = "Anonymous/standard-0.0.1.aacockpit.xml";
        if (strcmp(static_cast<const char*>(cockpit_file), defaultCockpit) != 0)
        {
            con << "[Cockpit] Failed to load '" << cockpit_file
                << "', falling back to default. Re-select from System Setup.\n";
            cockpit_file = defaultCockpit;
            if (!LoadWithParsing(cockpit_file))
                return;
        }
        else
            return;
    }
    m_LoadedFile = cockpit_file;
    node cur = GetFileContents();
    if(!cur) {
        tERR_WARN("No Cockpit node found!");
    }
    if (m_Path.Type() != "aacockpit") {
        tERR_WARN("Type 'aacockpit' expected, found '" << cur.GetProp("type") << "' instead");
        return;
    }
    if (cur.IsOfType("Cockpit")) {
        ProcessWidgets(cur);
        ProcessTouchOverlay();
        if(sr_screenWidth != 0) Readjust();
        return;
    } else {
        tERR_WARN("Found a node of type '" + cur.GetName() + "' where type 'Cockpit' was expected");
        return;
    }
}

// Default path (relative; resource manager handles directory search).
// The "touch/" subdir comes from the file's category="touch" XML attribute
// which the resource-install script (`batch/make/copyresources.py`) uses
// to rename the file at install time. User overrides at the same relative
// path under the user resource dir are found first by
// tResourceManager::locateResource.
static char const * const sg_touchCockpitFile =
    "AATeam/touch/touchbuttons-0.1.aacockpit.xml";

void cCockpit::ProcessTouchOverlay(void) {
    // Optional file — silent no-op when not present (touch overlay isn't
    // shipped on every install). Probe first so we don't get parser noise
    // for a missing-by-design file.
    tString located = tResourceManager::locateResource(sg_touchCockpitFile, "");
    if (located.Len() <= 1) return;

    // LoadWithParsing replaces m_Doc with the touch file's tree; the
    // primary's widgets have already been appended to m_Widgets above and
    // remain there. This call appends touch widgets on top — render order
    // matches insertion order, so touch buttons draw last (= on top).
    if (!LoadWithParsing(sg_touchCockpitFile)) return;
    m_LoadedTouchFile = sg_touchCockpitFile;

    node cur = GetFileContents();
    if (!cur) return;
    if (cur.IsOfType("Cockpit")) ProcessWidgets(cur);
}

void cCockpit::ProcessWidgets(node cur) {
    std::map<tString, node> templates;
    for(cur = cur.GetFirstChild(); cur; ++cur) {
        if (cur.IsOfType("text") || cur.IsOfType("comment")) continue;
        if (cur.IsOfType("WidgetTemplate")) {
            templates[cur.GetProp("id")] = cur;
            continue;
        }
        switch (m_Type) {
        case VIEWPORT_TOP:
            if(cur.GetProp("viewport") != "top") {
                continue;
            }
            break;
        case VIEWPORT_CYCLE:
            if(cur.GetProp("viewport") != "cycle") {
                continue;
            }
            break;
        case VIEWPORT_ALL:
            if(cur.GetProp("viewport") != "all") {
                continue;
            }
            break;
        }
        cWidget::Base_ptr widget_ptr = ProcessWidgetType(cur);
        if(!widget_ptr) {
            tERR_WARN("Unknown Widget type '" + cur.GetName() + "'");
            continue;
        }
        cWidget::Base &widget = *widget_ptr;

        widget.SetCockpit(this);
        widget.ParseTemplate(true);

        //Process all templates first
        tString use(cur.GetProp("usetemplate"));

        use += " "; //add the extra seperator, makes things easier
        int pos = 0;
        int next;
        while((next = use.find(' ', pos)) != -1) {
            tString thisuse = use.SubStr(pos, next - pos);
            if(templates.count(thisuse)) {
                ProcessWidget(templates[thisuse], widget);
            } else if (!thisuse.empty()) {
                tERR_WARN(tString("Nothing known about template id '") + thisuse + "'.");
            }
            pos = next+1;
        }

        widget.ParseTemplate(false);
        ProcessWidget(cur, widget);
        m_Widgets.push_back(widget_ptr.release());
    }
}

void cCockpit::ProcessWidget(node cur, cWidget::Base &widget) {
    ProcessWidgetCamera(cur, widget);
    int num;
    cur.GetProp("toggle", num);
    AddEventHandler(num, &widget);
    widget.SetDefaultState(cur.GetPropBool("toggleDefault"));
    widget.SetSticky(cur.GetPropBool("toggleSticky"));
    ProcessWidgetCore(cur, widget);
    widget.PostParsingProcess();
}

void cCockpit::AddEventHandler(int id, cWidget::Base *widget) {
    m_EventHandlers.insert(std::pair<int, cWidget::Base *>(id, widget));
}

cWidget::Base_ptr cCockpit::ProcessWidgetType(node cur) {
    if(cur.IsOfType("NeedleGauge"))
        return cWidget::Base_ptr(new cWidget::NeedleGauge());
    if(cur.IsOfType("BarGauge"))
        return cWidget::Base_ptr(new cWidget::BarGauge());
    if(cur.IsOfType("VerticalBarGauge"))
        return cWidget::Base_ptr(new cWidget::VerticalBarGauge());
    if(cur.IsOfType("Label"))
        return cWidget::Base_ptr(new cWidget::Label());
    if(cur.IsOfType("Map"))
        return cWidget::Base_ptr(new cWidget::Map());
    if(cur.IsOfType("Camview")) {
    	cWidget::Camview *w = new cWidget::Camview();
    	w->Process(cur);
        return cWidget::Base_ptr(w);
    }
    if(cur.IsOfType("Rectangle"))
        return cWidget::Base_ptr(new cWidget::Rectangle());
    if(cur.IsOfType("TouchButton")) {
        cWidget::TouchButton *w = new cWidget::TouchButton();
        w->Process(cur);  // read root-level action= and player= attributes
        m_TouchButtons.push_back(w);
        return cWidget::Base_ptr(w);
    }
    return cWidget::Base_ptr();
}

void cCockpit::ProcessWidgetCamera(node cur, cWidget::Base &widget) {
    tString cam(cur.GetProp("camera"));
    if(cam.size() == 0) {
        tERR_WARN("Empty camera string");
        widget.SetCam(all);
    }
    std::map<tString, unsigned> cams;
    cams[tString("custom")] = custom;
    cams[tString("follow")] = follow;
    cams[tString("free")] = free;
    cams[tString("in")] = in;
    cams[tString("server_custom")] = server_custom;
    cams[tString("smart")] = smart;
    cams[tString("mer")] = mer;
    cams[tString("all")] = all;
    cams[tString("*")] = all;

    unsigned ret=0;
    bool invert=false;
    if(cam(0) == '^') {
        invert=true;
        cam.erase(0,1);
    }
    cam += " "; //add the extra seperator, makes things easier

    int pos = 0;
    int next;
    while((next = cam.find(' ', pos)) != -1) {
        tString thiscam = cam.SubStr(pos, next - pos);
        if(cams.count(thiscam)) {
            ret |= cams[thiscam];
        } else {
            tERR_WARN(tString("Nothing known about camera type '") + thiscam + "'.");
        }
        pos = next+1;
    }
    if(invert) {
        ret = ~ret;
    }
    widget.SetCam(ret);
}

void cCockpit::ProcessWidgetCore(node cur, cWidget::Base &widget) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        tString name = cur.GetName();
        if(name == "comment" || name == "text") continue;
        widget.Process(cur);
    }
}


void cCockpit::SetCycle(gCycle const &cycle) {
    m_ViewportPlayer = m_FocusPlayer = cycle.Player();
    if(m_FocusPlayer != 0) {
        m_FocusCycle = dynamic_cast<gCycle *>(m_FocusPlayer->Object());
    } else {
        m_FocusCycle = 0;
    }
}

void cCockpit::Render() {
    // Set HUD render context to disable animated shader effects (save/restore to avoid leak)
    rRenderContext prevCtx = sr_GetRenderContext();
    sr_SetRenderContext(rRenderContext::HUD);

    switch(m_Type) {
    case VIEWPORT_ALL:
        if(m_FocusPlayer != 0 && m_ViewportPlayer != 0) {
            // Disable depth so cockpit widgets always render on top of 3D geometry.
            // ExecutePhase re-enables depth after each phase, so we must disable here
            // to protect widgets rendered between phase flushes.
            RenderDisableState(rCapability::DepthTest);
            RenderDepthMask(false);
            Color(1,1,1);
            if(m_Player->cam) {

                if (m_FocusCycle && ( !m_Player->netPlayer || !m_Player->netPlayer->IsChatting()) && se_GameTime()>-2){
                    for(widget_list_t::const_iterator i=m_Widgets.begin(); i!=m_Widgets.end(); ++i)
                    {
                        int cam = (*i)->GetCam();
                        switch(m_Player->cam->GetCamMode()) {
                        case CAMERA_IN:
                        case CAMERA_SMART_IN:
                            if(!(cam & in)) continue;
                            break;
                        case CAMERA_CUSTOM:
                            if(!(cam & custom)) continue;
                            break;
                        case CAMERA_FREE:
                            if(!(cam & free)) continue;
                            break;
                        case CAMERA_FOLLOW:
                            if(!(cam & follow)) continue;
                            break;
                        case CAMERA_SMART:
                            if(!(cam & smart)) continue;
                            break;
                        case CAMERA_SERVER_CUSTOM:
                            if(!(cam & server_custom)) continue;
                            break;
                        case CAMERA_MER:
                            if(!(cam & mer)) continue;
                            break;
                        case CAMERA_COUNT:
                            continue; //not handled, no sense?!
                        }
                        if ((*i)->Active()) {
                            (*i)->Render();
                        }
                    }
                    //  bool displayfastest = true;// put into global, set via menusytem... subby to do.make sr_DISPLAYFASTESTout

                }
            }
        }
        break;
    case VIEWPORT_TOP:
        sr_ResetRenderState(true);
        RenderViewport(0, 0, sr_screenWidth, sr_screenWidth);

        for(widget_list_t::const_iterator i=m_Widgets.begin(); i!=m_Widgets.end(); ++i) {
            if((*i)->Active()) {
                (*i)->Render();
            }
        }
        break;
    case VIEWPORT_CYCLE: {
            if(m_ViewportPlayer == 0) {
                sr_SetRenderContext(prevCtx);
                return;
            }

            bool depth_test_was_enabled = RenderIsEnabled(rCapability::DepthTest);
            RenderDisableState(rCapability::DepthTest);

            for(widget_list_t::const_iterator i=m_Widgets.begin(); i!=m_Widgets.end(); ++i) {
                if((*i)->Active()) {
                    (*i)->Render();
                }
            }

            if(depth_test_was_enabled) {
                RenderEnableState(rCapability::DepthTest);
            }
        } break;
    }
    sr_SetRenderContext(prevCtx);
}

void cCockpit::BeforeRoundProcess() {
	FOREACH_COCKPIT(i) {
		for(widget_list_t::const_iterator w=(*i)->m_Widgets.begin(); w!=(*i)->m_Widgets.end(); ++w) {
			(*w)->BeforeRoundProcess();
    	}
    }
}

void cCockpit::AfterRoundProcess() {
	FOREACH_COCKPIT(i) {
	    for(widget_list_t::iterator iter = (*i)->m_Widgets.begin(); iter != (*i)->m_Widgets.end(); ++iter) {
			(*iter)->AfterRoundProcess();
    	}
    }
}

static void display_cockpit_lucifer() {
    static cCockpit static_cockpit(cCockpit::VIEWPORT_TOP);

    sr_ResetRenderState(true);

    // Flush any HUD geometry submitted by earlier per-frame tasks (console,
    // scores) at the current fullscreen viewport, BEFORE we change viewport
    // for per-player cockpit rendering. The HUD queue doesn't track which
    // viewport each batch was submitted at, so we must drain it at every
    // viewport boundary.
    rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);

    if (!(se_mainGameTimer &&
            se_mainGameTimer->speed > .9 &&
            se_mainGameTimer->speed < 1.1 &&
            se_mainGameTimer->IsSynced() )) return;

    rViewportConfiguration* viewportConfiguration = rViewportConfiguration::CurrentViewportConfiguration();

    // In multi-viewport mode, per-player cockpits are rendered inside the
    // viewport FBO loop (sr_RenderViewportCockpit) so the UV rotation in
    // the composite pass applies to both 3D and cockpit. (BUG 17 fix)
    // Only render per-player cockpits here in single-viewport mode.
    if (viewportConfiguration->num_viewports <= 1)
    {
        for ( int viewport = viewportConfiguration->num_viewports-1; viewport >= 0; --viewport )
        {
            int playerID = sr_viewportBelongsToPlayer[ viewport ];
            ePlayer* player = ePlayer::PlayerConfig( playerID );

            rViewport *port = viewportConfiguration->Port( viewport );
            tCoord dims = port->GetDimensions();
            port->EqualAspectBottom().Select();

            RenderDisableState(rCapability::DepthTest);
            RenderDepthMask(false);

            cCockpit *player_cockpit;
            if(!(player_cockpit = dynamic_cast<cCockpit *>(player->cockpit.get()))) {
                player_cockpit = new cCockpit(cCockpit::VIEWPORT_ALL);
                player->cockpit = player_cockpit;
            }
            {
                float factor = 4./3. / (static_cast<float>(sr_screenWidth)/static_cast<float>(sr_screenHeight));
                player_cockpit->Readjust(factor * dims.y / dims.x);
            }

            player_cockpit->SetPlayer(player);
            player_cockpit->Render();

            rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
        }
    }

    // Global VIEWPORT_TOP cockpit (clock, FPS, anything `viewport="top"`).
    // In split-screen mode it must be HIDDEN globally — the per-viewport copy
    // in sr_RenderViewportCockpit already draws it inside each viewport FBO
    // and rotates it with the player's view. Drawing it here AS WELL produces
    // a duplicate at the swapchain's natural orientation that doesn't follow
    // any player's viewport rotation (the original bug on rotated tablets).
    // Single-viewport mode keeps the global pass — no per-viewport FBO exists
    // to host the widget otherwise.
    if (viewportConfiguration->num_viewports <= 1) {
        static_cockpit.Render();
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
    }

#if 0	// Testing ground :)
    vValue::Expr::Core::Base *test = vValue::Parser::parse(tString("10"));
    std::cerr << "test: " << test->GetValue() << std::endl;
#endif
}

static rPerFrameTask dfps(&display_cockpit_lucifer);

// Render a single player's cockpit into the current viewport FBO.
// Called from RenderAllViewports in multi-viewport mode (BUG 17 fix).
//
// Aspect-ratio handling mirrors display_cockpit_lucifer (the single-
// viewport path): inside the per-viewport FBO scope sr_screenWidth /
// sr_screenHeight have been swapped to the FBO's pixel dimensions (see
// sr_BeginViewportFBO), so EqualAspectBottom() builds a square-in-pixels
// rendering region on the FBO — NDC (0.12, 0.12) renders as a true
// square regardless of how stretched the FBO itself is (e.g. 1920×540
// for the top half of a 16:9 split). Without this the cockpit widgets
// inherit the FBO's raw aspect and look smeared horizontally.
void sr_RenderViewportCockpit(int viewport, int playerID)
{
    if (!(se_mainGameTimer &&
            se_mainGameTimer->speed > .9 &&
            se_mainGameTimer->speed < 1.1 &&
            se_mainGameTimer->IsSynced() )) return;

    rViewportConfiguration* viewportConfiguration = rViewportConfiguration::CurrentViewportConfiguration();
    ePlayer* player = ePlayer::PlayerConfig(playerID);
    if (!player) return;

    rViewport *port = viewportConfiguration->Port(viewport);
    if (!port) return;

    // Switch to an aspect-square Vulkan viewport carved from the FBO's
    // bottom edge. The FBO covers the player's portion of the screen;
    // EqualAspectBottom turns NDC (0.12, 0.12) into a square in pixels.
    rViewport(0, 0, 1, 1).EqualAspectBottom().Select();

    RenderDisableState(rCapability::DepthTest);
    RenderDepthMask(false);

    cCockpit *player_cockpit;
    if(!(player_cockpit = dynamic_cast<cCockpit *>(player->cockpit.get()))) {
        player_cockpit = new cCockpit(cCockpit::VIEWPORT_ALL);
        player->cockpit = player_cockpit;
    }
    {
        // Single-viewport-style readjustment factor — works directly on
        // the FBO because sr_screenWidth/Height are already the FBO's
        // dimensions inside this scope. The previous code multiplied by
        // an extra `dims.y / dims.x` correction that double-counted the
        // viewport fraction and squashed widgets to a sliver.
        float factor = 4./3. / (static_cast<float>(sr_screenWidth)/static_cast<float>(sr_screenHeight));
        player_cockpit->Readjust(factor);
    }

    player_cockpit->SetPlayer(player);
    player_cockpit->Render();
    rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
}

static uActionGlobal cockpitKey1("COCKPIT_KEY_1");
static uActionGlobal cockpitKey2("COCKPIT_KEY_2");
static uActionGlobal cockpitKey3("COCKPIT_KEY_3");
static uActionGlobal cockpitKey4("COCKPIT_KEY_4");
static uActionGlobal cockpitKey5("COCKPIT_KEY_5");
static uActionGlobalFunc ck1(&cockpitKey1, &cCockpit::ProcessKey1, true);
static uActionGlobalFunc ck2(&cockpitKey2, &cCockpit::ProcessKey2, true);
static uActionGlobalFunc ck3(&cockpitKey3, &cCockpit::ProcessKey3, true);
static uActionGlobalFunc ck4(&cockpitKey4, &cCockpit::ProcessKey4, true);
static uActionGlobalFunc ck5(&cockpitKey5, &cCockpit::ProcessKey5, true);
uActionTooltip sc_key1Tooltip(uActionTooltip::Level_Advanced, cockpitKey1, 1);

bool ProcessKey(float i, int num) {
    bool ret = false;
    FOREACH_COCKPIT(j) {
        if((*j)->HandleEvent(num, i>0)) {
            ret = true;
        }
    }
    return ret;
}

bool cCockpit::ProcessKey1(float i)
{ 
    bool ret = ProcessKey(i, 1); 
    if (!ret && i > 0 )
    {
        sc_key1Tooltip.Count(0);
    }
    return ret;
}

bool cCockpit::ProcessKey2(float i) { return ProcessKey(i, 2); }
bool cCockpit::ProcessKey3(float i) { return ProcessKey(i, 3); }
bool cCockpit::ProcessKey4(float i) { return ProcessKey(i, 4); }
bool cCockpit::ProcessKey5(float i) { return ProcessKey(i, 5); }

bool cCockpit::HandleEvent(int id, bool state) {
    if(m_EventHandlers.count(id)){
        for(std::multimap<int, cWidget::Base *>::iterator iter = m_EventHandlers.find(id); iter != m_EventHandlers.end() && iter->first == id; ++iter) {
            iter->second->HandleEvent(state, id);
        }
        return true;
    }
    return false;
}

void cCockpit::Readjust(void) {
    if(m_Type != VIEWPORT_TOP) return;
    if (sr_screenWidth == 0) return;
    float factor = 4./3. / (static_cast<float>(sr_screenWidth)/static_cast<float>(sr_screenHeight));
    Readjust(factor);
}
void cCockpit::Readjust(float factor) {
    for(widget_list_t::iterator iter = m_Widgets.begin(); iter != m_Widgets.end(); ++iter) {
        if(cWidget::WithCoordinates *coordWidget = dynamic_cast<cWidget::WithCoordinates *>(&(*(*iter)))) {
            coordWidget->SetFactor(factor);
        }
    }
}

gCycle* cCockpit::GetFocusCycle(void) {
    return m_FocusCycle;
}

ePlayer *cCockpit::GetPlayer() {
    return m_Player;
}

ePlayerNetID *cCockpit::GetCurrentOrFocusedPlayer() {
    if(m_ViewportPlayer->CurrentTeam()){
        return m_ViewportPlayer;
    } else {
        return m_FocusPlayer;
    }
}

// Active-finger state. Stores the viewport index that owned the FINGER_DOWN
// so MOTION/UP events transform their coordinates with the *same* viewport's
// EqualAspectBottom math — even if the finger drifts into another viewport
// on the screen.
struct ActiveFingerBinding {
    cWidget::TouchButton* btn;
    int vpIdx;
};
static std::map<int64_t, ActiveFingerBinding> s_activeFingers;

// Defined here (next to s_activeFingers) but forward-declared in the same
// translation unit so cCockpit::ClearWidgets (defined above) can call it.
// Erases any finger-binding entries that point to the given button, so a
// cockpit reload that frees the button doesn't leave dangling pointers
// for the next MOTION / UP dispatch.
namespace {
    void sg_DropFingerBindings(cWidget::TouchButton* btn) {
        for (auto it = s_activeFingers.begin(); it != s_activeFingers.end(); ) {
            if (it->second.btn == btn) it = s_activeFingers.erase(it);
            else                       ++it;
        }
    }
}

namespace {
    // Look up the cCockpit instance that owns the player rendered into the
    // given viewport. Returns nullptr if either the viewport or its player
    // has no cockpit (e.g. unconfigured slot).
    cCockpit* CockpitForViewport(int vpIdx) {
        const int playerID = sr_viewportBelongsToPlayer[vpIdx];
        ePlayer* player = ePlayer::PlayerConfig(playerID);
        if (!player) return nullptr;
        return dynamic_cast<cCockpit*>(player->cockpit.get());
    }
}

bool cCockpit::ProcessTouch(float x, float y, uint32_t type, int64_t fingerId) {
    rViewportConfiguration* config = rViewportConfiguration::CurrentViewportConfiguration();
    if (!config) return false;
    const int confNum = rViewportConfiguration::CurrentConfNum();

    if (type == SDL_EVENT_FINGER_DOWN) {
        for (int i = 0; i < config->num_viewports; i++) {
            rViewport* vp = config->Port(i);
            if (!vp) continue;
            if (!vp->Contains(x, y)) continue;

            cCockpit* cockpit = CockpitForViewport(i);
            if (!cockpit) continue; // no cockpit for this viewport yet; check others

            const int rotDeg = sr_GetViewportRotationDeg(confNum, i);
            float hx, hy;
            vp->TouchToCockpitHud(x, y, hx, hy, rotDeg);

            // Readjust button positions only when the viewport factor changes.
            {
                float vpW = vp->GetDimensions().x * static_cast<float>(sr_screenWidth);
                float vpH = vp->GetDimensions().y * static_cast<float>(sr_screenHeight);
                if (rotDeg == 90 || rotDeg == 270) std::swap(vpW, vpH);
                if (vpW > 0.0f && vpH > 0.0f) {
                    float factor = 4.f/3.f * vpH / vpW;
                    static float lastFactor = -1.0f;
                    if (fabsf(factor - lastFactor) > 0.001f) {
                        cockpit->Readjust(factor);
                        lastFactor = factor;
                    }
                }
            }

            // Find the closest button to the touch point. Since only one
            // button fires per finger, we can use a generous tolerance zone
            // and just pick the nearest center — no ambiguity.
            {
                cWidget::TouchButton* bestBtn = nullptr;
                float bestDist2 = 1e30f;
                for (cWidget::TouchButton* btn : cockpit->m_TouchButtons) {
                    if (!btn->IsActiveInCurrentMode() || !btn->IsVisible())
                        continue;
                    if (btn->activeFinger_ != -1 && btn->activeFinger_ != fingerId)
                        continue; // already held by another finger
                    float dx = hx - btn->GetNDCPosition().x;
                    float dy = hy - btn->GetNDCPosition().y;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < bestDist2) {
                        bestDist2 = d2;
                        bestBtn   = btn;
                    }
                }
                // Accept if within twice the button's half-extent (generous).
                if (bestBtn) {
                    float maxR = 2.0f * std::max(bestBtn->GetNDCSize().x,
                                                  bestBtn->GetNDCSize().y);
                    if (bestDist2 <= maxR * maxR) {
                        // Release stale finger on THIS button only (missed
                        // FINGER_UP from a system interruption).
                        if (bestBtn->activeFinger_ != -1 && bestBtn->activeFinger_ != fingerId) {
                            s_activeFingers.erase(bestBtn->activeFinger_);
                            bestBtn->activeFinger_ = -1;
                            bestBtn->pressed_ = false;
                        }
                        if (bestBtn->activeFinger_ == -1) {
                            bestBtn->activeFinger_ = fingerId;
                            s_activeFingers[fingerId] = { bestBtn, i };
                            bestBtn->OnPress(i, hx, hy);
                            return true;
                        }
                    }
                }
            }
            return false; // touch was in this viewport but hit no button
        }
        return false; // touch wasn't in any viewport with a valid cockpit
    }

    // FINGER_UP / FINGER_MOTION: dispatch via the viewport the FINGER_DOWN landed in.
    auto it = s_activeFingers.find(fingerId);
    if (it == s_activeFingers.end()) return false;
    cWidget::TouchButton* btn   = it->second.btn;
    const int             vpIdx = it->second.vpIdx;

    if (type == SDL_EVENT_FINGER_UP) {
        btn->activeFinger_ = -1;
        btn->OnRelease(vpIdx);
        s_activeFingers.erase(it);
        return true;
    }

    if (type == SDL_EVENT_FINGER_MOTION) {
        rViewport* vp = (vpIdx >= 0 && vpIdx < config->num_viewports) ? config->Port(vpIdx) : nullptr;
        if (!vp) {
            // Viewport config changed mid-press; release and drop.
            btn->activeFinger_ = -1;
            btn->OnRelease(vpIdx);
            s_activeFingers.erase(it);
            return true;
        }
        const int rotDeg = sr_GetViewportRotationDeg(confNum, vpIdx);
        float hx, hy;
        vp->TouchToCockpitHud(x, y, hx, hy, rotDeg);
        // Re-apply the same factor used at FINGER_DOWN so picker drag
        // coordinates stay in the same NDC space as the button positions.
        {
            cCockpit* cockpit = CockpitForViewport(vpIdx);
            if (cockpit) {
                float vpW = vp->GetDimensions().x * static_cast<float>(sr_screenWidth);
                float vpH = vp->GetDimensions().y * static_cast<float>(sr_screenHeight);
                if (rotDeg == 90 || rotDeg == 270) std::swap(vpW, vpH);
                if (vpW > 0.0f && vpH > 0.0f)
                    cockpit->Readjust(4.f/3.f * vpH / vpW);
            }
        }
        // OnDrag returns false when the button has decided to drop the
        // binding (drag-out cancel on a non-picker button; picker buttons
        // stay bound until release). Clean up our per-finger record then.
        if (!btn->OnDrag(vpIdx, hx, hy)) {
            btn->activeFinger_ = -1;
            s_activeFingers.erase(it);
        }
        return true;
    }

    return false;
}

#endif

// Free-function wrapper so uInput.cpp can call into the cockpit without
// a circular #include dependency (uInput.h ↔ cCockpit.h).
bool cCockpit_ProcessTouch(float x, float y, uint32_t type, int64_t fingerId) {
#ifndef DEDICATED
    return cCockpit::ProcessTouch(x, y, type, fingerId);
#else
    return false;
#endif
}

