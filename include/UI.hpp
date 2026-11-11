#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
#include "CoverCache.hpp"
#include "Lyrics.hpp"
#include "Track.hpp"

class Player;
class Library;
struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
struct ImFont;
struct ImDrawList;

struct Area {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// One finger (or the mouse on desktop), read from ImGui every frame.
struct Touch {
    bool down = false;       // finger is on the screen
    bool pressed = false;    // finger touched down this frame
    bool released = false;   // finger lifted this frame
    float x = 0, y = 0;      // where it is now
    float startX = 0, startY = 0; // where it touched down
    bool moved = false;      // moved far enough that it's a drag, not a tap
};

class UI {
    public:
        UI() = default;
        UI(const UI&) = delete;
        UI& operator=(const UI&) = delete;
        ~UI();

        bool init();
        bool processEvents();
        void draw(Player& player, Library& library);

        bool inBackground() const { return background; }
        bool takeRescanRequest();
        void showToast(const std::string& message);

    private:
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        bool sdlReady = false;
        bool imguiReady = false;
        std::atomic<bool> background{false};
        bool rescanRequested = false;
        static bool onAppEvent(void* userdata, SDL_Event* event);

        ImFont* fontRegular = nullptr;
        ImFont* fontSemiBold = nullptr;
        ImFont* fontBold = nullptr;

        CoverCache covers{144};
        CoverCache bigCovers{768};
        Lyrics lyrics;

        Touch touch;
        void readTouch();
        bool tapped(const Area& area) const;

        // library screen
        char searchText[128] = "";
        float scroll = 0.0f;
        float scrollVelocity = 0.0f;
        float scrollStart = 0.0f;
        bool scrollTouch = false;
        void drawLibrary(Player& player, Library& library, Area screen, float bottom, bool input);
        void drawHeader(Library& library, Area a, bool input);
        void drawSongList(Player& player, Library& library, Area a, bool input);
        void drawEmpty(Area a, const char* title, const char* text);
        void drawMiniPlayer(Player& player, Library& library, Area a, bool input);
        void drawToast(Area above);

        std::string toast;
        float toastTime = 0.0f;

        // now playing sheet
        bool sheetOpen = false;
        float sheetSlide = 0.0f;   // 0 = hidden, 1 = fully open
        float sheetDrag = 0.0f;    // how far it's being pulled down
        // What the finger is doing on the now playing screen. Decided once it
        // has moved a bit: sideways on the cover = change song, down = close.
        enum class SheetGesture { None, PullDown, CoverSwipe, Ignored };
        SheetGesture gesture = SheetGesture::None;
        float coverOffset = 0.0f;  // sideways shift of the cover while/after swiping
        float coverFlash = 0.0f;   // seconds left to show the play/pause icon on the cover
        bool seeking = false;
        float seekFraction = 0.0f;
        std::filesystem::path sheetSong;
        void drawNowPlaying(Player& player, Library& library, Area screen);
        void drawNpCover(Area a, const Track& track, float alpha);
        void drawNpProgress(Player& player, Area a, bool input);
        void drawNpControls(Player& player, Library& library, Area a, bool input);
        void drawNpLyrics(Player& player, const Track& track, Area a);

        float lyricScroll = 0.0f;
        std::filesystem::path lyricSong;
};
