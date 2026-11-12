#pragma once
// SoundCloud / YouTube search for the phone. The phone can't run yt-dlp, so it
// asks crimserver (server/crimserver.py, running on the PC) to search and to
// download songs as MP3. Works like the desktop SoundCloud class: the slow
// network work happens on background threads, and the UI reads a snapshot.
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class Source { SoundCloud, YouTube };

struct RemoteTrack {
    std::string title;
    std::string uploader;
    std::string url;
    float duration = 0.0f;
    Source source = Source::SoundCloud;
};

enum class DownloadState { Downloading, Done, Failed };

struct Download {
    std::string url;
    DownloadState state = DownloadState::Downloading;
};

enum class SearchState { Idle, Searching, Results, Error };

struct OnlineSnapshot {
    SearchState state = SearchState::Idle;
    Source source = Source::SoundCloud;
    std::string query;
    std::vector<RemoteTrack> results;
    std::string error;
    std::vector<Download> downloads;
};

class Online {
    public:
        Online();
        ~Online();
        Online(const Online&) = delete;
        Online& operator=(const Online&) = delete;

        void startSearch(const std::string& query, Source source);
        void startDownload(const RemoteTrack& track);
        OnlineSnapshot snapshot();
        void clear();                   // back to Idle (results are forgotten)

        // Name of a song that finished downloading since the last call, or "".
        std::string takeFinishedDownload();

        // Address of crimserver, like "http://192.168.1.20:8765". Saved between runs.
        const std::string& server() const { return serverAddress; }
        void setServer(const std::string& address);

    private:
        void searchWorker(std::string server, std::string query, Source source);
        void downloadWorker(std::string server, RemoteTrack track);

        std::mutex mutex;
        OnlineSnapshot shared;
        std::vector<std::string> finished;
        std::string serverAddress;
        std::filesystem::path settingsFile;
        std::thread searchThread;
        std::vector<std::thread> downloadThreads;
};
