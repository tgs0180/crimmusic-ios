#pragma once
// Everything that works differently on iPhone and on the desktop test build.
// iPhone:  src/platform/ios/Platform.mm      (Objective-C++)
// Desktop: src/platform/desktop/Platform.cpp
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;

namespace platform {
    // Folder that holds the songs. On iPhone this is the app's Documents
    // folder, which also shows up in the Files app ("On My iPhone > crimmusic").
    std::filesystem::path musicFolder();

    // Desktop test build only: use another music folder (ignored on iPhone).
    void setMusicFolder(const std::filesystem::path& folder);

    // Folder with fonts that ship with the app.
    std::filesystem::path assetsFolder();

    // Opens the system file picker for MP3s. Picked files are copied into
    // musicFolder() in the background; takeImportedCount() reports them.
    void showImportPicker(SDL_Window* window);
    int takeImportedCount();

    // Simple HTTP GET. The query parameters get URL-encoded for you.
    // Returns the HTTP status code (200, 404, ...) or 0 when there was no answer.
    int httpGet(const std::string& url,
                const std::vector<std::pair<std::string, std::string>>& query,
                std::string& body);

    // Extra space to keep clear at the top/bottom of the screen
    // (notch, Dynamic Island, home indicator), in points.
    struct Insets { float top, bottom, left, right; };
    Insets safeArea(SDL_Window* window);
}
