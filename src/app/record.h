// Demo recorder: renders the real HUD off-screen on a virtual clock, runs a
// scripted conversation through the real assistant and pipes the frames to
// ffmpeg. Used to make the promo video (see video/).
//
//   kayra.exe --record out.mp4 --script demo_tr.json --ffmpeg ffmpeg.exe
#pragma once
#include <string>
#include <vector>

namespace kayra {

int run_record(const std::vector<std::string>& args);

}  // namespace kayra
