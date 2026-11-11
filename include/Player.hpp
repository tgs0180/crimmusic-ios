#pragma once
#include <filesystem>
#include "miniaudio.h"

class Player {
    public: 
        Player() = default;
        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        bool init();
        bool play(const std::filesystem::path&file);
        void togglePause();
        void pause();
        void resume();
        bool isFinished();
        bool isPlaying();
        bool loaded() const;
        const std::filesystem::path& file() const;
        float position();
        float length();
        void seek(float seconds);
        void stop();
        ~Player();

    private:
        ma_context context;
        ma_engine engine;
        ma_sound sound;
        bool contextReady = false;
        bool engineReady = false;
        bool soundLoaded = false;
        std::filesystem::path current;
};
