# thundersearch -v0.1

A fast, keyboard-driven launcher for Wayland. Search applications, navigate files, and switch windows -- all from a single search bar.

Built with C, GTK4, and gtk4-layer-shell. Currently supports **niri** for window management.

inspired from OLauncher (Android Home Launcher)'s application launcher


**How fast is it?** <br>
Depends on how fast you can type. Literally.

**How does it work?** <br>
OLauncher launches application based on narrowing your app search launch with the characters you enter, and as soon as it hits on only one result, it immediately opens it.
That's what thundersearch is based on, but with more robust functionality, made in GTK4 and GTK4-layer-shell.

Below vidoes were recorded on an i3 4th Gen processor, MX500 ssd.


Opening Applications <br>
<video src="https://github.com/user-attachments/assets/7a598505-0814-4a9a-8dfd-fb542c00364a" height="352" width="720">


Switching Windows From Across Workspaces <br>
<video src="https://github.com/user-attachments/assets/a58cef7b-0d33-4578-8825-20e0040417f6" height="352" width="720">


Opening File Directories Directly In Your Default File Explorer <br>
<video src="https://github.com/user-attachments/assets/8d6df952-1117-40b0-81c9-e80a1c139a4d" height="352" width="720">


Opening Files From Thundesearch <br>
<video src="https://github.com/user-attachments/assets/a70cdf36-91d2-4afd-882b-cb7ef9add973" height="352" width="720">

<img width="758" height="576" alt="2026-03-30 15-27-28" src="https://github.com/user-attachments/assets/d77b5f0c-8a46-4540-8cc1-96ea8725e77b" />

<img width="758" height="576" alt="2026-03-30 15-28-13" src="https://github.com/user-attachments/assets/4581b0f1-7c1e-437b-afe2-6961cb17201c" />
<img width="758" height="576" alt="2026-03-30 15-27-56" src="https://github.com/user-attachments/assets/35b9df56-e915-4d64-9a2d-324416042b1d" />
<img width="758" height="576" alt="2026-03-30 15-27-48" src="https://github.com/user-attachments/assets/8c4bd2f0-4109-4f6d-890b-b666984026d7" />
<img width="758" height="576" alt="2026-03-30 15-27-40" src="https://github.com/user-attachments/assets/f4c0796a-1259-4f35-ba48-cf2a36d9d43a" />










## Building

### Dependencies

- GTK4
- gtk4-layer-shell
- GLib/GIO
- niri (for `/win` window switching)

### Compile and install

```
make
sudo make install
```

ThunderSearch runs as a single-instance daemon via GtkApplication. To apply a new build, kill the running instance first:

```
killall thundersearch && sudo make install
```

## Usage

Activate ThunderSearch with your compositor's keybinding (e.g., bind a key to launch `thundersearch`). A search bar appears as a Wayland overlay. Start typing to use any of the modes below. Press `Escape` to dismiss.

### Application launcher (default)

Just type an application name. Results narrow as you type. When only one match remains, it launches automatically.

```
firefox         -> launches Firefox
term            -> launches Terminal (if nicknamed)
```

Supports **nicknames** -- short aliases for applications (see Configuration).

### File navigation (`/f`)

Browse and open files/directories starting from your home directory.

| Command | Behavior |
|---------|----------|
| `/f ` | List home directory contents |
| `/f Documents` | Open Documents in file manager |
| `/f /Documents/` | Browse inside Documents (path mode) |
| `/f /Documents/project/` | Browse deeper -- keep typing to narrow |
| `/f/o /path/to/file.pdf` | Open file with configured app (or xdg-open) |
| `/fd ` | List contents of configured default directory |
| `/fd /subdir/` | Browse inside default directory |

**How path mode works:**

1. Start with `/f /` to enter path mode
2. Type to narrow results. Directories show as `(dirname)`, files as `filename.ext`
3. When narrowed to one directory, it auto-fills the name and shows its contents
4. When narrowed to one file, it opens the file (or its parent directory)
5. Backspace removes characters naturally -- the path in the search bar IS the state

**Simple mode** (`/f name` without leading `/`) opens the matched directory in your file manager or opens the file's parent directory.

### Window switcher (`/win`)

Search and focus any open window across all workspaces.

```
/win            -> list all open windows
/win fire       -> narrows to Firefox windows
/win code       -> narrows to VS Code
```

Results display as: `AppID - Window Title [workspace N]`

When narrowed to one result, ThunderSearch automatically switches to that workspace and focuses the window. You can also press `Enter` to focus the first result immediately.

Uses `niri msg` for window listing and focusing. Requires the **niri** compositor.

## Configuration

Config file location: `~/.config/thundersearch/config`

Created automatically on first run with example entries.

### Nicknames

Map short aliases to application names:

```
ff = Firefox
spot = Spotify
term = Alacritty
code = Visual Studio Code
```

Type `ff` and Firefox launches immediately.

### Default directory

Set the starting directory for the `/fd` command:

```
default_dir = ~/Projects
```

### File openers

Configure which application opens which file types when using `/f/o`. Group extensions on a single line:

```
open .pdf .epub = zathura
open .png .jpg .jpeg .gif .webp = imv
open .mp4 .mkv .avi .webm = mpv
open .txt .md .cfg .conf = gedit
```

If no match is found for an extension, falls back to `xdg-open`.

### Example config

```
# Nicknames
ff = Firefox
spot = Spotify
term = Alacritty

# Default directory for /fd
default_dir = ~/Projects

# File openers for /f/o
open .pdf .epub = zathura
open .png .jpg .jpeg .gif .webp = imv
open .mp4 .mkv .avi .webm = mpv
open .txt .md .cfg .conf = gedit
```

## Command reference

| Command | Description |
|---------|-------------|
| *(text)* | Search and launch applications |
| `/f ` | Browse home directory |
| `/f name` | Open directory/file by name |
| `/f /path/` | Navigate directories progressively |
| `/f/o /path/file` | Open file with configured or default app |
| `/fd ` | Browse configured default directory |
| `/win` | List all open windows |
| `/win query` | Search windows by title or app ID |
| `Escape` | Dismiss ThunderSearch |
| `Enter` | Confirm action on first result |

## Architecture

```
main.c        - GtkApplication setup, D-Bus activation, signal handling
window.c      - UI, input handling, mode dispatch
matcher.c     - Fuzzy app matching with nickname resolution and usage scoring
app_index.c   - Desktop application indexing via GIO
launcher.c    - Application launching
config.c      - Config parsing (nicknames, openers, default_dir) and usage history
file_nav.c    - Directory listing, file search, xdg-open integration
win_nav.c     - Window listing and focusing via niri IPC
```

## License

See LICENSE file for details.


