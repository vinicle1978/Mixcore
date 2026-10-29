# Installing MixCore on a Mac and testing it in Logic Pro

Logic Pro only loads **Audio Unit (AU)** plugins. The macOS installer contains
the AU (for Logic and GarageBand), the VST3 (for other DAWs) and the standalone
app. It is a universal build: Apple Silicon (M-series) and Intel.

## 1. Get the installer

1. Push the repository to GitHub.
2. Open the repo → **Actions** → **Build installers** → **Run workflow**.
   (Or push a tag such as `v0.1.0`; the installers are then also attached to a
   GitHub Release.)
3. Wait for the run to go green (about 10–15 minutes the first time).
4. Open the run, scroll to **Artifacts**, download **MixCore-macOS-installer**
   and unzip it. You get `MixCore-0.1.0-macOS.pkg`.

The workflow already installed this exact `.pkg` on a Mac runner and passed
Apple's `auval` validation, which is the same check Logic performs.

## 2. Install

1. Double-click `MixCore-0.1.0-macOS.pkg`.
2. macOS will say it cannot verify the developer (the installer is not yet
   signed with an Apple Developer ID). Click **Done**, then open
   **System Settings → Privacy & Security**, scroll down and click
   **Open Anyway** next to the MixCore message. Confirm with your password.
3. Follow the installer. **Customize** lets you skip the VST3 or standalone app.

Installed to:

| Item | Location |
|---|---|
| AU | `/Library/Audio/Plug-Ins/Components/MixCore.component` |
| VST3 | `/Library/Audio/Plug-Ins/VST3/MixCore.vst3` |
| Standalone | `/Applications/MixCore.app` |

## 3. Load it in Logic Pro

1. Open Logic. It scans new plugins on launch; MixCore should pass.
   If it does not appear: **Logic Pro → Settings → Plug-in Manager**, find
   **MixCore Audio → MixCore**, select it and click **Reset & Rescan Selection**.
2. On an audio track with a vocal: click an empty **Audio FX** slot →
   **Audio Units → MixCore Audio → MixCore**. Mono and stereo tracks both work.
3. Put it **first** in the chain, before EQ and compression.

## 4. Test checklist

* **Meters move** with playback; IN shows your raw level, OUT the processed level.
* **Calibrate:** press Calibrate during a sung passage, wait for the result,
  press **Apply suggestion**. The Static gain knob should move, and
  **Cmd-Z** in Logic should undo it.
* **Rider:** the amber line in the graph moves slowly up on quiet phrases and
  down on loud ones, and stays flat in pauses (activity band not green).
* **Automation:** in Logic, show automation for the track and pick
  MixCore → Static Gain / Target RMS. Write a move and play it back.
* **Save/restore:** change settings, save the project, quit, reopen. Settings
  must come back.
* **Bypass:** Logic's plugin power button bypasses MixCore without a click.
* **Peak guard:** enable it; Logic compensates the added 2 ms automatically
  (no audible timing shift against other tracks).
* **Sample rates:** try the project at 44.1, 48 and 96 kHz.

Then work through the listening tests in
[`docs/testing/listening-tests.md`](../testing/listening-tests.md) and note
anything that sounds wrong, with timestamps.

## If something goes wrong

* **MixCore missing in Logic:** Plug-in Manager → Reset & Rescan. If it shows
  "failed validation", run in Terminal and send the output:
  `auval -strict -v aufx Mxcr Mxca`
* **"MixCore is damaged and can't be opened"** (only if you copied bundles by
  hand from a zip instead of using the .pkg):
  `sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/MixCore.component`
* **Uninstall:** delete the three items in the table above.

## Signing for other people

For your own testing the unsigned installer is fine. Before giving it to
other people, sign and notarise it with an Apple Developer ID (Apple Developer
Program membership). The packaging script already accepts
`MIXCORE_APP_SIGN_ID` and `MIXCORE_PKG_SIGN_ID`; notarisation still needs to be
added to the workflow.
