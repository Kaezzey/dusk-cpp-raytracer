#ifndef RENDERER_H
#define RENDERER_H

#include "camera.h"
#include "colour.h"
#include "hittable.h"
#include <vector>
#include <atomic>

// Returned by renderer::render()
struct render_result {
    int width  = 0;
    int height = 0;

    // float RGB per pixel (your existing colour/vec3 type)
    std::vector<uint8_t> pixels;
};

class renderer {
public:
    renderer() = default;

    // High-level render call used by the EDITOR.
    // Does NOT write PPM. Does NOT do ETA. Pure pixel buffer generator.
    render_result render(
        const hittable& world,
        camera& cam,
        std::atomic<bool>* cancel_flag = nullptr
    ) const;
};

#endif
