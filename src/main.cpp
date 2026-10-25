// Faust's Deck -- C++ / raylib edition.
//   FaustsDeck                      normal play
//   FaustsDeck --seed 42            reproducible shuffles
//   FaustsDeck --script file.txt    drive the UI from a script (used for automated screenshots/tests)
//   FaustsDeck --root DIR           folder containing data/, assets/, saves/ (default: next to the program)
//   FaustsDeck --size 1920x1080     window size in pixels (default: fits the monitor; F11 toggles borderless fullscreen)
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/app.hpp"

int main(int argc, char** argv) {
    fd::ui::Options opt;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--script") && i + 1 < argc) opt.script_path = argv[++i];
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) opt.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) opt.root_override = argv[++i];
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 640 && h >= 360) { opt.win_w = w; opt.win_h = h; }
            else { std::fprintf(stderr, "--size expects WIDTHxHEIGHT, for example 1920x1080 (minimum 640x360)\n"); return 1; }
        }
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("Faust's Deck (C++/raylib)\n  --seed N        reproducible shuffles\n  --script FILE   drive the UI from a script\n  --root DIR      folder with data/, assets/, saves/\n  --size WxH      window size in pixels, e.g. 1920x1080\n  F11             toggle borderless fullscreen\n");
            return 0;
        }
    }
    fd::ui::App app(opt);
    if (!app.ok()) return 1;
    return app.run();
}
