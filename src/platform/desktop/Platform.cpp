// Desktop (Linux) versions of the platform functions, so the phone UI can be
// tested on the PC. Nothing here is used in the iPhone build.
#include "Platform.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <string>

static std::filesystem::path chosenMusicFolder;

namespace platform {

std::filesystem::path musicFolder() {
    if (!chosenMusicFolder.empty()) return chosenMusicFolder;
    // Test folder: ~/.local/share/tgs0180/crimmusic-ios/Music
    // (start the program with a folder as argument to use another one).
    char* pref = SDL_GetPrefPath("tgs0180", "crimmusic-ios");
    std::filesystem::path folder = pref ? std::filesystem::path(pref) / "Music" : std::filesystem::path("Music");
    SDL_free(pref);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    chosenMusicFolder = folder;
    return folder;
}

void setMusicFolder(const std::filesystem::path& folder) {
    chosenMusicFolder = folder;
}

std::filesystem::path assetsFolder() {
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : "") / "assets";
}

static std::atomic<int> imported{0};

static std::filesystem::path freeName(const std::filesystem::path& folder, const std::filesystem::path& name) {
    std::filesystem::path target = folder / name;
    std::error_code error;
    for (int n = 2; std::filesystem::exists(target, error); n++) {
        target = folder / (name.stem().string() + " (" + std::to_string(n) + ")" + name.extension().string());
    }
    return target;
}

static void onFilesChosen(void*, const char* const* files, int) {
    if (files == nullptr) return;
    int count = 0;
    for (int i = 0; files[i] != nullptr; i++) {
        std::filesystem::path source = files[i];
        std::error_code error;
        std::filesystem::copy_file(source, freeName(musicFolder(), source.filename()), error);
        if (!error) count++;
    }
    imported += count;
}

void showImportPicker(SDL_Window* window) {
    static const SDL_DialogFileFilter filters[] = { { "MP3 files", "mp3" } };
    SDL_ShowOpenFileDialog(onFilesChosen, nullptr, window, filters, 1, nullptr, true);
}

int takeImportedCount() {
    return imported.exchange(0);
}

int httpGet(const std::string& url, const std::vector<std::pair<std::string, std::string>>& query, std::string& body) {
    // Same trick as desktop crimmusic: run the curl program.
    std::vector<std::string> args = { "curl", "-s", "-m", "8", "-A", "crimmusic (https://github.com/tgs0180/crimmusic)",
                                      "-w", "\n%{http_code}", "-G", url };
    for (const auto& [key, value] : query) {
        args.push_back("--data-urlencode");
        args.push_back(key + "=" + value);
    }
    std::vector<const char*> argv;
    for (const std::string& arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    SDL_Process* process = SDL_CreateProcess(argv.data(), true);
    if (process == nullptr) return 0;
    size_t size = 0;
    int exitCode = -1;
    std::string output;
    void* data = SDL_ReadProcess(process, &size, &exitCode);
    if (data != nullptr) {
        output.assign(static_cast<char*>(data), size);
        SDL_free(data);
    }
    SDL_DestroyProcess(process);
    if (exitCode != 0) return 0;

    std::size_t newline = output.rfind('\n');
    if (newline == std::string::npos) return 0;
    body = output.substr(0, newline);
    try {
        return std::stoi(output.substr(newline + 1));
    }
    catch (const std::exception&) {
        return 0;
    }
}

Insets safeArea(SDL_Window*) {
    // Pretend to be a phone with a notch, so the layout can be checked.
    return Insets{ 47.0f, 34.0f, 0.0f, 0.0f };
}

}
