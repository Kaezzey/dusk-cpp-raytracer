#ifndef HITTABLE_LIST_H
#define HITTABLE_LIST_H

#include "aabb.h"
#include "hittable.h"

#include <vector>

class hittable_list : public hittable {

    public:
        //vector of shared pointers to hittable objects
        std::vector<shared_ptr<hittable>> objects;
        hittable_list() {}

        //constructor with single object
        hittable_list(shared_ptr<hittable> object) { add(object); }

        //clear the list
        void clear() { objects.clear(); }

        //add object to the list within world
        void add(shared_ptr<hittable> object) { 
            objects.push_back(object);
            bbox = aabb(bbox, object->bounding_box());
        }

        //override hit function to check for hits against all objects in the list
        bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
            hit_record temp_rec;
            bool hit_anything = false;
            auto closest_so_far = ray_t.max;

            //check each object for a hit
            for (const auto& object : objects) {

                //if hit, update hit record and closest_so_far
                if (object->hit(r, interval(ray_t.min, closest_so_far), temp_rec)) {
                    hit_anything = true;
                    closest_so_far = temp_rec.t;
                    rec = temp_rec;
                }
            }

            //return whether we hit anything
            return hit_anything;
        }

        aabb bounding_box() const override {
            return bbox;
        }

    private:
        aabb bbox;
};

#endif