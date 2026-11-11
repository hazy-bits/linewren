#include "Processes.r"
#include "Dialogs.r"
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
        "-", noIcon, noKey, noMark, plain;
        "Close", noIcon, "W", noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Open...", noIcon, "O", noMark, plain;
        "Add Folder...", noIcon, noKey, noMark, plain;
        "Save", noIcon, "S", noMark, plain;
        "Save As...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quick Open...", noIcon, "P", noMark, plain;
    }
};

resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Edit";
    {
        "Undo", noIcon, "Z", noMark, plain;
        "Redo", noIcon, noKey, noMark, plain;
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
        "Replace...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Link", noIcon, "K", noMark, plain;
        "Heading", noIcon, noKey, noMark, plain;
        "Quote", noIcon, noKey, noMark, plain;
        "List", noIcon, noKey, noMark, plain;
        "Task", noIcon, noKey, noMark, plain;
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
        "Focus", noIcon, noKey, noMark, plain;
        "Typewriter Scrolling", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Show Word Count", noIcon, noKey, noMark, plain;
        "Show Character Count", noIcon, noKey, noMark, plain;
        "Hide Count", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Outline", noIcon, "2", noMark, plain;
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

resource 'WIND' (128) {
    {0, 0, 190, 340}, altDBoxProc;
    invisible;
    noGoAway;
    0, "";
    noAutoCenter;
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
        {43, 18, 63, 322}, EditText { enabled, "Untitled.md" };
    }
};

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
    2048 * 1024,
    384 * 1024
};
