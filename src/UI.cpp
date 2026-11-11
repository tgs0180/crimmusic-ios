#include "UI.hpp"
#include "Library.hpp"
#include "Platform.hpp"
#include "Playback.hpp"
#include "Player.hpp"
#include "StringUtils.hpp"
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iostream>

// Everything here is drawn by hand with ImGui's draw list (shapes + text)
// instead of ImGui widgets, because widgets are made for a mouse. Positions
// are in "points": an iPhone is about 390 x 844 points.

// ------------------------------------------------------------------ colours
namespace palette {
    constexpr unsigned int base    = 0x0d0809;
    constexpr unsigned int surface = 0x1b1114;
    constexpr unsigned int overlay = 0x2a191d;
    constexpr unsigned int muted   = 0x75595f;
    constexpr unsigned int subtle  = 0xa8898f;
    constexpr unsigned int text    = 0xefe1e3;
    constexpr unsigned int pink    = 0xff6b9a;
    constexpr unsigned int crimson = 0xe01040;
    constexpr unsigned int bright  = 0xfc0556;
    constexpr unsigned int blood   = 0x8c0a26;
}

static ImVec4 hexColor(unsigned int rgb, float alpha = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

static ImU32 col(unsigned int rgb, float alpha = 1.0f) {
    return ImGui::GetColorU32(hexColor(rgb, alpha));
}

// ------------------------------------------------------------------ text helpers
static float textWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

static void drawText(ImDrawList* dl, ImFont* font, float size, float x, float y, ImU32 colour, const char* text) {
    dl->AddText(font, size, ImVec2(x, y), colour, text);
}

// Cuts text off with "..." so it fits in maxWidth (careful not to cut a UTF-8 letter in half).
static std::string fitText(ImFont* font, float size, const std::string& text, float maxWidth) {
    if (textWidth(font, size, text.c_str()) <= maxWidth) return text;
    std::string cut = text;
    while (!cut.empty() && textWidth(font, size, (cut + "...").c_str()) > maxWidth) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
    }
    return cut + "...";
}

static void formatTime(float seconds, char* buffer, std::size_t size) {
    int total = std::max(0, static_cast<int>(seconds));
    std::snprintf(buffer, size, "%d:%02d", total / 60, total % 60);
}

static float easeOut(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// ------------------------------------------------------------------ icons
// Vector icons, so they stay sharp on a 3x iPhone screen. Coordinates are on a
// 24x24 grid (like the Lucide icon set the desktop app uses), scaled to `size`.
namespace icon {
    struct Grid {
        float cx, cy, s;
        ImVec2 operator()(float x, float y) const { return ImVec2(cx + (x - 12.0f) * s, cy + (y - 12.0f) * s); }
    };

    static void play(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        dl->AddTriangleFilled(g(7, 4), g(20, 12), g(7, 20), c);
    }

    static void pause(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        dl->AddRectFilled(g(6, 4), g(10, 20), c, size * 0.06f);
        dl->AddRectFilled(g(14, 4), g(18, 20), c, size * 0.06f);
    }

    static void next(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        dl->AddTriangleFilled(g(4, 4.5f), g(16.5f, 12), g(4, 19.5f), c);
        dl->AddRectFilled(g(17.5f, 4.5f), g(20.5f, 19.5f), c, size * 0.04f);
    }

    static void previous(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        dl->AddTriangleFilled(g(20, 4.5f), g(20, 19.5f), g(7.5f, 12), c);
        dl->AddRectFilled(g(3.5f, 4.5f), g(6.5f, 19.5f), c, size * 0.04f);
    }

    static void shuffle(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        float w = std::max(1.5f, size * 0.085f);
        dl->PathLineTo(g(2, 18)); dl->PathLineTo(g(4, 18));
        dl->PathBezierCubicCurveTo(g(10, 18), g(10.5f, 6), g(16.5f, 6));
        dl->PathLineTo(g(21, 6));
        dl->PathStroke(c, 0, w);
        dl->PathLineTo(g(2, 6)); dl->PathLineTo(g(4, 6));
        dl->PathBezierCubicCurveTo(g(10, 6), g(10.5f, 18), g(16.5f, 18));
        dl->PathLineTo(g(21, 18));
        dl->PathStroke(c, 0, w);
        ImVec2 top[3] = { g(18, 2.5f), g(21.5f, 6), g(18, 9.5f) };
        ImVec2 bottom[3] = { g(18, 14.5f), g(21.5f, 18), g(18, 21.5f) };
        dl->AddPolyline(top, 3, c, 0, w);
        dl->AddPolyline(bottom, 3, c, 0, w);
    }

    static void repeat(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        float s = size / 24.0f;
        float w = std::max(1.5f, size * 0.085f);
        const float pi = 3.14159265f;
        dl->PathLineTo(g(3, 11));
        dl->PathArcTo(g(7, 10), 4 * s, pi, pi * 1.5f);
        dl->PathLineTo(g(21, 6));
        dl->PathStroke(c, 0, w);
        dl->PathLineTo(g(21, 13));
        dl->PathArcTo(g(17, 14), 4 * s, 0.0f, pi * 0.5f);
        dl->PathLineTo(g(3, 18));
        dl->PathStroke(c, 0, w);
        ImVec2 top[3] = { g(17, 2), g(21, 6), g(17, 10) };
        ImVec2 bottom[3] = { g(7, 22), g(3, 18), g(7, 14) };
        dl->AddPolyline(top, 3, c, 0, w);
        dl->AddPolyline(bottom, 3, c, 0, w);
    }

    static void plus(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        float w = std::max(2.0f, size * 0.1f);
        dl->AddLine(g(12, 5), g(12, 19), c, w);
        dl->AddLine(g(5, 12), g(19, 12), c, w);
    }

    static void search(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        float w = std::max(1.5f, size * 0.09f);
        dl->AddCircle(g(11, 11), 7 * size / 24.0f, c, 0, w);
        dl->AddLine(g(16.2f, 16.2f), g(21, 21), c, w);
    }

    static void cross(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        float w = std::max(1.5f, size * 0.09f);
        dl->AddLine(g(6, 6), g(18, 18), c, w);
        dl->AddLine(g(18, 6), g(6, 18), c, w);
    }

    static void chevronDown(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        ImVec2 points[3] = { g(5, 9), g(12, 16), g(19, 9) };
        dl->AddPolyline(points, 3, c, 0, std::max(2.0f, size * 0.1f));
    }

    static void note(ImDrawList* dl, float cx, float cy, float size, ImU32 c) {
        Grid g{cx, cy, size / 24.0f};
        float s = size / 24.0f;
        float w = std::max(1.5f, size * 0.08f);
        ImVec2 stem[4] = { g(9, 18), g(9, 5), g(21, 3), g(21, 16) };
        dl->AddPolyline(stem, 4, c, 0, w);
        dl->AddCircleFilled(g(6, 18), 3 * s, c);
        dl->AddCircleFilled(g(18, 16), 3 * s, c);
    }

    // Three bouncing bars next to the song that's playing.
    static void equalizer(ImDrawList* dl, float cx, float cy, float size, ImU32 c, bool moving) {
        float t = static_cast<float>(ImGui::GetTime());
        float barW = size * 0.2f;
        for (int i = 0; i < 3; i++) {
            float h = moving ? (0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * (5.0f + i * 1.7f) + i * 1.3f))) : 0.35f;
            float x = cx - size / 2 + i * (barW + size * 0.2f);
            dl->AddRectFilled(ImVec2(x, cy + size / 2 - size * h), ImVec2(x + barW, cy + size / 2), c, barW * 0.4f);
        }
    }
}

