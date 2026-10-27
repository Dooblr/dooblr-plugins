#!/bin/sh
# Ad-hoc signs a plug-in bundle so `codesign --verify --deep --strict` passes.
# The project lives in a File Provider synced ~/Documents, which can re-add Finder info to the
# bundle while it is being signed, so strip extended attributes and retry a few times.
bundle="$1"

for attempt in 1 2 3 4 5 6 7 8; do
    for attr in com.apple.quarantine com.apple.FinderInfo com.apple.ResourceFork; do
        xattr -r -d "$attr" "$bundle" 2>/dev/null
    done

    if codesign --force --sign - "$bundle" 2>/dev/null && codesign --verify --deep --strict "$bundle" 2>/dev/null; then
        exit 0
    fi

    sleep 0.5
done

codesign --verify --deep --strict "$bundle"
exit 1
