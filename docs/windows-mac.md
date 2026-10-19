# SpaceMouse on Windows and macOS: the official 3Dconnexion driver

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver).

Research date: 2026-10-12. The official 3Dconnexion pages (3dconnexion.com/...) returned HTTP 403
when fetched; statements about them come from search-result summaries and forum posts, not from
full texts read first-hand. Labels:

- **[verified]** = backed by a linked source (as far as the source was reachable/summarized)
- **[forum]** = forum/community statement only, partly old (2007-2016); driver version may differ
- **[assumption]** = own conclusion, not verified
- **[unresolved]** = nothing reliable found

## Summary

1. The official driver (3DxWare / 3Dconnexion Software) has **application profiles**: per app,
   axes can be enabled/disabled, inverted, dominance chosen, and sensitivity and button
   assignment set. [verified/forum, sources 1, 2, 3]
2. For **normal apps without 3D support (browsers, PDF viewers) the driver does not scroll
   reliably**: on macOS a moderator confirmed that scrolling in Safari/Acrobat Reader is not
   supported with 3DxWare 10.8.4. [forum, source 5] On Windows there are reports that Word/Excel
   scroll or that cap rotation zooms, but no official promise of a generic scroll mode. [forum,
   source 6] A documented generic "scroll" or "pan/zoom" mode for arbitrary apps was **not found**
   [unresolved].
3. Profiles can be **exported/imported officially** (Windows and macOS) and can therefore be
   versioned in principle, but only make sense with the same driver version. [verified]
4. Anyone who needs exactly this repo's behavior (analog scroll speed, twist = zoom, horizontal
   scroll) would have to build their own tool. The 3Dconnexion SDK is proprietary and not
   compatible with GPL-3.0; the ports described in `port-windows.md` and `port-macos.md`
   therefore read the device directly (raw HID / IOHID) instead of using the SDK.

## Windows

### Per-application settings

- The Control Panel has an "App Configuration" section per application: enable/disable pan,
  zoom, tilt, spin, roll individually, invert all axes, choose the dominant axis.
  [forum/third-party docs, source 2 (Autodesk help for the control panel, 2015)]
- Overall and per-axis sensitivity and zoom direction per application. [forum, source 3]
- The SpaceMouse Pro manual lists "Advanced Settings" with a pan/zoom switch and motion
  behavior for many applications. [verified, source 1]
- Finer axis mapping (e.g. zoom direction without swapping rotations) goes through the per-app
  XML configuration file (entries such as `AXIS_MAP_HRZ_USER`/`AXIS_MAP_VRT_USER`); one forum post
  says this is not exposed in the GUI. [forum, sources 3, 4]
- Button assignment per application: SpaceMouse (Pro/Compact) buttons can be assigned in the
  driver; the exact UI of the current "3Dconnexion Software" was not verified. [unresolved]
- Deadzone: no separate slider **found**. [unresolved] Sensitivity behaves similarly in practice
  but is not the same. [assumption]

### Scrolling in normal programs

- Office: forum report that scrolling in Office 2013 apps only worked after "Restore Defaults";
  cap rotation zooms in Excel/Word. [forum, source 6]
- Acrobat on Windows: pan/zoom worked after adjusting security settings; tip to delete
  `AdobeReader.xml` in the 3DxWare folder. [forum, source 6 (Adobe community)]
- Browsers (Chrome/Firefox): feature request in the forum, no evidence of native support.
  [forum, source 6]
- Conclusion: behavior depends on app-specific profiles/mappings in the driver. Whether the driver
  falls back to scroll events for unknown apps is **not verified**.

### Configuration files, export/import

- User-specific files: `%APPDATA%\3Dconnexion\3DxWare\Cfg` (XML). [verified, source 7]
- Older drivers: profiles as `.scg`; app-to-profile mapping additionally in the registry
  (`HKCU\Software\3Dconnexion\3DxWare\Applications`); merely copying files was not enough, one user
  reports copied profiles were ignored. [forum, source 8]
- Official: driver panel, "More" menu, "Export Settings" / "Import Settings"; only use with the
  same driver version. The storage location can be changed via the "3Dconnexion Administrators
  Guide" (section "Configuration hierarchy and sharing"). [verified, source 7]
- Versioning: the export file or the `Cfg` XMLs could go into a Git repo; restoring is most
  reliable via import. [assumption]

## macOS

### Settings

- Configuration is done in the 3Dconnexion section of System Settings (formerly a preference
  pane); button/axis options are analogous to Windows, details of the current macOS version not
  verified. [unresolved]
- Scrolling in Safari and Acrobat Reader is, per a moderator, not supported with 3DxWare 10.8.4;
  without the driver installed the mouse behaves as a plain 2D device, with the driver it no
  longer does. [forum, source 5] The example is outdated (version, date unclear); check current
  release notes. [unresolved]