// ------------------------------------------------------------------ setup
static void applyTheme() {
    using namespace palette;
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.WindowBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.FramePadding = ImVec2(0, 0);
    style.Colors[ImGuiCol_WindowBg] = hexColor(base);
    style.Colors[ImGuiCol_Text] = hexColor(text);
    style.Colors[ImGuiCol_TextDisabled] = hexColor(muted);
    style.Colors[ImGuiCol_TextSelectedBg] = hexColor(crimson, 0.35f);
    style.Colors[ImGuiCol_FrameBg] = hexColor(base, 0.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = hexColor(base, 0.0f);
    style.Colors[ImGuiCol_FrameBgActive] = hexColor(base, 0.0f);
    style.Colors[ImGuiCol_InputTextCursor] = hexColor(bright);
    style.Colors[ImGuiCol_NavCursor] = hexColor(base, 0.0f);
}

bool UI::init() {
    SDL_SetAppMetadata("crimmusic", "1.0", "com.tgs0180.crimmusic");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }
    sdlReady = true;
    SDL_AddEventWatch(onAppEvent, this);

    // On the desktop this opens a phone-sized window; on iPhone it's full screen.
    window = SDL_CreateWindow("crimmusic", 390, 844, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::cerr << "Failed to create window: " << SDL_GetError() << "\n";
        return false;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // don't write imgui.ini into the music folder
    applyTheme();

    // Inter for text, plus Noto Sans Math merged in for symbols Inter doesn't
    // have (like the ⧸ that yt-dlp puts in file names).
    std::filesystem::path fonts = platform::assetsFolder() / "fonts";
    std::string math = (fonts / "NotoSansMath-Regular.ttf").string();
    auto load = [&](const char* file) {
        ImFont* font = io.Fonts->AddFontFromFileTTF((fonts / file).string().c_str(), 16.0f);
        if (font == nullptr) return font;
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(math.c_str(), 16.0f, &merge);
        return font;
    };
    fontRegular = load("Inter_18pt-Regular.ttf");
    fontSemiBold = load("Inter_18pt-SemiBold.ttf");
    fontBold = load("Inter_18pt-Bold.ttf");
    if (fontRegular == nullptr || fontSemiBold == nullptr || fontBold == nullptr) {
        std::cerr << "Could not load fonts from " << fonts << "\n";
        return false;
    }
    io.FontDefault = fontRegular;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    imguiReady = true;
    return true;
}

UI::~UI() {
    covers.clear();
    bigCovers.clear();
    if (imguiReady) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (sdlReady) {
        SDL_RemoveEventWatch(onAppEvent, this);
        SDL_Quit();
    }
}

// iOS sends these the moment the app is hidden or shown again. While in the
// background we must not draw (iOS doesn't allow GPU work there), but the main
// loop keeps running so the next song still starts.
bool UI::onAppEvent(void* userdata, SDL_Event* event) {
    UI* ui = static_cast<UI*>(userdata);
    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            ui->background = true;
            break;
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            ui->background = false;
            break;
        default:
            break;
    }
    return true;
}

bool UI::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_TERMINATING) return false;
        // Songs may have been added or removed in the Files app meanwhile.
        if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) rescanRequested = true;
    }
    return true;
}

bool UI::takeRescanRequest() {
    bool result = rescanRequested;
    rescanRequested = false;
    return result;
}

void UI::showToast(const std::string& message) {
    toast = message;
    toastTime = 2.5f;
}

// ------------------------------------------------------------------ touch
void UI::readTouch() {
    ImGuiIO& io = ImGui::GetIO();
    touch.down = io.MouseDown[0];
    touch.pressed = io.MouseClicked[0];
    touch.released = io.MouseReleased[0];
    touch.x = io.MousePos.x;
    touch.y = io.MousePos.y;
    if (touch.pressed) {
        touch.startX = touch.x;
        touch.startY = touch.y;
        touch.moved = false;
    }
    if (touch.down || touch.released) {
        float dx = touch.x - touch.startX;
        float dy = touch.y - touch.startY;
        if (dx * dx + dy * dy > 10.0f * 10.0f) touch.moved = true;
    }
}

