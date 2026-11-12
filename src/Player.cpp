#include "Player.hpp"
#include <iostream>


bool Player::init() {
    // On iPhone the audio session needs the "playback" category. That makes
    // music keep playing when the screen locks and ignore the silent switch.
    // (miniaudio would otherwise pick "play and record".)
    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.coreaudio.sessionCategory = ma_ios_session_category_playback;
    if (ma_context_init(NULL, 0, &contextConfig, &context) != MA_SUCCESS) {
        std::cerr << "ERROR: failed to start audio context" << "\n";
        return false;
    }
    contextReady = true;

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pContext = &context;
    if (ma_engine_init(&engineConfig, &engine) != MA_SUCCESS) {
        std::cerr << "ERROR: failed to start audio engine" << "\n";
        return false;
    }
    engineReady = true;
    return true;
}

bool Player::play(const std::filesystem::path &file) {
    if (soundLoaded) {
        ma_sound_uninit(&sound);
        soundLoaded = false;
    }
    current.clear();
    ma_result result = ma_sound_init_from_file(&engine, file.string().c_str(), MA_SOUND_FLAG_STREAM, NULL, NULL, &sound);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed loading a song" << "\n";    
        return false;
    }
    soundLoaded = true;
    current = file;
    // Another app (e.g. TikTok) can interrupt our audio. iOS then stops the
    // whole audio device, not just this song, so turn the device back on too.
    // If it is already running this does nothing.
    ma_engine_start(&engine);
    ma_sound_start(&sound);
    return true;
}

bool Player::isFinished() {
    return soundLoaded && ma_sound_at_end(&sound);
}

bool Player::isPlaying() {
    return soundLoaded && ma_sound_is_playing(&sound);
}

bool Player::loaded() const {
    return soundLoaded;
}

const std::filesystem::path& Player::file() const {
    return current;
}

float Player::position() {
    if (!soundLoaded) return 0.0f;
    float seconds = 0.0f;
    ma_sound_get_cursor_in_seconds(&sound, &seconds);
    return seconds;
}

float Player::length() {
    if (!soundLoaded) return 0.0f;
    float seconds = 0.0f;
    ma_sound_get_length_in_seconds(&sound, &seconds);
    return seconds;
}

void Player::seek(float seconds) {
    if (!soundLoaded) return;
    ma_sound_seek_to_second(&sound, seconds);
}

void Player::togglePause() {
    if (!soundLoaded) return;
    if (ma_sound_is_playing(&sound)) pause();
    else resume();
}

void Player::pause() {
    if (soundLoaded) ma_sound_stop(&sound);
}

void Player::resume() {
    if (!soundLoaded) return;
    ma_engine_start(&engine); // see play(): the device may have been stopped
    ma_sound_start(&sound);
}

void Player::stop() {
    if (soundLoaded) ma_sound_uninit(&sound);
    soundLoaded = false;
    current.clear();
}

Player::~Player() {
    if (soundLoaded) ma_sound_uninit(&sound);
    if (engineReady) ma_engine_uninit(&engine);
    if (contextReady) ma_context_uninit(&context);
}
