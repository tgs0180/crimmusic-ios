# crimmusic-ios

The iPhone version of [crimmusic](https://github.com/tgs0180/crimmusic): a music player for your own MP3s, with synced lyrics, in the crimson theme.

## What it does
- **Library** with covers, search, Play and Shuffle buttons, and a mini player at the bottom.
- **Now playing** screen: big cover, time bar you can drag, shuffle/repeat, and scrolling synced lyrics. Swipe down or tap the arrow to close it.
- **Adding songs:**
  - Tap **+** and pick MP3s from Files, iCloud Drive or Downloads. They get copied into the app.
  - Or open the **Files** app and copy MP3s into *On My iPhone > crimmusic*. The app picks them up when you switch back to it.
- **Lyrics** are downloaded from [lrclib.net](https://lrclib.net) and saved as a `.lrc` file next to the song. You can also copy `.lrc` files from the desktop version into the same folder.
- **Lock screen and Control Center:** title, artist, cover, play/pause, next/previous and the time bar. It keeps playing in the background, and pauses when headphones are unplugged.

Left out compared to desktop: SoundCloud/YouTube, Discord status, and making/editing lyrics.

## How the code is organised
| File | What it does |
|---|---|
| `src/main.cpp` | The main loop: input, lock-screen commands, next song, drawing |
| `src/UI.cpp` | All the screens, drawn by hand with ImGui's draw list (touch-friendly) |
| `src/Player.cpp` | Plays audio with miniaudio |
| `src/Library.cpp` | Finds the MP3s and reads their tags (TagLib) |
| `src/Lyrics.cpp` | Finds and parses synced lyrics |
| `src/CoverCache.cpp` | Loads covers in the background |
| `include/Playback.hpp` | Next / previous / play-pause, shared by the UI and the lock screen |
| `include/Platform.hpp` | Things that differ per platform (folders, file picker, HTTP, safe area) |
| `src/platform/ios/*.mm` | iPhone versions, in Objective-C++ |
| `src/platform/desktop/*.cpp` | Linux versions, so you can test the UI on the PC |
| `ios/Info.plist.in` | iOS settings: background audio, Files app access, dark mode, portrait |
| `.github/workflows/ios.yml` | Builds the `.ipa` on a Mac in the cloud |

## Testing on Linux
The same code builds as a desktop program in a phone-sized window. The mouse acts as your finger, and the lock screen part does nothing.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/crimmusic ~/Music      # or leave out the folder to use a test folder
```
The first configure downloads SDL3, Dear ImGui and TagLib, so it takes a minute.

## Getting it on the iPhone
1. Push to `main` on GitHub. The **Build iOS app** workflow builds `crimmusic.ipa` on a macOS runner.
2. On the iPhone, download `crimmusic.ipa` from the repo's **Releases > latest** page.
3. Open it with **SideStore** to install. Like other sideloaded apps, it needs refreshing every 7 days.

Why a Mac in the cloud: iOS apps can only be built with Apple's Xcode, which only runs on macOS.