// A tap: finger went down and up inside the area without dragging.
bool UI::tapped(const Area& area) const {
    return touch.released && !touch.moved && area.contains(touch.startX, touch.startY) && area.contains(touch.x, touch.y);
}

// ------------------------------------------------------------------ frame
void UI::draw(Player& player, Library& library) {
    covers.update(renderer);
    bigCovers.update(renderer);
    lyrics.update();

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    // Points vs pixels: the UI is laid out in points (an iPhone is ~390 wide),
    // but the screen has 3 pixels per point. Tell ImGui the real ratio, measured
    // from the renderer itself. It's used for sharp text and, at the end of
    // draw(), for SDL_SetRenderScale.
    int pixelW = 0, pixelH = 0, pointW = 0, pointH = 0;
    SDL_GetRenderOutputSize(renderer, &pixelW, &pixelH);
    SDL_GetWindowSize(window, &pointW, &pointH);
    if (pointW > 0 && pointH > 0 && pixelW > 0 && pixelH > 0) {
        ImGui::GetIO().DisplayFramebufferScale = ImVec2(static_cast<float>(pixelW) / pointW, static_cast<float>(pixelH) / pointH);
    }

    ImGui::NewFrame();
    readTouch();

    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    platform::Insets insets = platform::safeArea(window);
    Area screen{ viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y };

    // Open/close animation of the now playing screen.
    float step = dt / 0.3f;
    sheetSlide = std::clamp(sheetSlide + (sheetOpen ? step : -step), 0.0f, 1.0f);
    bool sheetVisible = sheetSlide > 0.0f;
    bool libraryInput = !sheetOpen && sheetSlide == 0.0f;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("library", nullptr, flags);

    Area content{ screen.x + insets.left, screen.y + insets.top, screen.w - insets.left - insets.right, screen.h - insets.top - insets.bottom };
    const float miniHeight = 64.0f;
    Area mini{ content.x + 8, content.y + content.h - miniHeight - 6, content.w - 16, miniHeight };
    float listBottom = library.empty() ? content.y + content.h : mini.y - 6;

    drawLibrary(player, library, content, listBottom, libraryInput);
    if (!library.empty()) drawMiniPlayer(player, library, mini, libraryInput);

    if (toastTime > 0.0f) {
        toastTime -= dt;
        drawToast(Area{ content.x, listBottom - 56, content.w, 44 });
    }
    ImGui::End();

    if (sheetVisible) {
        float offset = (1.0f - easeOut(sheetSlide)) * screen.h + sheetDrag;
        ImGui::SetNextWindowPos(ImVec2(screen.x, screen.y + offset));
        ImGui::SetNextWindowSize(viewport->Size);
        // (no NoBringToFrontOnFocus here, or ImGui would put this window behind the library)
        ImGuiWindowFlags sheetFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("nowplaying", nullptr, sheetFlags);
        drawNowPlaying(player, library, Area{ screen.x, screen.y + offset, screen.w, screen.h });
        ImGui::End();
    }

    ImGui::Render();
    ImVec4 clear = hexColor(palette::base);
    SDL_SetRenderDrawColorFloat(renderer, clear.x, clear.y, clear.z, 1.0f);
    SDL_RenderClear(renderer);
    // ImGui gives its shapes in points; this makes SDL multiply them by the
    // pixels-per-point ratio (3 on an iPhone 15, 1 on a normal PC screen).
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    SDL_SetRenderScale(renderer, scale.x, scale.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

// ------------------------------------------------------------------ library screen
void UI::drawLibrary(Player& player, Library& library, Area content, float bottom, bool input) {
    const float headerHeight = 108.0f;
    drawHeader(library, Area{ content.x, content.y, content.w, headerHeight }, input);
    Area list{ content.x, content.y + headerHeight, content.w, bottom - (content.y + headerHeight) };

    if (library.empty()) {
        drawEmpty(list, "No songs yet", "Tap + to import MP3s, or copy them into\nFiles > On My iPhone > crimmusic");
        return;
    }
    drawSongList(player, library, list, input);
}

void UI::drawHeader(Library& library, Area a, bool input) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float side = 20.0f;

    // Title + song count
    drawText(dl, fontBold, 32.0f, a.x + side, a.y + 8, col(palette::text), "Library");
    char count[32];
    std::snprintf(count, sizeof(count), library.size() == 1 ? "%zu song" : "%zu songs", library.size());
    float titleW = textWidth(fontBold, 32.0f, "Library");
    drawText(dl, fontRegular, 14.0f, a.x + side + titleW + 10, a.y + 24, col(palette::subtle), count);

    // Import button
    float r = 20.0f;
    float bx = a.x + a.w - side - r;
    float by = a.y + 8 + 19;
    Area importButton{ bx - r - 6, by - r - 6, (r + 6) * 2, (r + 6) * 2 };
    bool held = input && touch.down && importButton.contains(touch.startX, touch.startY) && !touch.moved;
    dl->AddCircleFilled(ImVec2(bx, by), r, col(held ? palette::bright : palette::crimson));
    icon::plus(dl, bx, by, 22.0f, col(palette::text));
    if (input && tapped(importButton)) platform::showImportPicker(window);

    // Search field: a real ImGui text box (so the keyboard works) on top of a drawn background.
    Area field{ a.x + 16, a.y + 58, a.w - 32, 40 };
    dl->AddRectFilled(ImVec2(field.x, field.y), ImVec2(field.x + field.w, field.y + field.h), col(palette::surface), 12.0f);
    icon::search(dl, field.x + 22, field.y + field.h / 2, 18.0f, col(palette::subtle));

    bool hasText = searchText[0] != '\0';
    float textX = field.x + 42;
    float textW = field.w - 42 - (hasText ? 40 : 12);
    if (input) {
        ImGui::PushFont(fontRegular, 16.0f);
        ImGui::SetCursorScreenPos(ImVec2(textX, field.y + (field.h - ImGui::GetFrameHeight()) / 2));
        ImGui::SetNextItemWidth(textW);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, hexColor(palette::muted));
        ImGui::InputTextWithHint("##search", "Search songs or artists", searchText, sizeof(searchText));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    else {
        const char* shown = hasText ? searchText : "Search songs or artists";
        drawText(dl, fontRegular, 16.0f, textX, field.y + 11, col(hasText ? palette::text : palette::muted),
                 fitText(fontRegular, 16.0f, shown, textW).c_str());
    }

    if (hasText) {
        float cx = field.x + field.w - 20;
        float cy = field.y + field.h / 2;
        dl->AddCircleFilled(ImVec2(cx, cy), 9.0f, col(palette::muted));
        icon::cross(dl, cx, cy, 12.0f, col(palette::base));
        if (input && tapped(Area{ cx - 20, field.y, 40, field.h })) {
            searchText[0] = '\0';
            scroll = 0.0f;
        }
    }
}

void UI::drawEmpty(Area a, const char* title, const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = a.y + a.h * 0.38f;
    dl->AddCircleFilled(ImVec2(a.x + a.w / 2, cy), 44.0f, col(palette::surface));
    icon::note(dl, a.x + a.w / 2, cy, 40.0f, col(palette::crimson));
    float y = cy + 64;
    drawText(dl, fontBold, 22.0f, a.x + (a.w - textWidth(fontBold, 22.0f, title)) / 2, y, col(palette::text), title);
    y += 34;
    // centre each line of the explanation
    std::string all = text;
    std::size_t start = 0;
    while (start <= all.size()) {
        std::size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(start, end - start);
        drawText(dl, fontRegular, 15.0f, a.x + (a.w - textWidth(fontRegular, 15.0f, line.c_str())) / 2, y, col(palette::subtle), line.c_str());
        y += 22;
        start = end + 1;
    }
}

void UI::drawSongList(Player& player, Library& library, Area a, bool input) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);

    // Which songs match the search?
    std::string query = toLower(searchText);
    std::vector<std::size_t> shown;
    shown.reserve(library.size());
    for (std::size_t i = 0; i < library.size(); i++) {
        const Track& t = library.song(i);
        if (query.empty() || toLower(t.title + " " + t.artist).find(query) != std::string::npos) shown.push_back(i);
    }

    const float buttonsHeight = 64.0f;
    const float rowHeight = 64.0f;
    float contentHeight = buttonsHeight + rowHeight * shown.size() + 8.0f;
    float maxScroll = std::max(0.0f, contentHeight - a.h);

    // --- scrolling: drag with the finger, then let it glide and slow down
    if (input && touch.pressed && a.contains(touch.x, touch.y)) {
        scrollTouch = true;
        scrollStart = scroll;
        scrollVelocity = 0.0f;
    }
    if (scrollTouch && touch.down && touch.moved) {
        float target = scrollStart - (touch.y - touch.startY);
        if (dt > 0.0f) scrollVelocity = scrollVelocity * 0.5f + ((target - scroll) / dt) * 0.5f;
        scroll = target;
    }
    if (scrollTouch && !touch.down) scrollTouch = false;
    if (!scrollTouch) {
        scroll += scrollVelocity * dt;
        scrollVelocity *= std::exp(-dt * 3.0f);
        if (std::fabs(scrollVelocity) < 4.0f) scrollVelocity = 0.0f;
    }
    if (input && a.contains(touch.x, touch.y)) scroll -= ImGui::GetIO().MouseWheel * 80.0f; // desktop mouse wheel
    if (scroll < 0.0f || scroll > maxScroll) scrollVelocity = 0.0f;
    scroll = std::clamp(scroll, 0.0f, maxScroll);

    dl->PushClipRect(ImVec2(a.x, a.y), ImVec2(a.x + a.w, a.y + a.h), true);
    float top = a.y - scroll;

    // --- Play / Shuffle buttons
    if (top + buttonsHeight > a.y) {
        float gap = 12.0f;
        float w = (a.w - 32 - gap) / 2;
        Area playButton{ a.x + 16, top + 6, w, 44 };
        Area shuffleButton{ a.x + 16 + w + gap, top + 6, w, 44 };
        for (int b = 0; b < 2; b++) {
            Area area = b == 0 ? playButton : shuffleButton;
            bool held = input && touch.down && !touch.moved && area.contains(touch.startX, touch.startY);
            dl->AddRectFilled(ImVec2(area.x, area.y), ImVec2(area.x + area.w, area.y + area.h), col(held ? palette::overlay : palette::surface), 12.0f);
            const char* label = b == 0 ? "Play" : "Shuffle";
            float labelW = textWidth(fontSemiBold, 16.0f, label);
            float groupW = 20 + 8 + labelW;
            float gx = area.x + (area.w - groupW) / 2;
            float cy = area.y + area.h / 2;
            if (b == 0) icon::play(dl, gx + 10, cy, 18.0f, col(palette::crimson));
            else icon::shuffle(dl, gx + 10, cy, 18.0f, col(palette::crimson));
            drawText(dl, fontSemiBold, 16.0f, gx + 28, cy - 9, col(palette::crimson), label);
        }
        if (input && !shown.empty() && tapped(playButton) && a.contains(touch.x, touch.y)) {
            library.setShuffle(false);
            playback::playIndex(player, library, shown.front());
        }
        if (input && !shown.empty() && tapped(shuffleButton) && a.contains(touch.x, touch.y)) {
            library.setCurrent(shown[static_cast<std::size_t>(SDL_rand(static_cast<Sint32>(shown.size())))]);
            library.setShuffle(true);
            player.play(library.currentSong());
        }
    }

    if (shown.empty()) {
        dl->PopClipRect();
        std::string message = std::string("Nothing matches \"") + searchText + "\"";
        message = fitText(fontRegular, 15.0f, message, a.w - 40);
        drawText(dl, fontRegular, 15.0f, a.x + (a.w - textWidth(fontRegular, 15.0f, message.c_str())) / 2, a.y + buttonsHeight + 30, col(palette::subtle), message.c_str());
        return;
    }

    // --- song rows (only the ones on screen are drawn)
    float rowsTop = top + buttonsHeight;
    int first = std::max(0, static_cast<int>((a.y - rowsTop) / rowHeight));
    int last = std::min(static_cast<int>(shown.size()) - 1, static_cast<int>((a.y + a.h - rowsTop) / rowHeight));
    const float side = 16.0f;
    const float cover = 48.0f;
    bool playing = player.isPlaying();

    for (int k = first; k <= last; k++) {
        std::size_t index = shown[static_cast<std::size_t>(k)];
        const Track& track = library.song(index);
        Area row{ a.x, rowsTop + k * rowHeight, a.w, rowHeight };
        bool isCurrent = player.loaded() && track.file == player.file();

        bool held = input && touch.down && !touch.moved && row.contains(touch.startX, touch.startY) && a.contains(touch.startX, touch.startY);
        if (held) dl->AddRectFilled(ImVec2(row.x, row.y), ImVec2(row.x + row.w, row.y + row.h), col(palette::surface));

        // cover
        float cx = row.x + side;
        float cy = row.y + (rowHeight - cover) / 2;
        covers.request(track.file);
        SDL_Texture* tex = covers.get(track.file);
        if (tex != nullptr) {
            dl->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(cx, cy), ImVec2(cx + cover, cy + cover), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
        }
        else {
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cover, cy + cover), col(palette::overlay), 6.0f);
            icon::note(dl, cx + cover / 2, cy + cover / 2, 20.0f, col(palette::muted));
        }

        // title + artist
        float tx = cx + cover + 12;
        float rightSpace = isCurrent ? 44.0f : 16.0f;
        float tw = row.x + row.w - rightSpace - tx;
        drawText(dl, fontSemiBold, 16.0f, tx, row.y + 13, col(isCurrent ? palette::bright : palette::text), fitText(fontSemiBold, 16.0f, track.title, tw).c_str());
        drawText(dl, fontRegular, 14.0f, tx, row.y + 35, col(palette::subtle), fitText(fontRegular, 14.0f, track.artist, tw).c_str());
        if (isCurrent) icon::equalizer(dl, row.x + row.w - 28, row.y + rowHeight / 2, 16.0f, col(palette::crimson), playing);

        // thin line between rows
        if (k + 1 < static_cast<int>(shown.size())) {
            dl->AddLine(ImVec2(tx, row.y + rowHeight - 0.5f), ImVec2(row.x + row.w, row.y + rowHeight - 0.5f), col(palette::overlay), 1.0f);
        }

        if (input && tapped(row) && a.contains(touch.x, touch.y)) {
            if (isCurrent) sheetOpen = true;     // tapping the playing song opens the player
            else playback::playIndex(player, library, index);
        }
    }
    dl->PopClipRect();

    // small scroll indicator while moving
    if (maxScroll > 0.0f && (scrollTouch || scrollVelocity != 0.0f)) {
        float barH = std::max(40.0f, a.h * a.h / contentHeight);
        float barY = a.y + (a.h - barH) * (scroll / maxScroll);
        dl->AddRectFilled(ImVec2(a.x + a.w - 5, barY), ImVec2(a.x + a.w - 2, barY + barH), col(palette::muted, 0.8f), 2.0f);
    }
}

