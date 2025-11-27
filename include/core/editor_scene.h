// editor_scene.h
#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include "scene.h"
#include "editor_camera.h"
#include "hittable_list.h"

struct editor_scene {
    scene runtime;        // the actual data the renderer needs

    // editor-only stuff:
    int selected_object = -1;
    int selected_mat    = -1;
    editor_camera_state camera_state;
    // gizmos, UI flags, etc.
};

// Helper: rebuilds hittable world from the editor's current runtime scene
inline hittable_list build_world_from_editor(const editor_scene& escn) {
    return build_world_from_scene(escn.runtime);
}

#endif
