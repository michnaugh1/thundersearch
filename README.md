# ThunderSearch

A fast, keyboard-driven launcher for Ubuntu GNOME. Search and launch applications, navigate files, switch windows, evaluate math, and interact with Claude — all from a single search bar that appears instantly wherever your cursor is.

Built with C and GTK4. Runs under XWayland for reliable window positioning on GNOME Wayland sessions.

Inspired by [OLauncher](https://github.com/tanujnotes/Olauncher).

## Features

- **App launcher** — type to search, auto-launches when narrowed to one result
- **File navigator** — browse and open files and directories with `/f`
- **Window switcher** — focus any open window across workspaces with `/win`
- **Calculator** — evaluate math expressions inline with `=`
- **Claude AI** — quick inline answers with `ai`, or open a full Claude Code session with `cc`
- **Multi-monitor** — always appears centered on whichever monitor your cursor is on
- **Autostart** — runs as a daemon, triggered by a compositor keybinding

---

## Building

### Dependencies

```
sudo apt install libgtk-4-dev libgtk4-layer-shell-dev
```

- GTK4
- gtk4-layer-shell
- GLib / GIO
- libX11 (for XWayland positioning)
- niri compositor (for `/win` window switching)

### Compile

```bash
make
```

### Install

```bash
sudo make install          # installs to /usr/local/bin
make install-autostart     # adds to ~/.config/autostart
```

To apply a new build, kill the running instance first:

```bash
pkill thundersearch && ./thundersearch
```

---

## Setup

ThunderSearch runs as a background daemon. Bind a key in your compositor or GNOME settings to run:

```
/usr/local/bin/thundersearch
```

Each press toggles the launcher. Press `Escape` to dismiss without doing anything.

---

## Modes

### App launcher (default)

Just type. Results narrow as you type. When only one match remains, it launches automatically.

```
firefox       → launches Firefox
sp            → narrows to Spotify, launches when it's the only match
```

Use **nicknames** in the config file to map short aliases to app names:

```
ff            → launches Firefox  (if ff = Firefox in config)
term          → launches your terminal
```

---

### File navigation — `/f`, `/fd`, `/f/o`

Browse and open files starting from your home directory.

| Input | Behavior |
|---|---|
| `/f ` | List home directory |
| `/f Documents` | Open Documents in file manager |
| `/f /Documents/` | Browse inside Documents (path mode) |
| `/f /Documents/project/report` | Narrow to matching files |
| `/f/o /path/file.pdf` | Open file with configured app (or `xdg-open`) |
| `/fd ` | List your configured default directory |
| `/fd /subdir/` | Browse inside default directory |

**Path mode** (starts with `/f /`):
1. Type to narrow results
2. Directories auto-fill and expand when narrowed to one match
3. Files open when narrowed to one match
4. Backspace works naturally — the path in the bar is the state

---

### Window switcher — `/win`

Search and focus any open window across all workspaces. Requires the **niri** compositor.

```
/win          → list all open windows
/win fire     → narrows to Firefox
/win code     → narrows to VS Code
```

Auto-focuses when narrowed to one result. Press `Enter` to focus the first result immediately.

---

### Calculator — `=`

Type `=` followed by any math expression. The result updates live as you type. Press `Enter` to copy the result to the clipboard.

```
= 2 + 2            → 4
= sqrt(144)        → 12
= 2^10             → 1024
= pi * 5^2         → 78.53981634
= (100 - 32) / 1.8 → 37.77777778
```

**Supported:** `+` `-` `*` `/` `%` `^` `()` · `sqrt` `abs` `floor` `ceil` `round` · `sin` `cos` `tan` `log` `ln` `log2` `exp` · constants `pi` `e` `tau`

---

### Quick Claude answer — `ai`

Type `ai` followed by your question. Press `Enter` to send it to Claude (`claude -p`). The response appears inline. Press `Enter` again to copy it to the clipboard.

```
ai what flag does rsync use to delete files on the destination
ai bash one-liner to find all files modified in the last 24 hours
ai difference between tcp and udp in one sentence
```

Useful for quick lookups without switching context to a terminal. Requires [Claude Code](https://claude.ai/code) to be installed.

---

### Claude Code session — `cc`

Type `cc` followed by a directory path to open a Claude Code session in that directory. Press `Enter` to launch your terminal with `claude` running inside it.

```
cc ~/Dev/myproject
cc ~/Dev/thundersearch
cc /etc
```

Supports `~` expansion. Shows a warning if the directory doesn't exist. Requires [Claude Code](https://claude.ai/code) to be installed.

---

## Configuration

Config file: `~/.config/thundersearch/config`

Created automatically on first run with commented examples.

### Nicknames

Map short aliases to application names:

```
ff   = Firefox
spot = Spotify
term = Alacritty
code = Visual Studio Code
```

### Appearance

```
set win_width  = 680    # window width in pixels
set top_offset = 120    # distance from top of monitor in pixels
```

### Result limits

```
set max_app_results  = 10
set max_file_results = 50
set max_win_results  = 50
```

### Default directory

Sets the starting directory for `/fd`:

```
set default_dir = ~/Projects
```

### Terminal emulator

Override which terminal `cc` opens (autodetected by default via `xdg-terminal-exec`):

```
set terminal = kitty
```

### File openers

Configure which app opens which file types with `/f/o`. Falls back to `xdg-open` for unmatched extensions:

```
open .pdf .epub           = zathura
open .png .jpg .jpeg .gif = imv
open .mp4 .mkv .avi .webm = mpv
open .txt .md .cfg .conf  = gedit
```

### Full example

```ini
# Nicknames
ff   = Firefox
spot = Spotify
term = Alacritty

# Appearance
set win_width  = 680
set top_offset = 120

# Result limits
set max_app_results  = 10
set max_file_results = 50

# Default directory for /fd
set default_dir = ~/Projects

# File openers for /f/o
open .pdf .epub           = zathura
open .png .jpg .jpeg .gif = imv
open .mp4 .mkv .avi .webm = mpv
open .txt .md .cfg .conf  = gedit
```

---

## Command reference

| Input | Description |
|---|---|
| `text` | Search and launch applications |
| `/f ` | Browse home directory |
| `/f name` | Open directory or file by name |
| `/f /path/` | Navigate path progressively |
| `/f/o /path/file` | Open file with configured or default app |
| `/fd ` | Browse configured default directory |
| `/win` | List all open windows |
| `/win query` | Search windows by title or app |
| `= expr` | Evaluate math expression, Enter copies result |
| `ai query` | Ask Claude inline, Enter copies response |
| `cc /path` | Open Claude Code in directory |
| `↑` `↓` | Navigate results |
| `Enter` | Confirm / launch / copy |
| `Escape` | Dismiss |

---

## Architecture

```
main.c        - GtkApplication daemon, D-Bus activation
window.c      - UI, input handling, mode dispatch
matcher.c     - Fuzzy app matching with nickname resolution and usage scoring
app_index.c   - Desktop application indexing via GIO
launcher.c    - Application launching
config.c      - Config parsing and usage history
file_nav.c    - Directory listing, file search, xdg-open integration
win_nav.c     - Window listing and focusing via niri IPC
calc.c        - Recursive descent math expression evaluator
animation.c   - Show/hide slide animation
```

---

## License

See LICENSE file for details.
