#ifndef SCENE_H
#define SCENE_H

#include "hittable.h"
#include "hittable_list.h"
#include "material.h"
#include "texture.h"
#include <memory>

class scene {
public:
    std::shared_ptr<hittable> world;   // usually a hittable_list or BVH root
    colour background = colour(0.70, 0.80, 1.00);

    scene() = default;

    scene(std::shared_ptr<hittable> world_in,
          const colour& background_in = colour(0.70, 0.80, 1.00))
        : world(std::move(world_in)), background(background_in) {}
};

#endif
