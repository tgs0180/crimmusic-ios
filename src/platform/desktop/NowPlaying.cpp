// The desktop test build has no lock screen, so this does nothing.
#include "NowPlaying.hpp"

struct NowPlaying::Impl {};

NowPlaying::NowPlaying() = default;
NowPlaying::~NowPlaying() = default;
void NowPlaying::poll(Player&, Library&) {}
