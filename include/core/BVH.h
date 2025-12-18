#ifndef BVH_H
#define BVH_H

#include "aabb.h"
#include "hittable.h"
#include "hittable_list.h"
#include "packet.h"
#include "sphere.h"

#include <memory>

#include <algorithm>

class bvh_node : public hittable {
  public:
    bvh_node(hittable_list list) : bvh_node(list.objects, 0, list.objects.size()) {}

    bvh_node(std::vector<shared_ptr<hittable>>& objects, size_t start, size_t end) {

        // Build the bounding box of the span of source objects.
        bbox = aabb::empty;
        for (size_t object_index=start; object_index < end; object_index++)
            bbox = aabb(bbox, objects[object_index]->bounding_box());

        int axis = bbox.longest_axis();

        auto comparator = (axis == 0) ? box_x_compare
                        : (axis == 1) ? box_y_compare
                                      : box_z_compare;

        size_t object_span = end - start;

        if (object_span == 1) {
            left = right = objects[start];
        } else if (object_span == 2) {
            left = objects[start];
            right = objects[start+1];
        } else {
            std::sort(std::begin(objects) + start, std::begin(objects) + end, comparator);

            auto mid = start + object_span/2;
            left = make_shared<bvh_node>(objects, start, mid);
            right = make_shared<bvh_node>(objects, mid, end);
        }

    }

    bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
        if (!bbox.hit(r, ray_t))
            return false;

        bool hit_left = left->hit(r, ray_t, rec);
        bool hit_right = right->hit(r, interval(ray_t.min, hit_left ? rec.t : ray_t.max), rec);

        return hit_left || hit_right;
    }

    aabb bounding_box() const override { return bbox; }

    // Packet traversal entry point: traces up to 4 rays in a packet and fills
    // `out_recs` for lanes that hit. Returns bitmask of lanes that hit.
    inline unsigned int hit_packet(const RayPacket4& pkt, hit_record out_recs[4]) const {
        // mutable local bounds per-lane
        double local_tmin[4];
        double local_tmax[4];
        bool has_hit[4] = {false, false, false, false};

        for (int i = 0; i < 4; ++i) {
            local_tmin[i] = pkt.tmin[i];
            local_tmax[i] = pkt.tmax[i];
        }

        // small stack for traversal (LIFO)
        const bvh_node* stack_nodes[64];
        int sp = 0;
        stack_nodes[sp++] = this;

        unsigned int any_hit_mask = 0;

        while (sp > 0) {
            const bvh_node* node = stack_nodes[--sp];

            // quick packet-AABB test
            unsigned int mask = aabb_packet_test(node->bbox, pkt);
            if (mask == 0) continue;

            // Left child
            if (node->left) {
                auto left_bvh = std::dynamic_pointer_cast<bvh_node>(node->left);
                if (left_bvh) {
                    stack_nodes[sp++] = left_bvh.get();
                } else {
                    // Try sphere packet fast-path
                    auto left_sphere = std::dynamic_pointer_cast<sphere>(node->left);
                    if (left_sphere) {
                        hit_record leaf_recs[4];
                        unsigned int leaf_mask = left_sphere->hit_packet(pkt, leaf_recs);
                        for (int i = 0; i < 4; ++i) {
                            if (!(leaf_mask & (1u << i))) continue;
                            if (! (pkt.active_mask & (1u << i))) continue;
                            if (!has_hit[i] || leaf_recs[i].t < out_recs[i].t) {
                                out_recs[i] = leaf_recs[i];
                                local_tmax[i] = leaf_recs[i].t;
                                has_hit[i] = true;
                                any_hit_mask |= (1u << i);
                            }
                        }
                    } else {
                        // primitive: test per-lane
                        for (int i = 0; i < 4; ++i) {
                            if (!(mask & (1u << i))) continue;
                            if (! (pkt.active_mask & (1u << i))) continue;
                            hit_record tmp;
                            if (node->left->hit(pkt.r[i], interval(local_tmin[i], local_tmax[i]), tmp)) {
                                if (!has_hit[i] || tmp.t < out_recs[i].t) {
                                    out_recs[i] = tmp;
                                    local_tmax[i] = tmp.t;
                                    has_hit[i] = true;
                                    any_hit_mask |= (1u << i);
                                }
                            }
                        }
                    }
                }
            }

            // Right child
            if (node->right) {
                auto right_bvh = std::dynamic_pointer_cast<bvh_node>(node->right);
                if (right_bvh) {
                    stack_nodes[sp++] = right_bvh.get();
                } else {
                    auto right_sphere = std::dynamic_pointer_cast<sphere>(node->right);
                    if (right_sphere) {
                        hit_record leaf_recs[4];
                        unsigned int leaf_mask = right_sphere->hit_packet(pkt, leaf_recs);
                        for (int i = 0; i < 4; ++i) {
                            if (!(leaf_mask & (1u << i))) continue;
                            if (! (pkt.active_mask & (1u << i))) continue;
                            if (!has_hit[i] || leaf_recs[i].t < out_recs[i].t) {
                                out_recs[i] = leaf_recs[i];
                                local_tmax[i] = leaf_recs[i].t;
                                has_hit[i] = true;
                                any_hit_mask |= (1u << i);
                            }
                        }
                    } else {
                        for (int i = 0; i < 4; ++i) {
                            if (!(mask & (1u << i))) continue;
                            if (! (pkt.active_mask & (1u << i))) continue;
                            hit_record tmp;
                            if (node->right->hit(pkt.r[i], interval(local_tmin[i], local_tmax[i]), tmp)) {
                                if (!has_hit[i] || tmp.t < out_recs[i].t) {
                                    out_recs[i] = tmp;
                                    local_tmax[i] = tmp.t;
                                    has_hit[i] = true;
                                    any_hit_mask |= (1u << i);
                                }
                            }
                        }
                    }
                }
            }
        }

        return any_hit_mask;
    }

  private:
    shared_ptr<hittable> left;
    shared_ptr<hittable> right;
    aabb bbox;

    static bool box_compare(
        const shared_ptr<hittable> a, const shared_ptr<hittable> b, int axis_index
    ) {
        auto a_axis_interval = a->bounding_box().axis_interval(axis_index);
        auto b_axis_interval = b->bounding_box().axis_interval(axis_index);
        return a_axis_interval.min < b_axis_interval.min;
    }

    static bool box_x_compare (const shared_ptr<hittable> a, const shared_ptr<hittable> b) {
        return box_compare(a, b, 0);
    }

    static bool box_y_compare (const shared_ptr<hittable> a, const shared_ptr<hittable> b) {
        return box_compare(a, b, 1);
    }

    static bool box_z_compare (const shared_ptr<hittable> a, const shared_ptr<hittable> b) {
        return box_compare(a, b, 2);
    }
};

#endif