void UI::drawMiniPlayer(Player& player, Library& library, Area a, bool input) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Track& track = library.song(library.currentIndex());

    // soft shadow + card
    for (int i = 1; i <= 6; i++) {
        float g = i * 2.0f;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g + 4), ImVec2(a.x + a.w + g, a.y + a.h + g + 4), col(0x000000, 0.06f), 14.0f + g);
    }
    dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + a.w, a.y + a.h), col(palette::overlay), 14.0f);

    // progress line along the bottom edge
    float length = player.length();
    float fraction = length > 0.0f ? std::clamp(player.position() / length, 0.0f, 1.0f) : 0.0f;
    float lineY = a.y + a.h - 3;
    dl->AddRectFilled(ImVec2(a.x + 14, lineY), ImVec2(a.x + a.w - 14, lineY + 2), col(palette::muted, 0.5f), 1.0f);
    dl->AddRectFilled(ImVec2(a.x + 14, lineY), ImVec2(a.x + 14 + (a.w - 28) * fraction, lineY + 2), col(palette::crimson), 1.0f);

    // cover
    const float cover = 44.0f;
    float cx = a.x + 10;
    float cy = a.y + (a.h - cover) / 2 - 1;
    covers.request(track.file);
    SDL_Texture* tex = covers.get(track.file);
    if (tex != nullptr) {
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(cx, cy), ImVec2(cx + cover, cy + cover), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
    }
    else {
        dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cover, cy + cover), col(palette::surface), 8.0f);
        icon::note(dl, cx + cover / 2, cy + cover / 2, 18.0f, col(palette::muted));
    }

    // buttons on the right
    float nextX = a.x + a.w - 30;
    float playX = nextX - 50;
    float midY = a.y + a.h / 2 - 1;
    Area playButton{ playX - 24, a.y, 48, a.h };
    Area nextButton{ nextX - 24, a.y, 48, a.h };
    bool playHeld = input && touch.down && !touch.moved && playButton.contains(touch.startX, touch.startY);
    if (playHeld) dl->AddCircleFilled(ImVec2(playX, midY), 22.0f, col(palette::text, 0.08f));
    if (player.isPlaying()) icon::pause(dl, playX, midY, 26.0f, col(palette::text));
    else icon::play(dl, playX, midY, 26.0f, col(palette::text));
    icon::next(dl, nextX, midY, 22.0f, col(palette::text));

    // title + artist
    float tx = cx + cover + 12;
    float tw = playX - 28 - tx;
    drawText(dl, fontSemiBold, 15.0f, tx, a.y + 14, col(palette::text), fitText(fontSemiBold, 15.0f, track.title, tw).c_str());
    drawText(dl, fontRegular, 13.0f, tx, a.y + 34, col(palette::subtle), fitText(fontRegular, 13.0f, track.artist, tw).c_str());

    if (!input) return;
    if (tapped(playButton)) playback::toggle(player, library);
    else if (tapped(nextButton)) playback::next(player, library);
    else if (tapped(a)) sheetOpen = true;
    // swipe up on the mini player also opens the player
    else if (touch.down && touch.moved && a.contains(touch.startX, touch.startY) && touch.startY - touch.y > 30.0f) sheetOpen = true;
}

