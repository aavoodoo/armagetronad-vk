/*
 * cChatArea.cpp — Cockpit widget that renders the chat/console scroll.
 * When present, disables the default rConsole::Render() via sr_chatAreaActive.
 */

#include "cockpit/cChatArea.h"

#ifndef DEDICATED

#include "rConsole.h"
#include "rFont.h"
#include "rScreen.h"
#include "rRender.h"
#include "rRenderQueue.h"
#include "rVertex.h"
#include "tSysTime.h"
#include "tColor.h"
#include "rViewport.h"
#include <algorithm>

// Defined in rConsoleGraph.cpp — set to true to suppress default rendering.
extern bool sr_chatAreaActive;
extern rConsole sr_con;


namespace cWidget {

float ChatArea::s_lastBottom_ = 1.0f;
int   ChatArea::s_lastTop_    = 0;
int   ChatArea::s_lastIn_     = 0;

ChatArea::ChatArea() {}

ChatArea::~ChatArea()
{
    // If this widget is destroyed (cockpit reload), re-enable default console.
    sr_chatAreaActive = false;
}

bool ChatArea::Process(tXmlParser::node cur)
{
    if (WithCoordinates::Process(cur))
        return true;
    if (cur.IsOfType("ChatArea")) {
        if (cur.HasProp("bgAlpha"))
            cur.GetProp("bgAlpha", bgAlpha_);
        return true;
    }
    return false;
}

void ChatArea::PostParsingProcess()
{
    // Disable the hardcoded rConsole::Render() — this widget takes over.
    // The touch overlay only loads when ENABLE_TOUCH >= 1, so this is
    // safe — desktop never reaches here.
    sr_chatAreaActive = true;
    // Reset prediction state so a fresh cockpit load doesn't see stale values.
    s_lastBottom_ = 1.0f;
    s_lastTop_    = 0;
    s_lastIn_     = 0;
}

void ChatArea::Render()
{
    if (!sr_glOut) return;
    // Hide in split-screen — same as default console (no clean home in
    // any single viewport, shouldn't appear globally either).
    {
        rViewportConfiguration* vp = rViewportConfiguration::CurrentViewportConfiguration();
        if (vp && vp->num_viewports > 1) return;
    }

    static int dbgCount = 0;
    if (dbgCount++ < 5) {
        SDL_Log("[ChatArea] pos=(%.3f,%.3f) size=(%.3f,%.3f) useAnchorPos=%d useAnchorSize=%d",
                m_position.x, m_position.y, m_size.x, m_size.y,
                m_useAnchorPos, m_useAnchorSize);
    }

    // Widget bounds in NDC [-1, +1].
    const float left  = m_position.x - m_size.x;
    const float right = m_position.x + m_size.x;
    const float top   = m_position.y + m_size.y;
    const float bot   = m_position.y - m_size.y;
    const float areaW = right - left;
    const float areaH = top - bot;
    if (areaW <= 0 || areaH <= 0) return;

    // Compute character height to fit the area. Use a font size proportional
    // to the area height, with a sensible maximum number of visible lines.
    const float W = static_cast<float>(sr_screenWidth);
    const float H = static_cast<float>(sr_screenHeight);
    // Character height: same as default console (rConsoleGraph.cpp).
    float charH = 31.0f * 2.0f / H;
    // This widget only loads when touch is enabled (ProcessTouchOverlay
    // gates on ENABLE_TOUCH >= 1). On touch devices, cap at 4 lines
    // to avoid covering gameplay. Desktop uses the default console.
    const int maxVisibleLines = 4;

    // Advance timeout — same logic as rConsole::Render().
    double now = tSysTimeFloat();
    {
        double lastTimeout = sr_con.LastTimeout();
        double timeout     = sr_con.GetTimeout();
        int    top_line    = sr_con.CurrentTop();
        int    in_line     = sr_con.CurrentIn();
        // Note: we can't modify sr_con's currentTop here since it's private.
        // The default console's per-frame timeout advancement still runs in
        // sr_ConsolePerFrame → rConsole::Render() is skipped but the timeout
        // state is managed externally. For the widget, we just render what's
        // currently visible.
        (void)lastTimeout; (void)timeout; (void)now;
    }

    const auto& lines  = sr_con.Lines();
    int currentIn       = sr_con.CurrentIn();
    // Show only the last maxVisibleLines lines (most recent messages).
    int currentTop      = std::max(sr_con.CurrentTop(), currentIn - maxVisibleLines);

    if (currentTop >= currentIn) return;  // nothing to show
    int maxH            = maxVisibleLines;

    // Predict content bottom for background sizing (same approach as
    // rConsole::Render — track the previous frame's actual text bottom
    // and adjust for line count changes).
    REAL predictBottom = s_lastBottom_ - (s_lastTop_ - currentTop + currentIn - s_lastIn_) * charH;
    if (s_lastTop_ != currentTop || s_lastIn_ != currentIn) {
        s_lastTop_ = currentTop;
        s_lastIn_  = currentIn;
    }

    // Background: auto-sized from predicted text bottom to widget top.
    if (bgAlpha_ > 0.001f && predictBottom < top) {
        REAL bgBot = predictBottom - 0.4f * charH;
        if (bgBot < bot) bgBot = bot;
        uint8_t a = static_cast<uint8_t>(bgAlpha_ * 255.0f);
        rVertex20 v0(left,  bgBot, 0, 0, 0, 0, a, 0, 0);
        rVertex20 v1(right, bgBot, 0, 0, 0, 0, a, 0, 0);
        rVertex20 v2(right, top,   0, 0, 0, 0, a, 0, 0);
        rVertex20 v3(left,  top,   0, 0, 0, 0, a, 0, 0);
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, v0, v1, v2, v3);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
    }

    // Create text field at the widget's top-left corner.
    rTextField out(rTextField::Pixelize(left, W),
                   rTextField::Pixelize(top, H),
                   charH, sr_fontClass::sr_fontConsole);
    out.SetWidth(areaW);
    out.EnableLineWrap();
    out.SetIndent(3);

    // Render lines.
    rTextField::SetDefaultColor(tColor(1, 1, 1));
    int i;
    for (i = currentTop; i < lines.Len() && i <= currentIn && i <= currentTop + maxH; i++) {
        if (lines[i].Len() > 1) {
            rTextField::SetDefaultColor(tColor(1, 1, 1));
            out << lines[i];
            out.ResetColor();
        }
    }
    if (i < currentIn) {
        out << tColoredString::ColorString(1, .8, .5) << "   v   v   v   v   v\n";
    }

    // Track actual text bottom for next frame's background prediction.
    s_lastBottom_ = out.GetBottom();
}

} // namespace cWidget

#endif // DEDICATED
