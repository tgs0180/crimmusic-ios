#pragma once
// Lock-screen / Control Center controls and "Now Playing" info.
// iPhone:  src/platform/ios/NowPlaying.mm (MPNowPlayingInfoCenter + MPRemoteCommandCenter)
// Desktop: src/platform/desktop/NowPlaying.cpp (does nothing)
#include <filesystem>
#include <memory>

class Player;
class Library;

class NowPlaying {
    public:
        NowPlaying();
        ~NowPlaying();
        NowPlaying(const NowPlaying&) = delete;
        NowPlaying& operator=(const NowPlaying&) = delete;

        // Call once per loop. Runs the buttons pressed on the lock screen
        // and keeps the lock-screen info (title, cover, time) up to date.
        void poll(Player& player, Library& library);

        struct Impl;
    private:
        std::unique_ptr<Impl> impl;
};
