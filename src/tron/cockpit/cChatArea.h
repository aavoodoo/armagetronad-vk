/*
 * cChatArea.h — Cockpit widget that renders the chat/console scroll.
 * When present in a cockpit, disables the default rConsole::Render().
 */

#ifndef CCHATAREA_H
#define CCHATAREA_H

#include "cockpit/cWidgetBase.h"

#ifndef DEDICATED

namespace cWidget {

class ChatArea : public WithCoordinates {
    float bgAlpha_ = 0.5f;  // background opacity (0=transparent, 1=opaque)
public:
    ChatArea();
    ~ChatArea() override;

    void Render() override;
    bool Process(tXmlParser::node cur) override;
    void PostParsingProcess() override;
};

} // namespace cWidget

#endif // DEDICATED
#endif // CCHATAREA_H
