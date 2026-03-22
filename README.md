# BatterySaver

Process power manager for Windows that does one thing well: it takes whatever you are not actively looking at and stops it from wasting your battery. The moment you switch back to an app, it wakes up before the screen even finishes drawing. Your foreground app is never touched, period.

## How it works

BatterySaver watches which window has focus. When an app loses focus, its priority gets lowered. If it sits idle long enough (configurable), it gets fully suspended via `NtSuspendProcess`. When you click back into it, `NtResumeProcess` fires synchronously and the priority goes back to normal, all within a single frame budget.

There are five modes that control how hard it goes:

| Mode | What happens | When to use it |
|------|-------------|----------------|
| Off | Nothing | Plugged in, or you want it dormant |
| Passive | Lowers priority, never suspends | Light touch for when you want everything responsive |
| Balanced | Lowers priority, suspends after 45s | Daily driver on battery |
| Aggressive | Drops to idle priority, suspends after 15s | Low battery, long flights, extended sessions |
| Custom | You define everything | Whatever you need |

It auto-switches between modes when you plug in or unplug, and escalates to Aggressive when your battery drops below a threshold you set.

## Requirements

- Windows 10 (1903+) or Windows 11, x64 only
- Admin privileges (the manifest triggers UAC on launch, because without elevation it cannot touch other elevated processes and the whole thing falls apart)
- Visual Studio 2022 with v145 toolset and vcpkg for building from source

## Building

Open `batterysaver.slnx` in VS2022, make sure vcpkg is integrated (`vcpkg integrate install`), pick x64, and build. The only external dependency is `nlohmann-json` for config parsing, which vcpkg handles. Release builds with `/MT` so the output is a single self-contained exe with no runtime dependencies.

## Usage

```
BatterySaver.exe                    # tray app (default)
BatterySaver.exe --console          # same thing + live log output
BatterySaver.exe --install-service  # install as Windows Service
BatterySaver.exe --uninstall-service
```

Right-click the tray icon for mode switching and pause. Left-click opens the status window showing every tracked process, its state, and how long it has been there. Settings window covers everything from mode selection to low battery thresholds to the user whitelist.

Config lives at `%AppData%\BatterySaver\settings.json` and hot-reloads when you edit it. The settings GUI writes to the same file, so you can use either approach.

## Architecture

Single binary, three launch modes (tray app, console debug, service). The service runs as LocalSystem and spawns the tray helper into the active user session over a named pipe bridge. If the helper crashes, the service keeps managing processes and respawns the helper on the next session event.

System processes like `explorer.exe`, `dwm.exe`, `svchost.exe`, and about a dozen others are hardcoded as untouchable. User whitelist supports wildcards (`*launcher*.exe`). Processes without a visible window are ignored entirely.

Crash safety: an unhandled exception filter resumes every suspended process before the app goes down. Config writes are atomic (write to `.tmp`, rename over).
