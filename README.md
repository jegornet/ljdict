# LjDict

A lightweight dictionary app in C++20 with GTK 4 (gtkmm). It reads StarDict dictionaries (`.ifo` + `.idx` + `.dict`).

## Features

- Search across all enabled dictionaries as you type, ignoring case and diacritics
- Article pane with stress marks and clickable cross-references
- Search history of up to 500 words
- Global shortcut to show or hide the window, `⌘⇧D` by default on macOS and Alt+Shift+D on Linux
- Settings (`⌘,`): dictionary folder, turning dictionaries on and off, dragging them to set the article order, launch at login, start minimized, hiding to the menu bar

Dictionaries are loaded from `~/.dict` by default, including subfolders.

## Build

```sh
brew install gtkmm4 cmake ninja pkgconf
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_OBJCXX_COMPILER=/usr/bin/clang++
cmake --build build         # creates build/LjDict.app
ctest --test-dir build
./scripts/build-dmg.sh      # creates build/LjDict.dmg
```

`build/LjDict.app` is ad-hoc signed and links Homebrew's GTK, so it runs only on a Mac that has those libraries installed. `build/LjDict.dmg` carries a copy of the app with those libraries in `Contents/Frameworks` and runs without Homebrew, but only on the same architecture and macOS version as the build machine or later, because Homebrew builds its libraries for the current system. Move the app to `/Applications` before turning on launch at login, so the login item doesn't point at the build folder.

## Install

Open `LjDict.dmg` and drag LjDict to Applications. The app is ad-hoc signed and not notarized, so on a Mac other than the one that built it, macOS refuses to open a copy downloaded from the internet. Remove the quarantine flag once:

```sh
xattr -dr com.apple.quarantine /Applications/LjDict.app
```

Or right-click the app, choose Open, and confirm. On recent macOS that dialog may only offer to move the app to the Bin; then allow it in System Settings → Privacy & Security → Open Anyway, or use the command above.

## Linux packages

Packages for the latest Fedora release and the latest Ubuntu LTS are built in Docker, so they can be made on a Mac too. They land in `build/packages`, for the architecture of the machine that runs Docker.

```sh
./scripts/build-rpm.sh      # Fedora: ljdict-<version>-1.fc<N>.<arch>.rpm, plus source and debug packages
./scripts/build-deb.sh      # Ubuntu LTS: ljdict_<version>_<arch>.deb
```

Install with `sudo dnf install ./ljdict-*.rpm` or `sudo apt install ./ljdict_*.deb`. On a Linux machine you can also build natively: `rpmbuild` from `packaging/fedora/ljdict.spec`, or `dpkg-buildpackage -b` from the source tree, which has `debian/` at its root.

On Linux the global shortcut goes through the desktop's GlobalShortcuts portal (GNOME 48 and later, KDE Plasma): on first launch the desktop asks which keys to use, suggesting Alt+Shift+D, and clicking the shortcut in Settings opens that dialog again where the desktop supports it (GNOME changes the keys in its own Settings, on the page for LjDict). The menu bar icon is not available. Launch at login writes an XDG autostart entry to `~/.config/autostart` that runs the current binary. Closing the window or pressing Esc hides it and the app keeps running; launching LjDict again shows it.

## Settings

Settings and history are stored in `~/.config/ljdict/settings.ini` (or under `XDG_CONFIG_HOME`). On first launch they are copied from the AppKit version, if it has any.

## Limitations

- Compressed `.dict.dz` and `.idx.gz` files are not supported.
- Images in articles are not shown, and of inline CSS only text color and left margin are.
- On Linux the code builds with `platform_stub.cpp`, without the menu bar icon. The packages were checked only by installing them in containers and starting the app under Xvfb, The global shortcut and launch at login were also checked on GNOME 50 and KDE Plasma 6.8.
