# VanaSound

VanaSound adds independent **Sound Effects** and **Interface Sounds** volume controls to Final Fantasy XI through Ashita v4. It also includes a fix for an invalid sound handle bug.

Open the settings panel with `/vsound` or `/vanasound`.

## Demonstration

https://github.com/user-attachments/assets/97dad92d-e125-48c0-af52-993ed95d8abe

Play the video above to see the controls and hear the volume changes.

## Features

- Separate sliders for sound effects and interface sounds.
- Volume range from 0% to 100%, with both channels defaulting to 50%.
- Saved settings restored automatically between sessions.
- A compact panel that scales with the game display.
- An integrated invalid sound handle fix, enabled automatically.

## Installation

1. Download `VanaSound-v0.62.zip` from the [latest release](https://github.com/By-T0r/VanaSound/releases/latest).
2. Extract it into your Ashita directory. The ZIP places `VanaSound.dll` inside `plugins`.
3. Load it with `/load vanasound`, or add that command to your startup script.
4. Open the panel with `/vsound`.

## Controls

| Command | Action |
| --- | --- |
| `/vsound` | Open or close the panel |
| `/vsound sfx 50` | Set sound effects to 50% |
| `/vsound ui 50` | Set interface sounds to 50% |
| `/vsound status` | Show both volume settings |

`/vanasound` supports the same subcommands.

**Restore Defaults** sets both channels to 50%. Slider changes take effect during dragging and are saved when the slider is released.

## Invalid sound handle fix

### For players

During busy scenes with many sounds playing at once, this bug can cause an abrupt interruption of ambient audio or other active sounds. Ambient interruptions can be particularly noticeable: a long ambient sound may take a while to return after being cut off.

VanaSound includes an automatic correction for the invalid sound handle involved in this bug. There is nothing to configure or toggle.

This addresses a specific cause of sound interruptions. It does not prevent every possible audio cutoff or increase the number of sounds the game can play simultaneously.

### Technical details

The affected element playback path can receive `-1` from the native playback request and store it in the element's sound handle field.

VanaSound intercepts the playback request and converts **only a return value of `-1` from that specific caller to `0`**, before the native caller stores it. Zero represents the empty handle state used by this path.

Other callers retain their original return values. Within the corrected path, zero, valid handles and other negative values are left unchanged.

The correction shares the mixer's playback hook. It does not expand the voice pool, modify cache management or change voice selection policies.

## Compatibility and limitations

Requires **Ashita v4, x86 plugin interface 4.30**, and a supported FFXI client build. Initialization checks the client profile and relevant native instructions; unsupported builds and conflicting patches are rejected.

- Music volume is not controlled.
- Sound Effects covers selected native spatial playback routes, including many combat sounds. It is not an exhaustive classification of every sound asset.
- Some already-playing sounds retain their previous volume until the game updates them or they finish.
- The original voice capacity is preserved.
- A small lifecycle and error log is written beside the DLL. No sound capture or research instrumentation is included.

## Building

Requires Visual Studio C++ x86 tools and an external Ashita v4 SDK:

```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 -Sdk <SDK-directory>
```

The build runs regression tests for playback hooks, channel independence, invalid handle correction, settings persistence, client validation, Direct3D initialization and commands.

## License

MIT — Copyright © 2026 By-T0r.

Third-party components retain their respective licenses. See `THIRD_PARTY_NOTICES.md`.
