#include "../../include/core/dusktracer.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/sphere.h"
#include "../../include/core/camera.h"

int main(){
    hittable_list world;

    world.add(make_shared<sphere>(point3(0,0,-1), 0.5));
    world.add(make_shared<sphere>(point3(0,-100.5,-1), 100));

    camera cam;

    cam.aspect_ratio = 16.0 / 9.0;
    cam.image_width  = 400;
    cam.samples_per_pixel = 100;

    cam.render(world);
}