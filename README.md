# Mintglide

Native C++17 multi-touch gesture daemon and GTKmm settings application for
Apple trackpads on Linux Mint and other Linux distributions with an X11 desktop
session. The daemon reads the Apple trackpad's evdev stream and synthesizes
X11 shortcuts/buttons using XTest.

## Dependencies

Install the required development packages on Debian/Ubuntu/Linux Mint:

```sh
sudo apt install build-essential cmake libevdev-dev libudev-dev libx11-dev \
  libxtst-dev libgtkmm-3.0-dev nlohmann-json3-dev pkg-config dpkg-dev
```

## Build a Debian package

```sh
cmake -S . -B build-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-package --parallel
cpack --config build-package/CPackConfig.cmake -G DEB
sudo apt install ./build-package/mintglide_*.deb
```

`dpkg-dev` supplies `dpkg-shlibdeps`, which CPack uses to add the binary
runtime-library dependencies to the package metadata. The package also depends
on `udev`. The package installs the daemon, GUI, Mintglide menu entry and icon,
a desktop-session autostart entry for the daemon, and a narrowly scoped udev
rule for Apple touchpads. The autostart entry is not restricted to Cinnamon,
so it also works in other XDG-autostart desktop sessions such as XFCE.

Log out and back in after installing so your desktop session starts the daemon
and logind grants the active desktop user access to the trackpad. The udev rule
uses `TAG+="uaccess"` for Apple touchpads only; it does not require adding your
user to the broad `input` group. The daemon runs as your desktop user, not as
root, because it needs that session's X11 display and authorization. Launch
**Mintglide** from the applications menu to edit settings. To test before
logging out and back in, start the daemon from a terminal:

```sh
mintglide-daemon
```

The daemon stays in the foreground; press Ctrl+C to stop a manually started
instance. Settings are stored at `~/.config/mintglide/config.json`. Saving in
the GUI sends `SIGUSR1` to a running daemon so changes take effect immediately;
if it is not running, the settings are loaded at its next start. Disable
automatic startup from your desktop's **Startup Applications** settings if
desired.

Remove the package with `sudo apt remove mintglide`. This leaves your personal
configuration file in place.

## Manual development install

For development without building a Debian package, configure and install with
CMake using the same `/usr` prefix. Install the udev rule with the provided
helper script:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
sudo cmake --install build
sudo /usr/share/mintglide/setup_udev.sh
```

Run the daemon and GUI as your desktop user in an X11 session. The
development install also adds the desktop-session autostart entry.

## Gesture mappings and limits

The gesture page contains the enabled three-finger drag and two-finger
double-tap Smart Zoom options. Two-finger scrolling and optional Ctrl-modified
screen zoom use X11 wheel events. Tap-to-click, secondary click, natural
scrolling, and drag/scroll sensitivity remain configurable in the GUI.
While a three-finger drag holds the primary button for text selection, moving
only one finger vertically while the other two remain still sends traditional
scroll events without releasing the selection (finger up scrolls the page up).
This drag-scroll direction is independent of the Natural scrolling setting,
which continues to control other scrolling gestures. Once a drag starts, the
primary button stays held until all contacts are lifted, even if the contact
count temporarily drops below three.

Mission Control/App Exposé and workspace swipes, four-finger pinch actions,
and Look Up are not included.

The daemon observes (does not exclusively grab) the trackpad so normal pointer
motion remains managed by libinput. To avoid duplicate taps/scrolls, turn off
the corresponding native tap-to-click and two-finger-scroll options in Cinnamon
when enabling the daemon's synthetic equivalents.

Thresholds are normalized to the device's ABS_MT axis ranges, not screen
pixels. This is a hardware-specific implementation: verify behavior on the
target trackpad and adjust thresholds in the settings window. XTest action
delivery depends on an active X11 session; Wayland is not supported.

Existing settings in `~/.config/macgestures/config.json` are read as a
one-time compatibility fallback when the new Mintglide config file does not
exist. Saving from the GUI writes to `~/.config/mintglide/config.json`.
