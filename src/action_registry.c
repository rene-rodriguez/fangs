#include "action_registry.h"

#include <stddef.h>

static const FangsAction ACTIONS[] = {
    {
        FANGS_ACTION_OPEN_COMMAND_PALETTE,
        "palette.open",
        "Open Command Palette",
        "Search and run Fangs actions",
        "Cmd+P / Ctrl+Shift+P",
    },
    {
        FANGS_ACTION_OPEN_SETTINGS,
        "settings.open",
        "Open Settings",
        "Edit font, theme, and AI provider settings",
        "Cmd+, / Ctrl+,",
    },
    {
        FANGS_ACTION_TOGGLE_SIDEBAR,
        "ai.toggle_sidebar",
        "Toggle AI Sidebar",
        "Show or hide the chat sidebar",
        "Cmd+B / Ctrl+Shift+B",
    },
    {
        FANGS_ACTION_INLINE_COMMAND,
        "ai.inline_command",
        "Inline Command Generation",
        "Ask AI for a command and stage it at the prompt",
        "Ctrl+Space",
    },
    {
        FANGS_ACTION_ASK_LATEST_BLOCK,
        "blocks.ask_latest",
        "Ask AI About Latest Command Block",
        "Attach the latest command block output to the AI sidebar",
        "Cmd+Shift+/ / Ctrl+Shift+/",
    },
    {
        FANGS_ACTION_SAVE_LATEST_BLOCK_WORKFLOW,
        "blocks.save_latest_workflow",
        "Save Latest Command as Runbook",
        "Append the latest command block to the project runbook file",
        "",
    },
    {
        FANGS_ACTION_FIND,
        "terminal.find",
        "Find In Terminal",
        "Search visible terminal output",
        "Cmd+F / Ctrl+F",
    },
    {
        FANGS_ACTION_COPY_SELECTION,
        "clipboard.copy",
        "Copy Selection",
        "Copy the current terminal selection",
        "Cmd+C / Ctrl+Shift+C",
    },
    {
        FANGS_ACTION_PASTE,
        "clipboard.paste",
        "Paste",
        "Paste into the terminal with bracketed paste support",
        "Cmd+V / Ctrl+Shift+V",
    },
    {
        FANGS_ACTION_NEW_TAB,
        "tab.new",
        "New Tab",
        "Open a new terminal tab",
        "Cmd+T / Ctrl+Shift+T",
    },
    {
        FANGS_ACTION_CLOSE_PANE,
        "pane.close",
        "Close Focused Pane",
        "Close the active pane or tab",
        "Cmd+W / Ctrl+Shift+W",
    },
    {
        FANGS_ACTION_SPLIT_RIGHT,
        "pane.split_right",
        "Split Pane Right",
        "Open a new pane to the right of the focused pane",
        "Cmd+D / Ctrl+Shift+D",
    },
    {
        FANGS_ACTION_SPLIT_DOWN,
        "pane.split_down",
        "Split Pane Down",
        "Open a new pane below the focused pane",
        "Cmd+Shift+D / Ctrl+Shift+Alt+D",
    },
    {
        FANGS_ACTION_FOCUS_LEFT,
        "pane.focus_left",
        "Focus Pane Left",
        "Move focus to the pane on the left",
        "Cmd+Opt+Left / Ctrl+Shift+Left",
    },
    {
        FANGS_ACTION_FOCUS_RIGHT,
        "pane.focus_right",
        "Focus Pane Right",
        "Move focus to the pane on the right",
        "Cmd+Opt+Right / Ctrl+Shift+Right",
    },
    {
        FANGS_ACTION_FOCUS_UP,
        "pane.focus_up",
        "Focus Pane Up",
        "Move focus to the pane above",
        "Cmd+Opt+Up / Ctrl+Shift+Up",
    },
    {
        FANGS_ACTION_FOCUS_DOWN,
        "pane.focus_down",
        "Focus Pane Down",
        "Move focus to the pane below",
        "Cmd+Opt+Down / Ctrl+Shift+Down",
    },
    {
        FANGS_ACTION_FONT_INCREASE,
        "font.increase",
        "Increase Font Size",
        "Grow the terminal font size",
        "Cmd+= / Ctrl+=",
    },
    {
        FANGS_ACTION_FONT_DECREASE,
        "font.decrease",
        "Decrease Font Size",
        "Shrink the terminal font size",
        "Cmd+- / Ctrl+-",
    },
    {
        FANGS_ACTION_FONT_RESET,
        "font.reset",
        "Reset Font Size",
        "Restore the default terminal font size",
        "Cmd+0 / Ctrl+0",
    },
    {
        FANGS_ACTION_TOGGLE_WORKSPACE_RAIL,
        "workspace.toggle_rail",
        "Toggle Workspace Rail",
        "Show or hide vertical tabs, panes, and notifications",
        "",
    },
};

const FangsAction *action_registry_all(int *out_count)
{
    if (out_count)
        *out_count = (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]));
    return ACTIONS;
}

const FangsAction *action_registry_find(FangsActionId id)
{
    int count = 0;
    const FangsAction *actions = action_registry_all(&count);
    for (int i = 0; i < count; i++) {
        if (actions[i].id == id)
            return &actions[i];
    }
    return NULL;
}
