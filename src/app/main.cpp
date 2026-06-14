#include <exception>
#include <iostream>

#include <SDL3/SDL_main.h>

#include "core/asset_manifest.h"
#include "core/indexed_framebuffer.h"
#include "platform_sdl3/sdl_app.h"

int main(int, char*[]) {
    try {
        const auto verification =
            bumpy::AssetManifest::load("config/original-assets.sha256").verify(".");
        if (!verification.missing.empty() || !verification.changed.empty()) {
            std::cerr << "Original Bumpy assets are missing or changed\n";
            return 2;
        }

        bumpy::IndexedFramebuffer frame(320, 200);
        frame.set_palette(0, {0, 0, 0, 255});
        frame.set_palette(1, {255, 255, 255, 255});
        for (int x = 0; x < frame.width(); ++x) {
            frame.pixel(x, 0) = 1;
            frame.pixel(x, frame.height() - 1) = 1;
        }
        for (int y = 0; y < frame.height(); ++y) {
            frame.pixel(0, y) = 1;
            frame.pixel(frame.width() - 1, y) = 1;
        }

        bumpy::SdlApp app;
        return app.run(frame);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
