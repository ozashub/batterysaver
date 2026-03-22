# BatterySaver

Intelligent process power manager for Windows. Deprioritises and suspends background apps you're not using, snaps them back instantly when you switch to them. Foreground app is never touched.

## What it does

- Lowers priority of background processes (or suspends them after idle timeout)
- Resumes instantly on focus — no lag, no degraded experience
- Five modes: Off, Passive, Balanced, Aggressive, Custom
- Auto-switches mode on AC/battery and low battery
- Runs as a tray app or Windows Service
- Full settings GUI, no manual config editing

## Requirements

- Windows 10 (1903+) or Windows 11, x64
- Administrator privileges (UAC prompt on launch)
- Visual Studio 2022 with C++20 and vcpkg for building

## Building

1. Open `batterysaver.slnx` in Visual Studio 2022
2. Ensure vcpkg is integrated (`vcpkg integrate install`)
3. Build x64 Debug or Release

## Usage

```
BatterySaver.exe              # tray app (default)
BatterySaver.exe --console    # tray + live console logging
BatterySaver.exe --service    # run as Windows Service
```

Config stored at `%AppData%\BatterySaver\settings.json` — auto-created on first run.

## Status

Under active development. Core foundation (modes, settings, logging, elevation) is in place. Process management engine coming next.
