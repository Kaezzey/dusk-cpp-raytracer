#include "../../include/core/dusktracer.h"

#include "../../include/core/renderer.h"
#include "../../include/core/interval.h"

#include <cmath>

render_result renderer::render(
    const hittable& world,
    camera& cam,
    std::atomic<bool>* cancel_flag
) const
{
    // Use camera's existing setup code
    cam.initialize();

    render_result out;
    out.width  = cam.image_width;
    out.height = cam.image_height;
    out.pixels.resize(out.width * out.height * 3);

    const int width  = out.width;
    const int height = out.height;
    const int spp    = cam.samples_per_pixel;
    const int depth  = cam.max_depth;

    // Same parallelisation strategy the camera uses
    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < height; ++j) {

        if (cancel_flag && *cancel_flag)
            continue;

        for (int i = 0; i < width; ++i) {
            colour pixel_col(0,0,0);

            for (int s = 0; s < spp; ++s) {
                ray r = cam.get_ray(i, j, s);
                pixel_col += cam.ray_colour(r, depth, world);
            }

            // Average
            double scale = 1.0 / spp;
            double r_lin = scale * pixel_col.x();
            double g_lin = scale * pixel_col.y();
            double b_lin = scale * pixel_col.z();

            // sRGB gamma (uses your linear_to_gamma)
            double r_gam = linear_to_gamma(r_lin);
            double g_gam = linear_to_gamma(g_lin);
            double b_gam = linear_to_gamma(b_lin);

            static const interval intensity(0.000, 0.999);
            int rbyte = int(256 * intensity.clamp(r_gam));
            int gbyte = int(256 * intensity.clamp(g_gam));
            int bbyte = int(256 * intensity.clamp(b_gam));

            int idx = 3 * (j * width + i);
            out.pixels[idx + 0] = static_cast<unsigned char>(rbyte);
            out.pixels[idx + 1] = static_cast<unsigned char>(gbyte);
            out.pixels[idx + 2] = static_cast<unsigned char>(bbyte);
        }
    }

    return out;
}