void UI::drawToast(Area above) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float alpha = std::clamp(toastTime / 0.4f, 0.0f, 1.0f);
    float w = textWidth(fontSemiBold, 15.0f, toast.c_str()) + 40;
    float x = above.x + (above.w - w) / 2;
    dl->AddRectFilled(ImVec2(x, above.y), ImVec2(x + w, above.y + above.h), col(palette::crimson, 0.95f * alpha), above.h / 2);
    drawText(dl, fontSemiBold, 15.0f, x + 20, above.y + (above.h - 15) / 2 - 1, col(palette::text, alpha), toast.c_str());
}

// ------------------------------------------------------------------ now playing screen
void UI::drawNowPlaying(Player& player, Library& library, Area screen) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    platform::Insets insets = platform::safeArea(window);
    bool input = sheetOpen && sheetSlide == 1.0f;

    dl->AddRectFilled(ImVec2(screen.x, screen.y), ImVec2(screen.x + screen.w, screen.y + screen.h + sheetDrag + 50), col(palette::base));
    if (library.empty()) return;
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);

    const float side = 28.0f;
    float x = screen.x + insets.left + side;
    float w = screen.w - insets.left - insets.right - side * 2;
    float y = screen.y + insets.top;
    float bottom = screen.y + screen.h - insets.bottom;

    // --- top bar: close button + label
    float barCy = y + 24;
    icon::chevronDown(dl, x + 6, barCy, 26.0f, col(palette::text));
    Area closeButton{ screen.x, y, 90, 48 };
    const char* label = "NOW PLAYING";
    float spacing = 3.0f;
    float labelW = -spacing;
    for (const char* c = label; *c; c++) { char s[2] = { *c, 0 }; labelW += textWidth(fontSemiBold, 12.0f, s) + spacing; }
    float lx = screen.x + (screen.w - labelW) / 2;
    for (const char* c = label; *c; c++) {
        char s[2] = { *c, 0 };
        drawText(dl, fontSemiBold, 12.0f, lx, barCy - 7, col(palette::subtle), s);
        lx += textWidth(fontSemiBold, 12.0f, s) + spacing;
    }
    y += 48 + 8;

    // --- sizes: the cover takes what's left after the fixed parts
    const float titleH = 66, progressH = 56, controlsH = 92, minLyrics = 120;
    float cover = std::clamp(bottom - y - titleH - progressH - controlsH - minLyrics - 24, 140.0f, w);
    float coverX = x + (w - cover) / 2;
    Area coverArea{ coverX, y, cover, cover };
    // Order from top to bottom: cover, lyrics, title, time bar, buttons.
    // The lyrics get whatever height is left over.
    const float lyricsGap = 14, titleGap = 10;
    float lyricsTop = y + cover + lyricsGap;
    float lyricsH = bottom - 8 - lyricsTop - titleGap - titleH - progressH - controlsH;
    float titleTop = lyricsTop + lyricsH + titleGap;
    float progressTop = titleTop + titleH;

    // --- gestures, decided once the finger has moved far enough:
    //   sideways on the cover            -> next / previous song
    //   downwards, above the time bar    -> pull the screen closed
    if (input && touch.pressed) gesture = SheetGesture::None;
    if (input && touch.down && touch.moved && gesture == SheetGesture::None && !seeking) {
        float dx = touch.x - touch.startX;
        float dy = touch.y - touch.startY;
        if (coverArea.contains(touch.startX, touch.startY) && std::fabs(dx) > std::fabs(dy)) gesture = SheetGesture::CoverSwipe;
        else if (touch.startY < progressTop - 4 && dy > 0) gesture = SheetGesture::PullDown;
        else gesture = SheetGesture::Ignored;
    }
    if (gesture == SheetGesture::CoverSwipe) {
        float dx = touch.x - touch.startX;
        if (touch.down) coverOffset = dx;          // the cover follows the finger
        else {
            float needed = std::min(90.0f, cover * 0.25f);
            if (dx < -needed) {                    // swiped left: next song,
                playback::next(player, library);   // which slides in from the right
                coverOffset = cover + 40;
            }
            else if (dx > needed) {                // swiped right: previous song
                playback::previousTrack(player, library);
                coverOffset = -(cover + 40);
            }
            gesture = SheetGesture::None;          // not far enough: it springs back
        }
    }
    if (gesture != SheetGesture::CoverSwipe) {
        coverOffset += (0.0f - coverOffset) * std::min(1.0f, dt * 14.0f);
        if (std::fabs(coverOffset) < 0.5f) coverOffset = 0.0f;
    }
    if (input && tapped(coverArea)) {
        playback::toggle(player, library);
        coverFlash = 0.7f;
    }

    // (read the song only now, because a swipe may just have changed it)
    const Track& track = library.song(library.currentIndex());
    // Only keep the big cover of the current song in memory.
    if (track.file != sheetSong) {
        bigCovers.forgetAllExcept(track.file);
        sheetSong = track.file;
    }

    float fade = 1.0f - 0.6f * std::min(1.0f, std::fabs(coverOffset) / cover);
    drawNpCover(Area{ coverX + coverOffset, y, cover, cover }, track, fade);

    // after a tap: briefly show what happened (playing or paused)
    if (coverFlash > 0.0f) {
        coverFlash -= dt;
        float a = std::clamp(coverFlash / 0.3f, 0.0f, 1.0f);
        float cx = coverX + cover / 2;
        float cy = y + cover / 2;
        dl->AddCircleFilled(ImVec2(cx, cy), 44.0f, col(0x000000, 0.5f * a));
        if (player.isPlaying()) icon::play(dl, cx + 3, cy, 40.0f, col(palette::text, a));
        else icon::pause(dl, cx, cy, 40.0f, col(palette::text, a));
    }

    // --- lyrics, right under the cover
    drawNpLyrics(player, track, Area{ screen.x, lyricsTop, screen.w, lyricsH });

    // --- title + artist
    drawText(dl, fontBold, 24.0f, x, titleTop, col(palette::text), fitText(fontBold, 24.0f, track.title, w).c_str());
    drawText(dl, fontRegular, 17.0f, x, titleTop + 32, col(palette::subtle), fitText(fontRegular, 17.0f, track.artist, w).c_str());

    Area progress{ x, progressTop, w, progressH };
    drawNpProgress(player, progress, input);

    drawNpControls(player, library, Area{ x, progressTop + progressH, w, controlsH }, input);

    // --- closing: tap the chevron, or pull the screen down
    if (!input) return;
    if (tapped(closeButton)) sheetOpen = false;
    if (gesture == SheetGesture::PullDown) {
        if (touch.down) sheetDrag = std::max(0.0f, touch.y - touch.startY);
        else {
            if (sheetDrag > 120.0f) {
                sheetOpen = false;
                // continue the slide from where the finger let go
                sheetSlide = std::clamp(1.0f - sheetDrag / screen.h, 0.0f, 1.0f);
            }
            sheetDrag = 0.0f;
            gesture = SheetGesture::None;
        }
    }
    if (!touch.down && gesture == SheetGesture::Ignored) gesture = SheetGesture::None;
}

