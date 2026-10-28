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

3. That is the whole first start: opening dofscroll starts the daemon by itself and shows a short setup
   guide. You do not run a script or a command. The guide
   - checks live that dofscroll is running and that the SpaceMouse is found,
   - has "Start at login" (a check box, ticked by default; a per-user LaunchAgent, no administrator rights; it is
     switched on only when you press Finish, nothing changes before that or when you skip the guide),
   - has one row for each permission (System Settings > Privacy & Security), with a button that opens the
     matching pane and a note while macOS does not accept the events:
       Input Monitoring: needed to read the SpaceMouse.
       Accessibility: needed to post scroll, zoom and key events.
     Switch on dofscroll in both lists. If it is not listed, add the daemon with the "+" button
     (press Command-Shift-G to type the path):
       /Applications/dofscroll.app/Contents/Resources/daemon/dofscroll
     No restart is needed: dofscroll looks at the permissions every few seconds.
   You can skip the guide and open it again later from the Overview ("Show setup guide").

4. Alternative by hand (not needed after step 3): start the daemon at login with the script
       sh /Applications/dofscroll.app/Contents/Resources/launchagent/install-launchagent.sh \
          --daemon /Applications/dofscroll.app/Contents/Resources/daemon/dofscroll
   Remove it again:
       sh /Applications/dofscroll.app/Contents/Resources/launchagent/uninstall-launchagent.sh

5. Uninstall
   Run the uninstall script above (if you switched on "Start at login" in the app, switch it off first, or
   delete ~/Library/LaunchAgents/io.github.dofscroll.ui.plist), then drag dofscroll.app to the Trash. Your configuration in
   ~/Library/Application Support/dofscroll stays until you delete it.

6. Does it run next to the official 3Dconnexion driver? (a 5 minute test, please run it)
   You can keep the 3Dconnexion driver installed and running: dofscroll is designed to work next to it.
   Whether both can read the SpaceMouse at the same time on a real Mac has not been verified yet.
   Run in a terminal (it changes nothing and writes dofscroll-driver-test-report.txt):
       sh /Applications/dofscroll.app/Contents/Resources/diagnostics/test-driver-coexistence.sh
   Then attach the file to issue #37 of the project. The diagnostic tool it uses can also be run by hand:
       /Applications/dofscroll.app/Contents/Resources/daemon/dofscroll-hidprobe --list
   (Terminal needs the Input Monitoring permission for it, as for the daemon.)

The program has not been tested with a real SpaceMouse on macOS yet.
Licenses: LICENSE and THIRD-PARTY-NOTICES.md next to this file.
Project: https://github.com/HoZe-Lukas/dofScroll
