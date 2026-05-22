#pragma once
/*
 * rTouchOverlayIOS.h — iOS touch overlay: corner buttons for game actions.
 *
 * Two semi-transparent buttons in the top corners, active in all touch modes:
 *
 *  LEFT button:
 *    - Single tap  → score table (TAB)
 *    - Double tap  → game menu   (ESC)
 *    - Hold + drag → pick touch mode (1/2/3); release on item to switch,
 *                    release elsewhere to cancel
 *
 *  RIGHT button:
 *    - Single tap  → chat   (RETURN)
 *    - Double tap  → console (GRAVE/backtick)
 *    - Hold + drag → pick instant-chat string (first 8); release on item
 *                    to send, release elsewhere to cancel
 *
 * Install once after the SDL window has been created:
 *   aa_installTouchOverlay();
 */

#ifdef __cplusplus
extern "C" {
#endif

void aa_installTouchOverlay(void);

#ifdef __cplusplus
}
#endif