void UI::drawNpCover(Area a, const Track& track, float alpha) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // crimson glow behind the cover
    for (int i = 12; i >= 1; i--) {
        float g = i * 2.5f;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g + 6), ImVec2(a.x + a.w + g, a.y + a.h + g + 6), col(palette::crimson, 0.022f * alpha), 16.0f + g);
    }
    bigCovers.request(track.file);
    SDL_Texture* tex = bigCovers.get(track.file);
    if (tex != nullptr) {
        ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(a.x, a.y), ImVec2(a.x + a.w, a.y + a.h), ImVec2(0, 0), ImVec2(1, 1), tint, 16.0f);
    }
    else {
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + a.w, a.y + a.h), col(palette::surface, alpha), 16.0f);
        icon::note(dl, a.x + a.w / 2, a.y + a.h / 2, a.w * 0.3f, col(palette::muted, alpha));
    }
}

void UI::drawNpProgress(Player& player, Area a, bool input) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float length = player.length();
    float fraction = length > 0.0f ? std::clamp(player.position() / length, 0.0f, 1.0f) : 0.0f;
    float lineY = a.y + 10;
    Area hit{ a.x - 12, a.y - 10, a.w + 24, 40 };

    // Drag the knob; the song only jumps when you let go.
    if (input && touch.pressed && hit.contains(touch.x, touch.y) && length > 0.0f) seeking = true;
    if (seeking) {
        seekFraction = std::clamp((touch.x - a.x) / a.w, 0.0f, 1.0f);
        if (!touch.down) {
            player.seek(seekFraction * length);
            seeking = false;
        }
        fraction = seekFraction;
    }

    float knobX = a.x + fraction * a.w;
    float thickness = seeking ? 6.0f : 4.0f;
    dl->AddRectFilled(ImVec2(a.x, lineY - thickness / 2), ImVec2(a.x + a.w, lineY + thickness / 2), col(palette::text, 0.18f), thickness / 2);
    dl->AddRectFilled(ImVec2(a.x, lineY - thickness / 2), ImVec2(knobX, lineY + thickness / 2), col(palette::crimson), thickness / 2);
    dl->AddCircleFilled(ImVec2(knobX, lineY), seeking ? 10.0f : 7.0f, col(palette::text));

    char buffer[16];
    formatTime(fraction * length, buffer, sizeof(buffer));
    drawText(dl, fontRegular, 13.0f, a.x, a.y + 24, col(palette::subtle), buffer);
    char remaining[17];
    formatTime(length - fraction * length, buffer, sizeof(buffer));
    std::snprintf(remaining, sizeof(remaining), "-%s", buffer);
    drawText(dl, fontRegular, 13.0f, a.x + a.w - textWidth(fontRegular, 13.0f, remaining), a.y + 24, col(palette::subtle), remaining);
}

