dofscroll @VERSION@ for macOS (Apple silicon) - first run
=====================================================

This build is NOT signed with an Apple Developer ID and NOT notarized. It is signed ad hoc only
(that is what lets an arm64 binary run at all). macOS Gatekeeper will therefore warn on first use.

1. Install
   Drag dofscroll.app to Applications.

2. Open it the first time (Gatekeeper)
   - Right-click (or Control-click) dofscroll.app > Open > Open.
     On macOS 15 and later: open it once, then System Settings > Privacy & Security >
     "Open Anyway" next to the dofscroll message.
   - Or remove the quarantine flag in a terminal:
       xattr -dr com.apple.quarantine /Applications/dofscroll.app

3. Permissions (System Settings > Privacy & Security)
   - Input Monitoring: needed to read the SpaceMouse.
   - Accessibility: needed to post scroll, zoom and key events.
   Add and enable the daemon:
       /Applications/dofscroll.app/Contents/Resources/daemon/dofscroll
   (use the "+" button, press Command-Shift-G to type the path). Restart the daemon afterwards.

4. Start the daemon at login (per-user LaunchAgent, no administrator rights)
       sh /Applications/dofscroll.app/Contents/Resources/launchagent/install-launchagent.sh \
          --daemon /Applications/dofscroll.app/Contents/Resources/daemon/dofscroll
   Remove it again:
       sh /Applications/dofscroll.app/Contents/Resources/launchagent/uninstall-launchagent.sh

5. Uninstall
   Run the uninstall script above, then drag dofscroll.app to the Trash. Your configuration in
   ~/Library/Application Support/dofscroll stays until you delete it.

The program has not been tested with a real SpaceMouse on macOS yet.
Licenses: LICENSE and THIRD-PARTY-NOTICES.md next to this file.
Project: https://github.com/HoZe-Lukas/dofScroll
