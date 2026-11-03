#pragma once

#include <juce_core/juce_core.h>

namespace dooblr::installer
{
    /** A number that changes whenever anything is copied (macOS NSPasteboard changeCount, Windows
        GetClipboardSequenceNumber), or -1 where the system has none. Reading it does not read the
        clipboard, so the installer reads the text only after something new was copied. */
    juce::int64 clipboardChangeCount();
}
