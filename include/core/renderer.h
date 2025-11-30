#ifndef RENDERER_H
#define RENDERER_H

#include "camera.h"
#include "colour.h"
#include "hittable.h"

#include <vector>
#include <atomic>
#include <cstdint>

// Returned by renderer::render()
struct render_result {
    int width  = 0;
    int height = 0;

    // 8-bit RGB per pixel
    std::vector<std::uint8_t> pixels;
};

// Progress state shared between renderer and UI
struct render_progress_state {
    int total_scanlines = 0;
    std::atomic<int>    completed_scanlines{0};
    std::atomic<double> elapsed_seconds{0.0};
    std::atomic<double> eta_seconds{0.0};
};

class renderer {
public:
    renderer() = default;

    // OLD signature kept for console main, etc.
    render_result render(
        const hittable&   world,
        camera&           cam,
        std::atomic<bool>* cancel_flag = nullptr
    ) const;

    // NEW signature with progress reporting (used by editor_main.cpp)
    render_result render(
        const hittable&        world,
        camera&                cam,
        std::atomic<bool>*     cancel_flag,
        render_progress_state* progress
    ) const;
};

#endif // RENDERER_H
