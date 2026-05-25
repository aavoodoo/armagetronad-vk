/*
 * rTouchBridgeAndroid.cpp — JNI bridge for the Android touch overlay.
 *
 * Exposes native methods called from ArmagetronActivity.java.
 */
#ifdef __ANDROID__

#include <jni.h>
#include <cstring>
#include <SDL3/SDL.h>
#include "ePlayer.h"
#include "gGame.h"
#include "uInput.h"
#include "nNetwork.h"

// Flag: true only when the game explicitly requested the keyboard (chat/console),
// not when SDL_StartTextInput fires for generic menu string editing.
bool sr_androidKeyboardExplicit = false;

// su_SetEnableTouch / su_SetGyroActive are defined in uInput.cpp with extern "C" linkage
extern "C" void su_SetEnableTouch(int mode);
extern "C" void su_SetGyroActive(int active);
// Camera actions defined in eCamera.cpp with extern "C" linkage
extern "C" void aa_SetGlanceForward(bool active);
extern "C" void aa_SwitchCameraView(void);
extern "C" void aa_SetCameraFrozen(bool frozen);

// ---------------------------------------------------------------------------
// Inject an SDL key down + up event pair by scancode
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeInjectSdlKey(
        JNIEnv* /*env*/, jclass /*cls*/, jint scancode)
{
    SDL_Event down{}, up{};
    down.type         = SDL_EVENT_KEY_DOWN;
    down.key.scancode = static_cast<SDL_Scancode>(scancode);
    down.key.down     = true;
    SDL_PushEvent(&down);

    up.type         = SDL_EVENT_KEY_UP;
    up.key.scancode = static_cast<SDL_Scancode>(scancode);
    up.key.down     = false;
    SDL_PushEvent(&up);
}

// ---------------------------------------------------------------------------
// Returns true when a game round is currently active
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeIsGameRunning(
        JNIEnv* /*env*/, jclass /*cls*/)
{
    return sg_GameRunning() ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// Returns the instant-chat string for the given player/slot index
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeGetInstantChatString(
        JNIEnv* env, jclass /*cls*/, jint playerIndex, jint chatIndex)
{
    if (chatIndex < 0 || chatIndex >= MAX_INSTANT_CHAT)
        return env->NewStringUTF("");
    ePlayer* p = ePlayer::PlayerConfig(playerIndex);
    if (!p) return env->NewStringUTF("");
    return env->NewStringUTF(p->instantChatString[chatIndex].c_str());
}

// ---------------------------------------------------------------------------
// Sets the touch steering mode (1, 2, or 3)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeSetTouchMode(
        JNIEnv* /*env*/, jclass /*cls*/, jint mode)
{
    su_SetEnableTouch(static_cast<int>(mode));
}

// Gyro toggle: activates/deactivates SDL3 sensor-based gyro camera look.
// Replaces the old Java gravity listener + nativeSetGyroCameraInput bridge.
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeSetGyroActive(
        JNIEnv* /*env*/, jclass /*cls*/, jboolean active)
{
    su_SetGyroActive(active ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Camera button: glance forward (hold = true while finger is down)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeSetGlanceForward(
        JNIEnv* /*env*/, jclass /*cls*/, jboolean active)
{
    aa_SetGlanceForward(active == JNI_TRUE);
}

// ---------------------------------------------------------------------------
// Camera button: switch camera view (one-shot on quick tap release)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeSwitchCameraView(
        JNIEnv* /*env*/, jclass /*cls*/)
{
    aa_SwitchCameraView();
}

// ---------------------------------------------------------------------------
// Freeze/unfreeze camera direction (below-right toggle button)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeSetCameraFrozen(
        JNIEnv* /*env*/, jclass /*cls*/, jboolean frozen)
{
    aa_SetCameraFrozen(frozen == JNI_TRUE);
}

// ---------------------------------------------------------------------------
// Returns the number of active viewports
// ---------------------------------------------------------------------------
#include "rViewport.h"
extern int sr_GetViewportRotationDeg(int confNum, int vpIdx);

extern "C" JNIEXPORT jint JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeGetNumViewports(
        JNIEnv* /*env*/, jclass /*cls*/)
{
    return rViewportConfiguration::CurrentViewportConfiguration()->num_viewports;
}

// ---------------------------------------------------------------------------
// Returns viewport rect (left, bottom, width, height in 0..1 OpenGL coords)
// and rotation in degrees. Returns false if vpIdx is out of range.
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeGetViewportInfo(
        JNIEnv* env, jclass /*cls*/, jint vpIdx,
        jfloatArray outRect, jintArray outRot)
{
    rViewportConfiguration* vc = rViewportConfiguration::CurrentViewportConfiguration();
    if (vpIdx < 0 || vpIdx >= vc->num_viewports) return JNI_FALSE;
    rViewport* vp = vc->Port(vpIdx);
    if (!vp) return JNI_FALSE;

    tCoord pos = vp->GetPosition();
    tCoord dim = vp->GetDimensions();
    float rect[4] = { (float)pos.x, (float)pos.y, (float)dim.x, (float)dim.y };
    int rot = sr_GetViewportRotationDeg(rViewportConfiguration::CurrentConfNum(), vpIdx);

    env->SetFloatArrayRegion(outRect, 0, 4, rect);
    env->SetIntArrayRegion(outRot, 0, 1, &rot);
    return JNI_TRUE;
}

// ---------------------------------------------------------------------------
// Returns true if chat is available (networked game, not standalone)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeIsChatAvailable(
        JNIEnv* /*env*/, jclass /*cls*/)
{
    return (sn_GetNetState() != nSTANDALONE) ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// Trigger a named global action (SCORE, INGAME_MENU, CONSOLE_INPUT, etc.)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeTriggerAction(
        JNIEnv* env, jclass /*cls*/, jstring actionName)
{
    const char* name = env->GetStringUTFChars(actionName, nullptr);
    if (name)
    {
        // Track explicit keyboard requests (CONSOLE_INPUT triggers keyboard)
        if (strcmp(name, "CONSOLE_INPUT") == 0)
            sr_androidKeyboardExplicit = true;

        uAction* act = uAction::Find(name);
        if (act)
            uActionGlobalFunc::GlobalAct(act, 1.0f);
        env->ReleaseStringUTFChars(actionName, name);
    }
}

// ---------------------------------------------------------------------------
// Trigger a named player action on player 0 (CHAT, SWITCH_VIEW, etc.)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_org_armagetronad_game_ArmagetronActivity_nativeTriggerPlayerAction(
        JNIEnv* env, jclass /*cls*/, jstring actionName)
{
    const char* name = env->GetStringUTFChars(actionName, nullptr);
    if (name)
    {
        // Track explicit keyboard requests (CHAT triggers keyboard)
        if (strcmp(name, "CHAT") == 0)
            sr_androidKeyboardExplicit = true;

        uAction* act = uAction::Find(name);
        ePlayer* p = ePlayer::PlayerConfig(0);
        if (act && p)
            p->Act(act, 1.0f);
        env->ReleaseStringUTFChars(actionName, name);
    }
}

#endif // __ANDROID__
