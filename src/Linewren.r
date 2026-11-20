#include "Processes.r"
#include "Dialogs.r"
#include "Controls.r"
#include "Menus.r"
#include "Windows.r"
#include "MacTypes.r"

resource 'MENU' (128) {
    128, textMenuProc;
    allEnabled, enabled;
    apple;
    {
        "About Linewren...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "File";
    {
        "New", noIcon, "N", noMark, plain;
        "Open...", noIcon, "O", noMark, plain;
        "Open Recent", noIcon, noKey, noMark, plain;
        "Quick Open...", noIcon, "P", noMark, plain;
        "Add Folder...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Close", noIcon, "W", noMark, plain;
        "Save", noIcon, "S", noMark, plain;
        "Save As...", noIcon, "S", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Open as MacRoman...", noIcon, noKey, noMark, plain;
        "Reload from Disk", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Edit";
    {
        "Undo", noIcon, "Z", noMark, plain;
        "Redo", noIcon, "Z", noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
        "Select All", noIcon, "A", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Bold", noIcon, "B", noMark, plain;
        "Italic", noIcon, "I", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Find...", noIcon, "F", noMark, plain;
        "Find Next", noIcon, "G", noMark, plain;
        "Find Previous", noIcon, "G", noMark, plain;
        "Replace...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Link", noIcon, "K", noMark, plain;
        "Heading", noIcon, noKey, noMark, plain;
        "Quote", noIcon, noKey, noMark, plain;
        "List", noIcon, noKey, noMark, plain;
        "Task", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Spelling", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (139) {
    139, textMenuProc;
    allEnabled, enabled;
    "Spelling";
    {
        "Spelling...", noIcon, ":", noMark, plain;
        "Check Spelling", noIcon, ";", noMark, plain;
        "Check Spelling as You Type", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (131) {
    131, textMenuProc;
    allEnabled, enabled;
    "View";
    {
        "Light", noIcon, noKey, noMark, plain;
        "Dark", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Sidebar", noIcon, "1", noMark, plain;
        "Outline", noIcon, "2", noMark, plain;
        "Focus Layout", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Line Focus", noIcon, noKey, noMark, plain;
        "Paragraph Focus", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Typewriter Scrolling", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Show Word Count", noIcon, noKey, noMark, plain;
        "Show Character Count", noIcon, noKey, noMark, plain;
        "Hide Count", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (132) {
    132, textMenuProc;
    allEnabled, enabled;
    "";
    {
        "New Markdown File...", noIcon, noKey, noMark, plain;
        "Rename...", noIcon, noKey, noMark, plain;
        "Reveal in Finder", noIcon, noKey, noMark, plain;
        "Remove from Sidebar", noIcon, noKey, noMark, plain;
        "Delete...", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (136) {
    136, textMenuProc;
    allEnabled, enabled;
    "";
    {
        "Cut", noIcon, noKey, noMark, plain;
        "Copy", noIcon, noKey, noMark, plain;
        "Paste", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Find in Document", noIcon, noKey, noMark, plain;
        "Search the Web", noIcon, noKey, noMark, plain;
        "Look Up in Dictionary", noIcon, noKey, noMark, plain;
    }
};

resource 'MBAR' (128) {
    { 128, 129, 130 };
};

resource 'MENU' (137) {
    137, textMenuProc;
    allEnabled, enabled;
    "Window";
    {
        "Zoom", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

resource 'DLOG' (128) {
    {90, 90, 272, 460}, movableDBoxProc;
    invisible, noGoAway, 0, 128, "About Linewren";
    centerMainScreen;
};

resource 'DITL' (128) {
    {
        {142, 272, 163, 352}, Button { enabled, "OK" };
        {22, 20, 54, 52}, UserItem { disabled };
        {18, 68, 38, 350}, StaticText { disabled, "Linewren" };
        {48, 68, 68, 350}, StaticText { disabled, "A quiet place to write." };
        {78, 68, 126, 350}, StaticText { disabled,
            "Markdown and plain text for PowerPC and classic Macintosh computers." };
    }
};

resource 'ALRT' (129) {
    {85, 85, 205, 447}, 129,
    { Cancel, visible, silent; Cancel, visible, silent;
      Cancel, visible, silent; Cancel, visible, silent; };
    centerMainScreen;
};

resource 'DITL' (129) {
    {
        {83, 260, 104, 345}, Button { enabled, "Save..." };
        {83, 166, 104, 249}, Button { enabled, "Cancel" };
        {83, 18, 104, 103}, Button { enabled, "Discard" };
        {14, 62, 68, 344}, StaticText { disabled,
            "This document has unsaved changes. Save them to a text file before closing?" };
    }
};

resource 'DLOG' (130) {
    {90, 90, 211, 430}, movableDBoxProc;
    invisible, noGoAway, 0, 130, "New Markdown File";
    centerMainScreen;
};

resource 'dlgx' (130) {
    versionZero { kDialogFlagsUseThemeBackground | kDialogFlagsUseControlHierarchy |
                  kDialogFlagsHandleMovableModal | kDialogFlagsUseThemeControls }
};

resource 'DITL' (130) {
    {
        {80, 242, 101, 324}, Button { enabled, "Create" };
        {80, 148, 101, 230}, Button { enabled, "Cancel" };
        {16, 16, 34, 324}, StaticText { disabled, "Name the new Markdown file:" };
#if TARGET_API_MAC_CARBON
        {43, 18, 63, 322}, EditText { enabled, "Untitled.md" };
#else
        {43, 18, 63, 322}, Control { enabled, 135 };
#endif
    }
};

#if !TARGET_API_MAC_CARBON
resource 'CNTL' (135) {
    {43, 18, 63, 322}, 0, visible, 0, 0, 272, 0, "";
};
#endif

resource 'ALRT' (131) {
    {85, 85, 205, 447}, 131,
    { OK, visible, silent; OK, visible, silent;
      OK, visible, silent; OK, visible, silent; };
    centerMainScreen;
};

resource 'DITL' (131) {
    {
        {83, 166, 104, 244}, Button { enabled, "Cancel" };
        {83, 254, 104, 345}, Button { enabled, "Delete" };
        {14, 62, 68, 344}, StaticText { disabled, "Delete ^0 by moving it to the Trash?" };
    }
};

resource 'ALRT' (132) {
    {85, 85, 205, 467}, 132,
    { Cancel, visible, silent; Cancel, visible, silent;
      Cancel, visible, silent; Cancel, visible, silent; };
    centerMainScreen;
};

resource 'DITL' (132) {
    {
        {83, 265, 104, 365}, Button { enabled, "Save As..." };
        {83, 175, 104, 254}, Button { enabled, "Cancel" };
        {83, 18, 104, 159}, Button { enabled, "Discard and Reload" };
        {14, 62, 68, 365}, StaticText { disabled,
            "Reloading replaces your unsaved edits with the file on disk. Use Save As to keep your edits in a separate file." };
    }
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
#if TARGET_API_MAC_CARBON
    2048 * 1024,
#else
    8192 * 1024,
#endif
    384 * 1024
};

resource 'DLOG' (133) {
    {90, 90, 337, 509}, movableDBoxProc;
    invisible, noGoAway, 0, 133, "Spelling";
    centerMainScreen;
};

resource 'dlgx' (133) {
    versionZero { kDialogFlagsUseThemeBackground | kDialogFlagsUseControlHierarchy |
                  kDialogFlagsHandleMovableModal | kDialogFlagsUseThemeControls }
};

resource 'DITL' (133) {
    {
        {18, 299, 40, 401}, Button { enabled, "Change" };
        {210, 299, 232, 401}, Button { enabled, "Done" };
        {52, 299, 74, 401}, Button { enabled, "Find Next" };
        {86, 299, 108, 401}, Button { enabled, "Ignore" };
        {35, 18, 55, 280}, Control { disabled, 134 };
        {83, 18, 199, 280}, Control { enabled, 133 };
        {16, 18, 34, 280}, StaticText { disabled, "Misspelled word" };
        {62, 18, 81, 280}, StaticText { disabled, "Suggestions" };
        {210, 84, 232, 280}, Button { enabled, "Language" };
        {213, 18, 232, 82}, StaticText { disabled, "Language" };
    }
};

resource 'CNTL' (133) {
    {83, 18, 199, 280}, 0, visible, 0, 0, 352, 0, "";
};

resource 'CNTL' (134) {
    {35, 18, 55, 280}, 0, visible, 0, 0, 288, 0, "";
};
