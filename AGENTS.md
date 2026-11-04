# LjDict GTK: notes for agents

C++20 and gtkmm 4 rewrite of the AppKit app in `../ljdict`. See `README.md` for features and limitations. This file records why things are the way they are.

## Commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_OBJCXX_COMPILER=/usr/bin/clang++
./scripts/build-dmg.sh      # build/LjDict.dmg, with Homebrew's libraries copied into the bundle
./scripts/build-rpm.sh      # Fedora packages in build/packages, built in a fedora:latest container
./scripts/build-deb.sh      # Ubuntu package in build/packages, built in an ubuntu:latest (latest LTS) container
cmake --build build && ctest --test-dir build
```

- Pass `/usr/bin/clang++` as the compiler, as `build-dmg.sh` does. Plain `clang` on the author's machine resolves to a swiftly toolchain.
- The CMake target is the bundle itself, `build/LjDict.app`, ad-hoc signed after every link. There is no bare binary, because outside a bundle macOS shows a generic "exec" icon in the Dock and setting the icon from code does not change what the system reports.
- `build-dmg.sh` copies every non-system library into `Contents/Frameworks` of a staged copy and rewrites references to `@rpath/<linked name>`; it fails if any Homebrew path is left. GTK's runtime data (icon themes, translations) are not copied: the UI is English, the icons it uses are built into GTK, and `Gtk::FileDialog` is the native panel, so no GSettings schemas are needed.
- The version lives in `project(... VERSION ...)` in `CMakeLists.txt`; `Info.plist.in` and the About dialog take it from there. `packaging/fedora/ljdict.spec` (`Version`, `%changelog`), `debian/changelog` and the metainfo `<release>` must be bumped by hand; `build-rpm.sh` and `build-deb.sh` refuse to run if the first two disagree with CMake.
- Linux files are in `resources/linux`. The desktop file, metainfo and installed icon are named by the application id `net.jegor.ljdict.gtk`, because Wayland matches windows to desktop files by it. The PNG icons were extracted from `AppIcon.icns` with `iconutil`.
- The icon is a generated binary, `resources/AppIcon.icns`, made once by the old project's `make-icon.swift`.

## Layout

- `src/core`: no UI, depends only on GLib, covered by `tests/core_tests.cpp`. Keep testable logic here.
  - `text_folding`: search keys and row identity.
  - `stardict_book`: one dictionary, its index and record parsing.
  - `dictionary_library`: folder scan, search, article lookup, custom ordering.
  - `article_renderer`: record to HTML, link URLs.
  - `rich_text`: HTML to styled spans.
- `src/app`: GTK. `application` (lifecycle, menu, windows), `app_model` (library and settings), `main_window` (three panes, search flow, history timing), `word_list`, `article_view`, `settings_window`, `settings`, `search_history`.
- `src/platform`: everything GTK does not offer. `platform_mac.mm` is the only Objective-C++ file.

## Architecture decisions

**Article view.** WebKitGTK has no macOS bottle, so articles are a `GtkTextView`. `ArticleRenderer` still produces the same HTML as the AppKit version, and `layoutHTML` turns it into spans. Raw `h` records go through the same path. Real `h` data uses only `span`, `i`, `b`, `a`, `blockquote`, `div`, `sub`, `sup`, and inline styles only for `color:green` and `margin-left:1em` (both in `Sr_Ru`); those two properties are honored and the rest of CSS is dropped.

**Security.** Dictionary content is untrusted. There is no HTML engine: only text, a fixed set of styles and link targets leave `layoutHTML`, so scripts and remote loads cannot happen. Only `http(s)` links leave the app. Internal links use `ljdict://lookup?w=...`; `bword://` is accepted too.

**Index.** As in the AppKit version: each book keeps its own index sorted by folded key, with `.idx` and `.dict` memory-mapped. Search is a binary search per book, then a merge, so turning a dictionary on or off or reordering rebuilds nothing. There is no on-disk cache.

**Two keys, on purpose.**
- `folding::key` (lowercase, no diacritics) decides what matches.
- `folding::rowKey` (lowercase, diacritics kept) decides what is one row. `House` and `house` merge; `cim` and `čim` stay separate.

Letters with no Unicode decomposition are listed in `overrideFor`. `й` folds to `и` as a side effect of decomposition; this is accepted.

**XDXF.** A tolerant tag scanner, not an XML parser: records are rootless fragments and not always well-formed. Unknown tags are dropped and their text kept.

**History.** Typing previews articles without recording them. A word is recorded after an explicit action (Enter, clicking a row or link) or after 2 s on screen.

**Focus.** Word lists are not focusable, so the search field keeps focus and arrow keys in it drive the list. `ListItem` must be non-focusable too. Rows handle clicks themselves, because the row's own gesture claims the press and hides the release.

