#include "Online.hpp"
#include "Platform.hpp"
#include <SDL3/SDL.h>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>

Online::Online() {
    // The server address is kept in a small text file in the app's private
    // settings folder (not in Documents, so it doesn't show up in Files).
    char* pref = SDL_GetPrefPath("tgs0180", "crimmusic");
    if (pref != nullptr) {
        settingsFile = std::filesystem::path(pref) / "server.txt";
        SDL_free(pref);
        std::ifstream in(settingsFile);
        std::getline(in, serverAddress);
    }
}

Online::~Online() {
    if (searchThread.joinable()) searchThread.join();
    for (std::thread& thread : downloadThreads) {
        if (thread.joinable()) thread.join();
    }
}

void Online::setServer(const std::string& address) {
    serverAddress = address;
    // People type "192.168.1.20:8765"; the HTTP code needs the "http://" too.
    if (!serverAddress.empty() && serverAddress.find("://") == std::string::npos) serverAddress = "http://" + serverAddress;
    while (!serverAddress.empty() && serverAddress.back() == '/') serverAddress.pop_back();
    if (!settingsFile.empty()) {
        std::ofstream out(settingsFile);
        out << serverAddress << "\n";
    }
}

OnlineSnapshot Online::snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return shared;
}

void Online::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    if (shared.state == SearchState::Searching) return; // the worker will fill it in soon
    shared.state = SearchState::Idle;
    shared.results.clear();
    shared.error.clear();
}

std::string Online::takeFinishedDownload() {
    std::lock_guard<std::mutex> lock(mutex);
    if (finished.empty()) return "";
    std::string name = finished.front();
    finished.erase(finished.begin());
    return name;
}

// ------------------------------------------------------------------ search
void Online::startSearch(const std::string& query, Source source) {
    std::lock_guard<std::mutex> lock(mutex);
    if (shared.state == SearchState::Searching) return;
    shared.state = SearchState::Searching;
    shared.source = source;
    shared.query = query;
    shared.results.clear();
    shared.error.clear();
    if (serverAddress.empty()) {
        shared.state = SearchState::Error;
        shared.error = "Fill in the server address first";
        return;
    }
    if (searchThread.joinable()) searchThread.join();
    searchThread = std::thread(&Online::searchWorker, this, serverAddress, query, source);
}

void Online::searchWorker(std::string server, std::string query, Source source) {
    std::string body;
    const char* sourceName = source == Source::YouTube ? "youtube" : "soundcloud";
    // yt-dlp needs a few seconds to search, so wait longer than for lyrics.
    int status = platform::httpGet(server + "/search", { {"q", query}, {"source", sourceName} }, body, 60);

    std::vector<RemoteTrack> results;
    std::string error;
    if (status == 0) error = "Can't reach the server. Is crimserver running?";
    else if (status != 200) error = "Server error: " + body;
    else {
        try {
            for (const nlohmann::json& entry : nlohmann::json::parse(body)) {
                RemoteTrack track;
                track.title = entry.value("title", "Unknown");
                track.uploader = entry.value("uploader", "Unknown");
                track.url = entry.value("url", "");
                if (entry.contains("duration") && entry["duration"].is_number()) track.duration = entry["duration"].get<float>();
                track.source = source;
                if (!track.url.empty()) results.push_back(track);
            }
        }
        catch (const std::exception&) {
            error = "The server sent something strange";
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    shared.results = std::move(results);
    shared.error = error;
    shared.state = error.empty() ? SearchState::Results : SearchState::Error;
}

// ------------------------------------------------------------------ download
void Online::startDownload(const RemoteTrack& track) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (serverAddress.empty()) return;
        bool found = false;
        for (Download& download : shared.downloads) {
            if (download.url == track.url) {
                if (download.state != DownloadState::Failed) return; // already busy or done
                download.state = DownloadState::Downloading;
                found = true;
                break;
            }
        }
        if (!found) shared.downloads.push_back(Download{track.url});
    }
    downloadThreads.emplace_back(&Online::downloadWorker, this, serverAddress, track);
}

// Characters that aren't allowed (or are awkward) in file names.
static std::string safeFileName(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            static_cast<unsigned char>(c) < 32) c = '_';
    }
    while (!name.empty() && (name.front() == '.' || name.front() == ' ')) name.erase(name.begin());
    if (name.size() > 150) name.resize(150);
    return name.empty() ? "song" : name;
}

void Online::downloadWorker(std::string server, RemoteTrack track) {
    std::string body;
    // The server only answers once yt-dlp has finished, which can take a while.
    int status = platform::httpGet(server + "/download", { {"url", track.url} }, body, 300);
    bool ok = status == 200 && !body.empty();

    std::string name = track.source == Source::YouTube ? track.title : track.uploader + " - " + track.title;
    name = safeFileName(name);
    if (ok) {
        // Write to a ".part" file first and rename it when it's complete, so
        // the library never sees half a song.
        std::filesystem::path folder = platform::musicFolder();
        std::filesystem::path partial = folder / (name + ".part");
        {
            std::ofstream out(partial, std::ios::binary);
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
            ok = static_cast<bool>(out);
        }
        std::filesystem::path target = folder / (name + ".mp3");
        std::error_code error;
        for (int n = 2; std::filesystem::exists(target, error); n++) {
            target = folder / (name + " (" + std::to_string(n) + ").mp3");
        }
        if (ok) std::filesystem::rename(partial, target, error);
        if (!ok || error) {
            ok = false;
            std::filesystem::remove(partial, error);
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    for (Download& download : shared.downloads) {
        if (download.url == track.url) download.state = ok ? DownloadState::Done : DownloadState::Failed;
    }
    if (ok) finished.push_back(track.title);
}
