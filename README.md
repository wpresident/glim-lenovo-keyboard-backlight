<p align="center">
  <img src="src/icon_256.png" width="128" alt="Glim icon">
</p>

<h1 align="center">Glim</h1>
<p align="center"><b>Keyboard backlight keeper for Lenovo laptops</b></p>

---

On many Lenovo laptops the keyboard backlight doesn't stay the way you set it: Xiaoxin, IdeaPad and ThinkBook models turn it back on (often in **Auto** mode) after every cold boot or hibernation, and many ThinkPads switch it off after sleep or when the screen turns off. Lenovo's own apps can't keep your choice. **Glim** remembers the mode you want and quietly restores it.

## Features
- Backlight modes: **Off / Low / High / Auto** (Auto where the keyboard has it)
- Restores your mode after **sleep and hibernation**, and at sign-in with **Run at Windows startup**
- **Keeps a mode you set with Fn+Space** until restart (optional)
- Per-user autostart, no admin rights
- Tray icon that shows the current mode and switches it
- Follows Windows' **dark mode** and display scaling
- **Check for updates** on request (never on its own)
- Built-in **self-test** and log, and **Report this laptop**: a ready report for GitHub that helps add new models
- English, Русский, 中文
- One portable exe: no installer, no telemetry

## Requirements
- Windows 10 / 11 x64
- A Lenovo driver that is preinstalled on most models (otherwise download it for your model from Lenovo support):
  - Xiaoxin, IdeaPad, ThinkBook: **Lenovo Energy Management**
  - ThinkPad: **Lenovo Power Management** driver

Click **Self-test** to check your model.

### Tested laptops
| Laptop | Driver | Works |
|---|---|---|
| Lenovo XiaoXin Air 15 IWC11 (83UF), Windows 11 | Energy Management | yes |
| Lenovo ThinkBook 14 G7+ IAH (21TK), Windows 11 | Energy Management | yes |

ThinkPad support is new and not confirmed yet. Your laptop isn't listed, or Glim doesn't work on it? Please [report it](#report-your-laptop): that's how new models get added.

## Download
Get `Glim.exe` from the [latest release](../../releases/latest). The release notes list its SHA256.

`Glim.exe` is not code-signed, so when you first run it Windows SmartScreen may say *"Windows protected your PC"*: click **More info → Run anyway**. If Windows then asks for confirmation every time Glim starts at sign-in, right-click `Glim.exe` → **Properties** → tick **Unblock** → **OK**.

## Usage
1. Put `Glim.exe` in a permanent folder (for example `C:\Glim`, not Downloads) and run it.
2. Choose a mode, tick **Run at Windows startup** if you want it, and click **Apply**. Settings you changed are shown in **bold** until you apply them.
3. Click **Self-test** to make sure your laptop is supported.

Glim then lives in the tray. Closing the window hides it; use **Exit** in the tray menu to quit.

### Settings
| Option | Default | Description |
|---|---|---|
| Backlight mode | Off | Applied at sign-in and after wake |
| Run at Windows startup | off | Starts `Glim.exe` from its folder at sign-in (not available for a file in Downloads or a temporary folder). If you move `Glim.exe`, start it from the new place, tick the box and click **Apply** |
| Force re-apply after sleep/hibernation | off | Re-applies the mode after every wake. Enable only if the backlight still turns on by itself after waking |
| Keep manual changes (Fn+Space) until restart | on | A mode you set with Fn+Space stays until restart instead of being overwritten |
| Language | English | English / Русский / 中文 |

### Updating
Click **Check for updates** (in the window or the tray menu). If there is a newer release, Glim opens its page. Exit Glim from the tray menu, save the new `Glim.exe` over the old one and start it: settings and autostart are kept.

### Report your laptop
In Glim: tray menu → **Report this laptop…**. Glim runs the self-test (the keyboard light goes through its modes for a moment) and opens a [report form](../../issues/new/choose) on GitHub in its interface language, with the report filled in (if a field comes up empty, press Ctrl+V there: the report is copied too): the laptop model, Windows version, what the Lenovo driver answers, the self-test and the last lines of the log (your user name is removed). Nothing is published until you submit the form, and you can edit everything there; you also answer a few questions (for example, whether the backlight still changes after hibernation). No GitHub account? Choose **Copy the report** and post it anywhere. Glim also offers this after a self-test that fails while a Lenovo driver answers, or on a kind of laptop nobody has confirmed yet.

### Command line
```
Glim.exe /selftest [/quiet]    run the self-test (saved to selftest.txt; exit code 0 = OK, 1 = problems)
Glim.exe /autostart on|off     turn autostart on or off (exit code 1 = autostart wasn't turned on)
```
Glim is a windowed program, so to see the exit code in a command prompt start it with `start /wait Glim.exe /selftest /quiet`, then `echo %errorlevel%`.

### Files
- Settings: `HKCU\Software\Glim`
- Autostart: value `Glim` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
- Log and self-test report: `%LOCALAPPDATA%\Glim\`

### Uninstall
Untick **Run at Windows startup** → **Apply**, choose **Exit** in the tray menu, then delete `Glim.exe`, `%LOCALAPPDATA%\Glim` and `HKCU\Software\Glim`.

## How it works
Glim talks to the Lenovo Energy Management driver (`\\.\EnergyDrv`) or, on ThinkPads, the Lenovo Power Management driver (`\\.\IBMPmDrv`) via `DeviceIoControl`, the same interfaces Lenovo's own software uses. It sets your mode at sign-in, watches for the firmware resetting it after sleep or hibernation and restores it, and treats a change made with Fn+Space as your choice.

## Build
Requires MinGW-w64 (Linux or MSYS2):
```sh
cd src
sh build.sh
```
Every push is also built by [GitHub Actions](.github/workflows/build.yml).

## Notes
- For 90 seconds after sign-in and 15 seconds after waking up or the screen turning back on, Glim undoes any change of the backlight, including Fn+Space: that is how it catches the firmware's reset. Press Fn+Space again a bit later.
- An antivirus may flag a new, unsigned release until it builds up reputation. That is a false positive: you can check the file on [VirusTotal](https://www.virustotal.com) by its SHA256 and report it to your antivirus vendor.
- Based on research by [gonwan.com](https://www.gonwan.com/2025/04/11/keyboard-backlight-control-on-lenovo-ideapad-xiaoxin-models/).
- Not affiliated with Lenovo. Use at your own risk.

## Privacy
Glim sends nothing anywhere. It connects to the internet (GitHub) only when you click **Check for updates**. **Report this laptop** only opens a form in your browser (or copies the report); you decide whether to submit it.

## License
[MIT](LICENSE)
