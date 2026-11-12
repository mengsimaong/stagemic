# Installing StageMic (Windows, 64-bit)

You need **Windows 10 or 11 (64-bit)** and **OBS Studio 31 or newer** (Help → About shows your version).

## 1. Install

1. Download **StageMic-Setup.exe**:
   https://github.com/mengsimaong/stagemic/releases/latest/download/StageMic-Setup.exe
2. **Close OBS Studio** completely (also from the system tray near the clock).
3. Double-click **StageMic-Setup.exe**.
   * If Windows says **"Windows protected your PC"**, click **More info → Run anyway**.
     This appears because StageMic is new and not code-signed yet.
   * Click **Yes** when Windows asks to allow changes (the installer writes into OBS's plugin folder).
4. Follow the steps. The installer asks one question:
   * **"I installed OBS Studio normally"**: choose this if you got OBS from obsproject.com, Steam or the
     Microsoft Store. Most people.
   * **"I use a zip (portable) copy"**: choose this if you unzipped OBS into a folder yourself, then
     pick that folder (the one containing `bin` and `data`).

## 2. Add StageMic to your microphone

1. Start OBS.
2. In the **Audio Mixer**, click the **three dots** (or gear) next to your microphone → **Filters**.
3. Click **+** at the bottom left → **StageMic** → **OK**.
4. Click **Open StageMic** to open its window.

**Tip:** if your room is noisy, add OBS's own **Noise Suppression** filter *above* StageMic in the
same Filters list.

**Tip for singing:** to hear yourself while singing, use your audio interface's or headset's
**direct monitoring** if it has one. OBS's own "Monitor" setting adds a noticeable delay, which
makes singing in time hard. That delay comes from OBS, not StageMic (StageMic adds zero delay).

## 3. Update or uninstall

* **Update:** download and run the newest StageMic-Setup.exe. It installs over the old version.
  Your presets are kept.
* **Uninstall:** Windows **Settings → Apps → Installed apps → StageMic for OBS Studio → Uninstall**.
  Your own presets in `Documents\StageMic\Presets` are kept unless you delete them.

## If something goes wrong

* **StageMic isn't in the + list:** run the installer again and check you picked the right type of
  OBS. Then, in OBS, open **Help → Log Files → View Current Log** and search for `stagemic`.
* **Anything else:** **Help → Log Files → Upload Current Log File** in OBS, and send the link.
* **OBS crashed:** restart OBS (it offers Safe Mode, which disables plugins), and send the crash
  report from **Help → Crash Reports**.

## Manual install (advanced)

Each release also has a `…-manual.zip` with the plugin files.

* **Installed OBS:** copy the `stagemic` folder from `obs-plugins` into
  `C:\ProgramData\obs-studio\plugins\`, so that
  `C:\ProgramData\obs-studio\plugins\stagemic\bin\64bit\stagemic.dll` exists.
* **Zip/portable OBS:** copy `stagemic.dll` into `<OBS folder>\obs-plugins\64bit\` (create it if
  missing), and the contents of `data` into `<OBS folder>\data\obs-plugins\stagemic\`.
  This location works for OBS 31-33 in both portable and normal mode.
* Never put StageMic inside OBS's `core` folder; that's for OBS's own parts.

## Optional: the VST3 version

The installer's "full" option also installs StageMic as a VST3 into
`C:\Program Files\Common Files\VST3\`, for music apps such as Reaper. Streamers don't need it.