void UI::drawNpControls(Player& player, Library& library, Area a, bool input) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = a.y + a.h / 2 - 6;
    float mid = a.x + a.w / 2;
    ImU32 white = col(palette::text);
    ImU32 crimson = col(palette::crimson);

    auto held = [&](const Area& area) { return input && touch.down && !touch.moved && area.contains(touch.startX, touch.startY); };

    // shuffle
    Area shuffleButton{ a.x - 12, cy - 26, 48, 52 };
    icon::shuffle(dl, a.x + 12, cy, 22.0f, library.shuffled() ? crimson : col(palette::subtle));
    if (library.shuffled()) dl->AddCircleFilled(ImVec2(a.x + 12, cy + 18), 2.0f, crimson);

    // previous / next
    float skip = std::min(84.0f, a.w * 0.24f);
    Area prevButton{ mid - skip - 28, cy - 28, 56, 56 };
    Area nextButton{ mid + skip - 28, cy - 28, 56, 56 };
    if (held(prevButton)) dl->AddCircleFilled(ImVec2(mid - skip, cy), 26.0f, col(palette::text, 0.08f));
    if (held(nextButton)) dl->AddCircleFilled(ImVec2(mid + skip, cy), 26.0f, col(palette::text, 0.08f));
    icon::previous(dl, mid - skip, cy, 30.0f, white);
    icon::next(dl, mid + skip, cy, 30.0f, white);

    // big play button
    float r = 36.0f;
    Area playButton{ mid - r, cy - r, r * 2, r * 2 };
    float pr = held(playButton) ? r * 0.94f : r;
    dl->AddCircleFilled(ImVec2(mid, cy + 3), pr + 4, col(palette::blood, 0.5f));
    dl->AddCircleFilled(ImVec2(mid, cy), pr, crimson);
    if (player.isPlaying()) icon::pause(dl, mid, cy, 30.0f, white);
    else icon::play(dl, mid + 2, cy, 30.0f, white);

    // repeat (crimson = on, with a "1" when repeating one song)
    RepeatMode mode = library.repeatMode();
    float rx = a.x + a.w - 12;
    Area repeatButton{ rx - 24, cy - 26, 48, 52 };
    icon::repeat(dl, rx, cy, 22.0f, mode == RepeatMode::Off ? col(palette::subtle) : crimson);
    if (mode != RepeatMode::Off) dl->AddCircleFilled(ImVec2(rx, cy + 18), 2.0f, crimson);
    if (mode == RepeatMode::One) {
        dl->AddCircleFilled(ImVec2(rx + 10, cy - 10), 7.0f, crimson);
        drawText(dl, fontBold, 10.0f, rx + 10 - textWidth(fontBold, 10.0f, "1") / 2, cy - 16, white, "1");
    }

    if (!input) return;
    if (tapped(shuffleButton)) library.setShuffle(!library.shuffled());
    if (tapped(prevButton)) playback::previous(player, library);
    if (tapped(playButton)) playback::toggle(player, library);
    if (tapped(nextButton)) playback::next(player, library);
    if (tapped(repeatButton)) {
        if (mode == RepeatMode::All) library.setRepeatMode(RepeatMode::One);
        else if (mode == RepeatMode::One) library.setRepeatMode(RepeatMode::Off);
        else library.setRepeatMode(RepeatMode::All);
    }
}

