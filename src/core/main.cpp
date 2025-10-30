#include "../../include/core/dusktracer.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/sphere.h"


//returns colour based on ray direction
colour ray_colour(const ray& r, const hittable& world){

    hit_record rec;

    if(world.hit(r, 0, infinity, rec)){
        return 0.5 * (rec.normal + colour(1,1,1));
    }

    vec3 unit_direction = unit_vector(r.direction());
    auto a = 0.5 * (unit_direction.y() + 1.0);
    return (1.0 - a) * colour(1.0, 1.0, 1.0) + a * colour(0.5, 0.7, 1.0);
}


int main(){

    //IMAGE//
    auto aspect_ratio = 16.0 / 9.0;
    int image_width = 400;

    //image height based on ideal ratio and > 1
    int image_height = int(image_width / aspect_ratio);
    image_height = (image_height < 1) ? 1 : image_height;

    //WORLD//
    hittable_list world;

    world.add(make_shared<sphere>(point3(0,0,-1), 0.5));
    world.add(make_shared<sphere>(point3(0,-100.5,-1), 100));

    //CAMERA DETAILS//
    auto focal_length = 1.0;
    auto viewport_height = 2.0;
    auto viewport_width = viewport_height * (double(image_width)/image_height);
    auto camera_centre = point3(0,0,0);

    //viewport horizontal and vertical vectors (u (x),v (-y))
    auto viewport_u = vec3(viewport_width, 0, 0);
    auto viewport_v = vec3(0, -viewport_height, 0);

    //delta steps for u and v as vectors
    auto pixel_delta_u = viewport_u / image_width;
    auto pixel_delta_v = viewport_v / image_height;

    auto viewport_upper_left =
        camera_centre
        - vec3(0, 0, focal_length)
        - (viewport_u / 2)
        - (viewport_v / 2);
    
    auto pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

    //header
    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    //pixel loop
    for(int i = 0; i < image_height; i++){

        std::clog << "\rScanlines remaining: " << (image_height - i) << ' ' << std::flush;

        for(int j = 0; j < image_width; j++){
            
            //compute ray for pixel (i,j)
            auto pixel_centre = pixel00_loc + (j * pixel_delta_u) + (i * pixel_delta_v);

            //create ray from camera centre to pixel centre
            auto ray_direction = pixel_centre - camera_centre;
            ray r(camera_centre, ray_direction);
            
            //get colour for ray
            colour pixel_colour = ray_colour(r, world);

            //write colour to output
            write_colour(std::cout, pixel_colour);
        }
    }

    std::clog << "\nDone.\n";

    return 0;
}