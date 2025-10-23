#include <iostream>

int main(){

    //image
    int image_width = 256;
    int image_height = 256;

    //render
    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    //pixel loop
    for(int i = 0; i < image_height; i++){

        for(int j = 0; j < image_width; j++){
            
            //normalized [0,1]
            auto r = double(j) / (image_width - 1);
            auto g = double(i) / (image_height - 1);
            auto b = 0.0;

            //gamma correction
            int ir = int(255.999 * r);
            int ig = int(255.999 * g);
            int ib = int(255.999 * b);

            std::cout << ir << ' ' << ig << ' ' << ib << '\n';
        }
    }

    return 0;
}