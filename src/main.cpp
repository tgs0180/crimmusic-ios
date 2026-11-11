#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // on iPhone, SDL starts the app and then calls this main()
#include <iostream>
#include <string>
#include "Library.hpp"
#include "NowPlaying.hpp"
#include "Platform.hpp"
#include "Playback.hpp"
#include "Player.hpp"
#include "UI.hpp"

int main(int argc, char* argv[]) {
    // Desktop test build: `crimmusic ~/Music` uses that folder instead.
    if (argc > 1) platform::setMusicFolder(argv[1]);

    Player player;
    if (!player.init()) return 1;

    UI ui;
    if (!ui.init()) return 1;

    Library library;
    library.scan(platform::musicFolder());
    std::cout << "Found " << library.size() << " songs in " << platform::musicFolder() << "\n";

    NowPlaying nowPlaying;

    while (ui.processEvents()) {
        nowPlaying.poll(player, library);
        if (player.isFinished()) playback::songFinished(player, library);

        int imported = platform::takeImportedCount();
        if (imported > 0 || ui.takeRescanRequest()) {
            library.rescan();
            if (imported > 0) ui.showToast(imported == 1 ? "Added 1 song" : "Added " + std::to_string(imported) + " songs");
        }

        if (ui.inBackground()) {
            // Hidden (screen locked / other app): don't draw, just keep the music going.
            SDL_Delay(100);
            continue;
        }
        ui.draw(player, library);
    }
    return 0;
}
