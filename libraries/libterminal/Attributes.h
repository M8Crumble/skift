#pragma once

#include <libsystem/Common.h>
#include <libterminal/Color.h>

struct TerminalAttributes
{
    TerminalColor foreground;
    TerminalColor background;

    bool bold;
    bool inverted;
    bool underline;
};

TerminalAttributes terminal_attributes_bold(TerminalAttributes attributes);

TerminalAttributes terminal_attributes_inverted(TerminalAttributes attributes);

TerminalAttributes terminal_attributes_underline(TerminalAttributes attributes);

TerminalAttributes terminal_attributes_regular(TerminalAttributes attributes);

TerminalAttributes terminal_attributes_with_foreground(TerminalAttributes attributes, TerminalColor foreground);

TerminalAttributes terminal_attributes_with_background(TerminalAttributes attributes, TerminalColor background);

bool terminal_attributes_equals(TerminalAttributes left, TerminalAttributes right);
