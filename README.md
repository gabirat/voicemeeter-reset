# Voicemeeter Reset

Restarts Voicemeeter's audio engine by sending `Command.Restart` through the Voicemeeter Remote API.

Build with Visual Studio 2022 and CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

For Task Scheduler, run `build\Release\voicemeeter_reset.exe` at user logon and workstation unlock. Run it as your user with **Run only when user is logged on**, so it can connect to Voicemeeter in your session. Each run restarts the audio engine.