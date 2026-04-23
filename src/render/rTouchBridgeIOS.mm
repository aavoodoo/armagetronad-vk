/*
 * rTouchBridgeIOS.mm — C++ bridge for the iOS touch overlay.
 *
 * Compiled WITHOUT ARC so it can include C++ game headers safely.
 * Provides extern "C" functions called from rTouchOverlayIOS.mm.
 */
#if defined(__APPLE__) && TARGET_OS_IOS

#include <TargetConditionals.h>
#include "ePlayer.h"
#include "eCamera.h"
#include "rViewport.h"
#include "uInput.h"

// Trigger a named global action (e.g., "SCORE", "INGAME_MENU", "CONSOLE_INPUT")
extern "C" void aa_TriggerAction(const char* actionName)
{
    uAction* act = uAction::Find(actionName);
    if (act)
        uActionGlobalFunc::GlobalAct(act, 1.0f);
}

// Trigger a named player action on player 0 (CHAT, SWITCH_VIEW, etc.)
extern "C" void aa_TriggerPlayerAction(const char* actionName)
{
    uAction* act = uAction::Find(actionName);
    ePlayer* p = ePlayer::PlayerConfig(0);
    if (act && p) p->Act(act, 1.0f);
}
#include "tCallback.h"
#include "nNetwork.h"

// Returns true if chat is available (not in standalone/local game)
extern "C" bool aa_IsChatAvailable()
{
    return sn_GetNetState() != nSTANDALONE;
}

extern "C" const char* aa_getInstantChatString(int playerIndex, int chatIndex)
{
    if (chatIndex < 0 || chatIndex >= MAX_INSTANT_CHAT) return "";
    ePlayer* p = ePlayer::PlayerConfig(playerIndex);
    if (!p) return "";
    return p->instantChatString[chatIndex].c_str();
}

// Returns the number of active viewports in the current configuration.
extern "C" int aa_getNumViewports()
{
    return rViewportConfiguration::CurrentViewportConfiguration()->num_viewports;
}

// Returns position (0..1, OpenGL bottom-origin) and rotation for viewport i.
// Returns 0 if i is out of range.
extern "C" int aa_getViewportInfo(int vpIdx, float* outLeft, float* outBottom,
                                  float* outWidth, float* outHeight, int* outRotDeg)
{
    rViewportConfiguration* vc = rViewportConfiguration::CurrentViewportConfiguration();
    if (vpIdx < 0 || vpIdx >= vc->num_viewports) return 0;
    rViewport* vp = vc->Port(vpIdx);
    if (!vp) return 0;
    tCoord pos = vp->GetPosition();
    tCoord dim = vp->GetDimensions();
    *outLeft   = (float)pos.x;
    *outBottom = (float)pos.y;
    *outWidth  = (float)dim.x;
    *outHeight = (float)dim.y;
    *outRotDeg = sr_GetViewportRotationDeg(rViewportConfiguration::CurrentConfNum(), vpIdx);
    return 1;
}

#endif // TARGET_OS_IOS
