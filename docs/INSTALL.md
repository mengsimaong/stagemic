# Installing Paopoa StageMic (Windows, 64-bit)

You need **Windows 10 or 11 (64-bit)** and **OBS Studio 31 or newer** (in OBS: **Help → About** shows your version).

## 1. Install

1. Download the installer (**StageMic-Setup-(version).exe**) from the Paopoa StageMic download page.
2. **Close OBS Studio completely**, also from the system tray (the small icons near the clock: right-click the OBS icon → **Exit**).
3. Double-click the installer.
   * If Windows says **"Windows protected your PC"**, click **More info → Run anyway**.
     This appears because Paopoa StageMic is new and not code-signed yet.
   * Click **Yes** when Windows asks to allow changes (the installer writes into OBS's plugin folder).
4. The installer asks one question:
   * **"I installed OBS Studio normally"**: you got OBS from obsproject.com, Steam or the Microsoft Store. Most people.
   * **"I use a zip (portable) copy"**: you unzipped OBS into a folder yourself. Pick that folder (the one containing `bin` and `data`).

## 2. Add it to your microphone

1. Start OBS.
2. In the **Audio Mixer**, click the **three dots** (or the gear) next to your microphone → **Filters**.
3. Under **Audio Filters**, click **+** → **Paopoa StageMic** → **OK**.
4. Click **Open Paopoa StageMic**.
5. In the window, click **Set up my voice** once (3 seconds quiet, 8 seconds singing), then pick a preset.

**Simple / Advanced:** the switch at the top. Simple shows the essentials; Advanced shows everything.
Switching never changes the sound.

**Hearing yourself:** use your headset's or audio interface's **direct monitoring**. With Tune on,
direct monitoring plays your voice without the tuning; to hear the effect, set the mic's
**Audio Monitoring** to **Monitor and Output** in **Edit → Advanced Audio Properties** (adds a small delay).

## 3. Update or uninstall

* **Update:** run the newest installer. It installs over the old version; your filter, settings and presets stay.
* **Uninstall:** Windows **Settings → Apps → Installed apps → Paopoa StageMic for OBS Studio → Uninstall**.
  Your own presets in `Documents\StageMic\Presets` are kept unless you delete them.

## OBS doesn't show "Paopoa StageMic"

Go through these in order. Most people are fixed by step 1 or 2.

1. **Restart OBS completely.** If OBS was open while installing, it can't see the new filter.
   Exit it from the tray icon too (or restart the PC), then start OBS again.
2. **Look in the right list.** Paopoa StageMic is an **audio** filter. Open Filters on your
   **microphone** (Audio Mixer → three dots → Filters) and click **+** under **Audio Filters**.
   It isn't in the list of a camera, image or scene (those only show video "Effect Filters").
3. **Check your OBS version:** **Help → About** must say **31** or newer. Update OBS from obsproject.com if it's older.
4. **Pick the right kind of OBS in the installer.** Run the installer again:
   * Steam, obsproject.com installer or Microsoft Store → **"I installed OBS Studio normally"**.
   * You unzipped OBS yourself, or there's a file called `portable_mode.txt` next to OBS's `bin` folder →
     **"I use a zip (portable) copy"**, and choose that OBS folder.
   * Two OBS copies on one PC (e.g. Steam and a zip)? Install for the one you actually start.
5. **Antivirus:** some antivirus programs quarantine new, unsigned plugins. Look in your antivirus history for
   `stagemic.dll`, restore it / allow it, and run the installer again.
6. **Read what OBS says:** **Help → Log Files → View Current Log**, then search (Ctrl+F) for `stagemic`:
   * `[StageMic] Loaded` → it loaded fine: go back to step 2.
   * `Failed to load module` or `could not load` → install the **Microsoft Visual C++ Redistributable (x64)**
     from Microsoft's website, restart the PC, start OBS again.
   * No line at all → OBS didn't find it: step 4 (wrong OBS chosen), or use the manual install below.
7. **Still stuck:** **Help → Log Files → Upload Current Log File**, and send the link it copies.

Not supported: Windows on ARM, 32-bit Windows, macOS and Linux (for now).

## If OBS crashes

Restart OBS. It offers **Safe Mode**, which turns off plugins so you can stream normally. Then send the
crash report from **Help → Crash Reports** and the log link from **Help → Log Files → Upload Current Log File**.

## Manual install (advanced)

Each release also has a `…-manual.zip` with the plugin files.

* **Installed OBS:** copy the `stagemic` folder from `obs-plugins` into
  `C:\ProgramData\obs-studio\plugins\`, so that
  `C:\ProgramData\obs-studio\plugins\stagemic\bin\64bit\stagemic.dll` exists.
* **Zip/portable OBS:** copy `stagemic.dll` into `<OBS folder>\obs-plugins\64bit\` (create it if
  missing), and the contents of `data` into `<OBS folder>\data\obs-plugins\stagemic\`.
  This location works for OBS 31-33 in both portable and normal mode.
* Never put it inside OBS's `core` folder; that's for OBS's own parts.

## Optional: the VST3 version

The installer's "full" option also installs a VST3 into `C:\Program Files\Common Files\VST3\`,
for music apps such as Reaper. Streamers don't need it.