**Esc.** Hides the app and keeps the query. In the search field it arrives through `stop-search`, so it can still cancel input method composition; a capture-phase controller on the window catches it everywhere else.

**Search focus.** The query is selected on `focusSearch`, on window activation, and on a click that moves focus to the field. That last case needs `EventControllerLegacy`, because the text widget claims the gesture.

**Window.** Closing only hides the window and `hold()` keeps the app running, so the global shortcut stays alive. GTK does not restore window size or panes, so `saveGeometry` does.

**Linux.** There is no status item, so a hidden window also comes back through a second launch: the app is unique there (D-Bus), and the second instance activates the first. On macOS it stays `NON_UNIQUE` and uses the reopen handler. The stub's `hideApp` hides every toplevel, so Esc works. Launch at login is an XDG autostart file named by the application id; its `Exec` is the running binary from `/proc/self/exe`, so a build outside the PATH starts too. Turning it off deletes the file.

**Linux shortcut.** The GlobalShortcuts portal: the app asks for one shortcut with a preferred trigger, and the desktop picks the keys, so `systemOwnsHotKey` replaces recording in Settings with the portal's dialog (`ConfigureShortcuts`, portal version 2, or `BindShortcuts` again while no keys are set). The portal gets a private D-Bus connection, because a host app must call `Registry.Register` with its id before any other portal call on a connection and GTK has already used the shared one. The `activation_token` of `Activated` goes to the window as its startup id. GNOME 50 shows its own dialog on the first bind, reports the keys as "Press <Control><Alt>d" (so `readableTrigger` parses the accelerator out), and does not implement `ConfigureShortcuts`, so Settings says where to change them.

**Hidden windows.** gtkmm removes a window from its application when it is hidden, and the app's accelerators stop working in it. `Application::present` adds it back; `Gtk::Window::set_remove_on_hide(false)` needs gtkmm 4.22.

**Dark mode.** GTK does not follow the macOS appearance, so `followSystemAppearance` copies it on window activation. A change made while the app is active shows up at the next activation. Set the color scheme through the typed `property_gtk_interface_color_scheme()`: `set_property` with the C enum logs a GObject critical.

**Menu shortcuts.** GTK's `<Primary>` is Control on macOS too, so accelerators use `COMMAND_KEY`, which is `<Meta>` there.

**Hot key.** Carbon `RegisterEventHotKey`, because it needs no Accessibility permission, and key codes are physical. Recording uses an `NSEvent` local monitor rather than GDK, because Carbon needs macOS key codes and Command combinations would otherwise reach the menu first. The hot key is unregistered while recording.

**Menu bar.** `updateDockIcon` keeps the status item while the setting is on and switches to `.accessory` only while no window is on screen. A minimized window counts as on screen.

## Persistence

`Glib::KeyFile` at `<user config dir>/ljdict/settings.ini`, one group `LjDict`. Dictionary ids are `.ifo` paths relative to the dictionaries folder. `dictionaryOrder` keeps ids of absent books on purpose. When the file is missing, `readLegacySettings` reads the AppKit app's `net.jegor.ljdict` defaults once.

The bundle identifier is `net.jegor.ljdict.gtk`, so this app and the AppKit one can be installed together. Both default to the same global shortcut, and whichever starts second reports it as taken.

## Verifying changes

The core was checked against the Swift original by dumping every record of `~/.dict` from both: words, keys, row keys, HTML and order, plus search results. Repeat that if you change `text_folding` or `article_renderer`.

Tests cover only the core, so run the app after UI changes. Synthetic input is blocked in this environment. What worked: copy the project to a scratch directory, make the members public there, add a timed script that calls the controllers directly, and render windows to PNG with `GtkWidgetPaintable` and `gsk_renderer_render_texture`. Set `XDG_CONFIG_HOME` to a scratch folder. Do not put driver code in this repository.

The Linux packages were checked by installing them in clean `ubuntu:26.04` and `fedora:latest` containers and starting the app twice under `xvfb-run dbus-run-session`: the first instance runs and the second exits, leaving one process. A minimal Fedora container lacks `libglvnd-gles`, which a desktop install has. Nothing on Linux was checked visually.

The Linux shortcut was checked on Fedora 44 with GNOME 50 on Wayland: the bind dialog, the binding kept across restarts, showing and hiding with another window focused, the label and notice in Settings, and launch at login. Input and screenshots went through Mutter's `RemoteDesktop` and `ScreenCast` D-Bus APIs from a script outside the repository. KDE was not tried.

Not verified end to end so far: a physical press of the global shortcut, real mouse clicks on rows and links, drag reordering, the status item, launch at login, shortcut recording, and the native menu bar.

## Conventions

- Keep the existing comment density: a short comment for a non-obvious reason, none for the obvious.
- Dependencies: gtkmm and GLib only.