void UI::drawNpLyrics(Player& player, const Track& track, Area a) {
    if (a.h < 40.0f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::vector<LyricLine>& lines = lyrics.linesFor(track.file, track.artist, track.title);
    bool transcribed = lyrics.isTranscribed(track.file);
    bool current = player.loaded() && player.file() == track.file;
    float position = current ? player.position() : 0.0f;

    // the glowing crimson lines above and below, like on desktop
    float fadeLeft = a.x + a.w * 0.1f;
    float fadeRight = a.x + a.w * 0.9f;
    float middle = (fadeLeft + fadeRight) / 2;
    ImU32 clear = col(palette::crimson, 0.0f);
    ImU32 glow = col(palette::crimson, 0.65f);
    for (float ly : { a.y + 2, a.y + a.h - 3 }) {
        dl->AddRectFilledMultiColor(ImVec2(fadeLeft, ly), ImVec2(middle, ly + 1.5f), clear, glow, glow, clear);
        dl->AddRectFilledMultiColor(ImVec2(middle, ly), ImVec2(fadeRight, ly + 1.5f), glow, clear, clear, glow);
    }

    if (lines.empty()) {
        const char* none = "No lyrics for this song";
        drawText(dl, fontRegular, 15.0f, a.x + (a.w - textWidth(fontRegular, 15.0f, none)) / 2, a.y + a.h / 2 - 9, col(palette::muted), none);
        return;
    }

    int now = -1;
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (lines[i].time > position) break;
        now = static_cast<int>(i);
    }
    if (track.file != lyricSong) {
        lyricSong = track.file;
        lyricScroll = static_cast<float>(now);
    }
    float target = static_cast<float>(now);
    lyricScroll += (target - lyricScroll) * std::min(1.0f, ImGui::GetIO().DeltaTime * 10.0f);
    if (std::fabs(target - lyricScroll) < 0.001f) lyricScroll = target;

    float centreY = a.y + a.h / 2;
    float spacing = std::clamp((a.h - 12) / 3.0f, 30.0f, 44.0f);
    ImVec4 pink = transcribed ? hexColor(palette::subtle) : hexColor(palette::pink);
    ImVec4 grey = hexColor(palette::subtle);
    float dim = transcribed ? 0.7f : 1.0f;
    float maxWidth = a.w - 40;

    dl->PushClipRect(ImVec2(a.x, a.y + 4), ImVec2(a.x + a.w, a.y + a.h - 3), true);
    int first = static_cast<int>(std::floor(lyricScroll)) - 3;
    for (int i = first; i <= first + 7; i++) {
        if (i < 0 || static_cast<std::size_t>(i) >= lines.size()) continue;
        float offset = i - lyricScroll;
        float d = std::min(std::fabs(offset), 2.5f);
        float closeness = std::max(0.0f, 1.0f - d);
        float size = 16.0f + 6.0f * closeness;
        ImFont* font = closeness > 0.5f ? fontBold : fontRegular;
        float alpha = (d <= 1.0f ? 0.55f + 0.45f * closeness : 0.55f * std::max(0.0f, 2.5f - d) / 1.5f) * dim;
        ImVec4 c(grey.x + (pink.x - grey.x) * closeness, grey.y + (pink.y - grey.y) * closeness, grey.z + (pink.z - grey.z) * closeness, alpha);
        // long lines first get a bit smaller, and only then cut off with "..."
        const std::string& full = lines[static_cast<std::size_t>(i)].text;
        float fullWidth = textWidth(font, size, full.c_str());
        if (fullWidth > maxWidth) size = std::max(size * 0.75f, size * maxWidth / fullWidth);
        std::string text = fitText(font, size, full, maxWidth);
        if (text.empty()) continue;
        float ly = centreY + offset * spacing - size / 2;
        drawText(dl, font, size, a.x + (a.w - textWidth(font, size, text.c_str())) / 2, ly, ImGui::GetColorU32(c), text.c_str());
    }
    dl->PopClipRect();
}