### Configuration files, export/import

- User settings: `~/Library/Preferences/com.3dconnexion.mappings.plist`; a moderator says editing
  is "not recommended and not supported". [forum, source 9]
- Further plists (`com.3dconnexion.3DMouseHome.plist`, `VirtualNumPad.plist`) are named in a McNeel
  thread, role unclear. [forum, source 10]
- Settings are stored per user and survive uninstalling the driver. [forum, source 9]
- Official: pull-down menu in the driver pane, "Export Configuration...". [forum, source 9]
- The path `~/Library/Application Support/3Dconnexion` was not confirmed. [unresolved]

## If the driver does not scroll: alternatives

| Option | Description | Effort/benefit (estimate = assumption) |
|---|---|---|
| Own tool, direct device access | Reads SpaceMouse motion via raw HID (Windows) / IOHID (macOS) and generates scroll/zoom events (`SendInput` / CGEvent); see `port-windows.md`, `port-macos.md` | Medium to high; functionally equals this repo. Benefit: exactly the desired behavior. |
| Create driver profiles per app | Mappings/button assignment (e.g. button = page up/down) | Low; no analog scrolling, but no code. |
| Third-party tools | Specific tools not researched | [unresolved] |

### SDK access and license

- Developer registration with 3Dconnexion is required (developer program, per the ParaView docs
  `3dconnexion.com/us/software-developer-program/`); one comment says no formal contract is
  needed, others report registration problems. [forum/community, sources 11, 12]
- License text (2007/2013/2020 versions in third-party products): revocable, royalty-free,
  non-transferable, no patent rights; applications must name 3Dconnexion in the about/help text.
  Check the current wording in the SDK download before use. [verified, source 13]
- Older C/C++ SDK versions are in an archive area per the forum; a Linux SDK was reportedly
  never planned. [forum, source 12]
- Because of these terms this project does not link the SDK (see `port-architecture.md`).

## Recommendation

1. Install the official driver and try it: test browser, Office, PDF viewer directly. Set axes,
   inversion, sensitivity and buttons per application in the driver panel. Compare the result with
   the Linux behavior (deflection = speed).
2. If cap rotation zooms and scrolling suffices: save settings via "Export Settings" and keep the
   export file (not the raw `Cfg` directory) together with the driver version.
3. If apps do not scroll (mainly macOS browsers per the forum): check whether current release
   notes fix it; otherwise use this project's direct-HID port.
4. Close the gaps before deciding: deadzone, generic scroll mode, current menus of the
   "3Dconnexion Software" - best checked on the device itself.

## Sources

1. SpaceMouse Pro Manual: https://3dconnexion.com/manuals/spacemouse-pro/en/Manual_3Dconnexion-SpaceMouse-Pro_EN.pdf
2. Autodesk Moldflow on the 3Dconnexion Control Panel: https://help.autodesk.com/cloudhelp/2015/ENU/MoldflowComm/files/GUID-68EED47E-2541-47A2-835F-F08CCDF7BE5D.htm
3. Forum, zoom direction/axes XML: https://forum.3dconnexion.com/viewtopic.php?p=7144
4. Forum, zoom direction without rotation: https://forum.3dconnexion.com/viewtopic.php?p=124907
5. Forum, Safari/Adobe Reader (macOS): https://forum.3dconnexion.com/viewtopic.php?p=122247
6. Forum, Axes Configuration; Adobe community: https://forum.3dconnexion.com/viewtopic.php?p=120675 , https://community.adobe.com/t5/acrobat-discussions/3d-navigation/m-p/11505360/highlight/true
7. 3Dconnexion FAQ config files and storage location: https://3dconnexion.com/uk/?p=5394 , https://3dconnexion.com/us/?p=56012 (403 when fetched, search summary only)
8. Forum, backup/restore profiles: https://forum.3dconnexion.com/viewtopic.php?p=7227
9. Forum, macOS config location and backup: https://forum.3dconnexion.com/viewtopic.php?p=46892 , https://forum.3dconnexion.com/viewtopic.php?p=78976
10. McNeel Discourse: https://discourse.mcneel.com/t/3dconnexion-rhino-issue/57143
11. ParaView SpaceMouseInteractor: https://gitlab.kitware.com/paraview/paraview/-/tree/master/Plugins/SpaceMouseInteractor
12. Forum "Where's the SDK?"; PrusaSlicer issue: https://forum.3dconnexion.com/viewtopic.php?t=984 , https://github.com/prusa3d/PrusaSlicer/issues/3447
13. SDK license text in third-party docs: https://learn.foundry.com/modo/16.0/content/help/pages/appendices/appendix_c_external_software/third_party_libraries.html
