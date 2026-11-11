#pragma once
// Small helpers that use the Player and the Library together. The UI, the
// lock-screen buttons and the main loop all call these, so "next" behaves
// the same no matter where you press it.
#include "Library.hpp"
#include "Player.hpp"

namespace playback {
    inline void playIndex(Player& player, Library& library, std::size_t index) {
        if (index >= library.size()) return;
        library.setCurrent(index);
        player.play(library.currentSong());
    }

    // Play/pause. If nothing is loaded yet, start the current song.
    inline void toggle(Player& player, Library& library) {
        if (player.loaded()) player.togglePause();
        else if (!library.empty()) player.play(library.currentSong());
    }

    inline void resume(Player& player, Library& library) {
        if (player.loaded()) player.resume();
        else if (!library.empty()) player.play(library.currentSong());
    }

    inline void next(Player& player, Library& library) {
        if (library.empty()) return;
        library.next();
        player.play(library.currentSong());
    }

    // Like most music apps: more than 3 seconds in, "previous" restarts the song.
    inline void previous(Player& player, Library& library) {
        if (library.empty()) return;
        if (player.loaded() && player.position() > 3.0f) {
            player.seek(0.0f);
            return;
        }
        library.previous();
        player.play(library.currentSong());
    }

    // What to do when a song reaches its end.
    inline void songFinished(Player& player, Library& library) {
        if (library.empty()) return;
        if (library.repeatMode() == RepeatMode::One) player.play(library.currentSong());
        else if (library.repeatMode() == RepeatMode::Off && library.atEnd()) {
            // End of the list: rewind and pause, so the mini player stays visible.
            player.pause();
            player.seek(0.0f);
        }
        else next(player, library);
    }
}
