// Lock screen + Control Center support.
//  - MPNowPlayingInfoCenter: what is shown (title, artist, cover, time bar).
//  - MPRemoteCommandCenter:  the buttons (play/pause, next, previous, seeking).
// iOS calls the button handlers on the main thread, but we only queue the
// commands there and run them in poll(), from the normal main loop.
#include "NowPlaying.hpp"
#include "CoverCache.hpp"
#include "Library.hpp"
#include "Playback.hpp"
#include "Player.hpp"
#import <AVFoundation/AVFoundation.h>
#import <MediaPlayer/MediaPlayer.h>
#import <UIKit/UIKit.h>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

namespace {
    enum class CommandType { Play, Pause, Toggle, Next, Previous, Seek };
    struct Command {
        CommandType type;
        double seconds = 0.0;
    };
}

struct NowPlaying::Impl {
    std::mutex mutex;
    std::vector<Command> commands;
    std::vector<id> handlers;       // so we can unregister them again
    id routeObserver = nil;

    std::filesystem::path shownFile;
    bool shownPlaying = false;
    float shownPosition = 0.0f;
    std::chrono::steady_clock::time_point shownAt;

    void push(Command command) {
        std::lock_guard<std::mutex> lock(mutex);
        commands.push_back(command);
    }
};

NowPlaying::NowPlaying() : impl(std::make_unique<Impl>()) {
    Impl* state = impl.get();
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];

    auto add = [state](MPRemoteCommand* command, CommandType type) {
        command.enabled = YES;
        id handler = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            state->push(Command{type});
            return MPRemoteCommandHandlerStatusSuccess;
        }];
        state->handlers.push_back(handler);
    };
    add(center.playCommand, CommandType::Play);
    add(center.pauseCommand, CommandType::Pause);
    add(center.togglePlayPauseCommand, CommandType::Toggle);
    add(center.nextTrackCommand, CommandType::Next);
    add(center.previousTrackCommand, CommandType::Previous);

    // Dragging the time bar on the lock screen.
    center.changePlaybackPositionCommand.enabled = YES;
    id seekHandler = [center.changePlaybackPositionCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        auto* seek = (MPChangePlaybackPositionCommandEvent*)event;
        state->push(Command{CommandType::Seek, seek.positionTime});
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    state->handlers.push_back(seekHandler);

    // Pause when headphones are unplugged / Bluetooth disconnects, like every
    // other music app (otherwise it would suddenly play out of the speaker).
    state->routeObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:nil
                usingBlock:^(NSNotification* note) {
                    NSNumber* reason = note.userInfo[AVAudioSessionRouteChangeReasonKey];
                    if (reason.unsignedIntegerValue == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
                        state->push(Command{CommandType::Pause});
                    }
                }];

    [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
}

NowPlaying::~NowPlaying() {
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    NSArray<MPRemoteCommand*>* commands = @[center.playCommand, center.pauseCommand, center.togglePlayPauseCommand,
                                            center.nextTrackCommand, center.previousTrackCommand,
                                            center.changePlaybackPositionCommand];
    for (MPRemoteCommand* command in commands) [command removeTarget:nil];
    if (impl->routeObserver != nil) [[NSNotificationCenter defaultCenter] removeObserver:impl->routeObserver];
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
}

static NSString* toNSString(const std::string& text) {
    NSString* result = [NSString stringWithUTF8String:text.c_str()];
    return result != nil ? result : @"";
}

void NowPlaying::poll(Player& player, Library& library) {
    // 1. Run the buttons that were pressed since the last loop.
    std::vector<Command> todo;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        todo.swap(impl->commands);
    }
    for (const Command& command : todo) {
        switch (command.type) {
            case CommandType::Play:     playback::resume(player, library); break;
            case CommandType::Pause:    player.pause(); break;
            case CommandType::Toggle:   playback::toggle(player, library); break;
            case CommandType::Next:     playback::next(player, library); break;
            case CommandType::Previous: playback::previous(player, library); break;
            case CommandType::Seek:     player.seek(static_cast<float>(command.seconds)); break;
        }
    }

    // 2. Keep the lock-screen info in sync.
    MPNowPlayingInfoCenter* info = [MPNowPlayingInfoCenter defaultCenter];
    if (!player.loaded()) {
        if (!impl->shownFile.empty()) {
            info.nowPlayingInfo = nil;
            impl->shownFile.clear();
        }
        return;
    }

    bool playing = player.isPlaying();
    float position = player.position();
    auto now = std::chrono::steady_clock::now();

    if (player.file() != impl->shownFile) {
        // New song: send everything, including the cover.
        std::string title = player.file().stem().string();
        std::string artist;
        if (!library.empty() && library.currentSong() == player.file()) {
            const Track& track = library.song(library.currentIndex());
            title = track.title;
            artist = track.artist;
        }

        NSMutableDictionary* fields = [NSMutableDictionary dictionary];
        fields[MPMediaItemPropertyTitle] = toNSString(title);
        fields[MPMediaItemPropertyArtist] = toNSString(artist);
        fields[MPMediaItemPropertyPlaybackDuration] = @(player.length());
        fields[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(position);
        fields[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
        fields[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);

        std::vector<unsigned char> bytes;
        if (CoverCache::rawCover(player.file(), bytes)) {
            UIImage* image = [UIImage imageWithData:[NSData dataWithBytes:bytes.data() length:bytes.size()]];
            if (image != nil) {
                fields[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                    requestHandler:^UIImage*(CGSize size) { return image; }];
            }
        }
        info.nowPlayingInfo = fields;
    }
    else {
        // Same song: only update when play/pause changed or the position
        // jumped (seeking). In between, iOS moves the time bar by itself.
        float seconds = std::chrono::duration<float>(now - impl->shownAt).count();
        float expected = impl->shownPosition + (impl->shownPlaying ? seconds : 0.0f);
        if (playing == impl->shownPlaying && std::fabs(position - expected) < 1.5f) return;

        NSMutableDictionary* fields = [info.nowPlayingInfo mutableCopy];
        if (fields == nil) fields = [NSMutableDictionary dictionary];
        fields[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(position);
        fields[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
        info.nowPlayingInfo = fields;
    }

    impl->shownFile = player.file();
    impl->shownPlaying = playing;
    impl->shownPosition = position;
    impl->shownAt = now;
}
