#include "Clipboard.h"

#import <AppKit/AppKit.h>

namespace dooblr::installer
{
    juce::int64 clipboardChangeCount()
    {
        @autoreleasepool
        {
            return (juce::int64) [[NSPasteboard generalPasteboard] changeCount];
        }
    }
}
