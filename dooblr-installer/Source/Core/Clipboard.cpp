#include "Clipboard.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace dooblr::installer
{
    juce::int64 clipboardChangeCount()
    {
       #if JUCE_WINDOWS
        return (juce::int64) GetClipboardSequenceNumber();
       #else
        return -1;
       #endif
    }
}
