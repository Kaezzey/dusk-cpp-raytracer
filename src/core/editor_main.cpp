// src/core/editor_main.cpp

#include <cstdio>
#include <stdexcept>
#include <atomic>
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>   // for material labels

#include "../../external/glew/include/GL/glew.h"   // GLEW (OpenGL loader)
#include "../../include/external/GLFW/glfw3.h"     // our local GLFW

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "../../include/core/camera.h"
#include "../../include/core/renderer.h"
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/editor_camera.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// -----------------------------------------------------------------------------
// Engine globals
// -----------------------------------------------------------------------------

static scene               g_scene;
static camera              g_camera;
static renderer            g_renderer;
static editor_camera_state g_editor_cam;
static std::atomic<bool>   g_cancel_flag{false};
static bool                g_scene_initialized = false;

// Viewport focus/hover state
static bool g_viewport_focused = false;
static bool g_viewport_hovered = false;

// Editor camera tuning
static float g_camera_move_speed = 4.0f;   // units per second
static float g_camera_look_sens  = 0.2f;   // mouse sensitivity

// Currently selected object in the Scene Hierarchy (-1 = none)
static int g_selected_object = -1;

// -----------------------------------------------------------------------------
// GPU texture for displaying the ray-traced image
// -----------------------------------------------------------------------------

static GLuint                    g_rtTexture  = 0;
static int                       g_rtWidth    = 0;
static int                       g_rtHeight   = 0;
static bool                      g_rtHasImage = false;
static std::vector<std::uint8_t> g_rtPixels;   // RGBA8

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Build a simple test scene using your scene.h material/object system
static void build_default_scene(scene& scn)
{
    scn.materials.clear();
    scn.objects.clear();
    scn.meshes.clear();
    scn.lights.clear();

    // -------------------------
    // Materials
    // -------------------------
    int ground_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name       = "Ground";
    scn.materials.back().model      = scene_material_model::lambert;
    scn.materials.back().base_color = colour(0.8, 0.8, 0.0);

    int center_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name       = "Center";
    scn.materials.back().model      = scene_material_model::lambert;
    scn.materials.back().base_color = colour(0.1, 0.2, 0.5);

    int glass_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name       = "Glass";
    scn.materials.back().model      = scene_material_model::dielectric;
    scn.materials.back().ior        = 1.5;
    scn.materials.back().base_color = colour(1.0, 1.0, 1.0);

    int pbr_metal_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name            = "PBR Metal";
    scn.materials.back().model           = scene_material_model::pbr;
    scn.materials.back().base_color      = colour(0.8, 0.6, 0.2);
    scn.materials.back().metallic        = 1.0;
    scn.materials.back().roughness       = 0.2;
    scn.materials.back().normal_strength = 1.0;
    scn.materials.back().dielectric_F0   = colour(0.04, 0.04, 0.04);

    // -------------------------
    // Spheres
    // -------------------------
    scn.objects.push_back({
        "GroundSphere",
        scene_object_type::sphere,
        ground_mat,
        point3(0, -100.5, -1),
        100.0,
        -1,                    // mesh_index
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    scn.objects.push_back({
        "CenterSphere",
        scene_object_type::sphere,
        center_mat,
        point3(0, 0, -1),
        0.5,
        -1,
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    scn.objects.push_back({
        "GlassSphere",
        scene_object_type::sphere,
        glass_mat,
        point3(-1, 0, -1),
        0.5,
        -1,
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    scn.objects.push_back({
        "PBRMetalSphere",
        scene_object_type::sphere,
        pbr_metal_mat,
        point3(1, 0, -1),
        0.5,
        -1,
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    // Optional light placeholder
    scene_light sun;
    sun.name      = "Sun";
    sun.type      = scene_light_type::directional;
    sun.radiance  = vec3(3.0, 3.0, 3.0);
    sun.direction = unit_vector(vec3(-1.0, -1.0, -0.5));
    scn.lights.push_back(std::move(sun));
}

static void init_engine_once()
{
    if (g_scene_initialized)
        return;

    build_default_scene(g_scene);

    // Editor camera initial pose (same as your old camera)
    g_editor_cam.position = point3(3, 3, 2);
    g_editor_cam.vfov     = 40.0;

    point3 target  = point3(0, 0, -1);
    vec3   forward = unit_vector(target - g_editor_cam.position);

    g_editor_cam.yaw   = std::atan2(forward.z(), forward.x());
    g_editor_cam.pitch = std::asin(forward.y());
    g_editor_cam.update_basis();

    // Ray-tracer camera base params
    g_camera.aspect_ratio      = 16.0 / 9.0;
    g_camera.image_width       = 800;
    g_camera.samples_per_pixel = 20;   // default sampling
    g_camera.max_depth         = 20;   // default bounce depth
    g_camera.background        = colour(0.70, 0.80, 1.00);

    // Ensure camera uses the editor cam pose
    to_shirley_camera(g_editor_cam, g_camera);

    g_scene_initialized = true;
}

// Sync editor camera into the ray-tracer camera
static void sync_camera_from_editor(float viewport_width, float viewport_height)
{
    if (viewport_width > 0.0f && viewport_height > 0.0f) {
        g_camera.aspect_ratio = viewport_width / viewport_height;
    }

    // Resolution binding could go here later if you want.

    to_shirley_camera(g_editor_cam, g_camera);
}

// Convert render_result (uint8 RGB) into an RGBA8 OpenGL texture.
static void UploadRenderToTexture(const render_result& img)
{
    const int width  = img.width;
    const int height = img.height;

    const std::vector<std::uint8_t>& src = img.pixels; // 3 bytes per pixel

    if (width <= 0 || height <= 0 || src.empty())
        return;

    g_rtWidth  = width;
    g_rtHeight = height;
    g_rtPixels.resize(width * height * 4);

    for (int i = 0; i < width * height; ++i) {
        int src_idx = 3 * i;
        int dst_idx = 4 * i;

        g_rtPixels[dst_idx + 0] = src[src_idx + 0]; // R
        g_rtPixels[dst_idx + 1] = src[src_idx + 1]; // G
        g_rtPixels[dst_idx + 2] = src[src_idx + 2]; // B
        g_rtPixels[dst_idx + 3] = 255;              // A
    }

    if (g_rtTexture == 0) {
        glGenTextures(1, &g_rtTexture);
        glBindTexture(GL_TEXTURE_2D, g_rtTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA,
            width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, g_rtPixels.data()
        );
    } else {
        glBindTexture(GL_TEXTURE_2D, g_rtTexture);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA,
            width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, g_rtPixels.data()
        );
    }

    g_rtHasImage = true;
}

// Basic FPS-style camera controls using GLFW input
static void update_editor_camera_from_input(GLFWwindow* window, double dt)
{
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    // Only move camera when viewport is focused/hovered
    if (!g_viewport_focused && !g_viewport_hovered)
        return;

    // -----------------------------
    // Keyboard movement (WASD/QE)
    // -----------------------------
    float move_forward = 0.0f;
    float move_right   = 0.0f;
    float move_up      = 0.0f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) move_forward += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) move_forward -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) move_right   += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) move_right   -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) move_up      += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) move_up      -= 1.0f;

    if (move_forward != 0.0f || move_right != 0.0f || move_up != 0.0f) {
        g_editor_cam.move_from_input(
            move_forward,
            move_right,
            move_up,
            (float)(dt * g_camera_move_speed)
        );
    }

    // -----------------------------
    // Mouse look (hold RMB)
    // -----------------------------
    static bool   rotating = false;
    static double last_x   = 0.0;
    static double last_y   = 0.0;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
    {
        double x, y;
        glfwGetCursorPos(window, &x, &y);

        if (!rotating) {
            rotating = true;

            // Lock/hide cursor while looking
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            last_x = x;
            last_y = y;
            return; // avoid jump on first frame
        }

        double dx = x - last_x;
        double dy = y - last_y;
        last_x = x;
        last_y = y;

        float sensitivity = g_camera_look_sens;
        float yaw_delta   =  (float)( dx * sensitivity);  // right -> +yaw
        float pitch_delta =  (float)(-dy * sensitivity);  // up   -> +pitch
        g_editor_cam.look(yaw_delta, pitch_delta);
    }
    else
    {
        if (rotating) {
            // Restore cursor
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        rotating = false;
    }
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
    // ---------------------------------------------------------
    // GLFW init + window
    // ---------------------------------------------------------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "Dusktracer Editor", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // GLEW init (after context)
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "Failed to init GLEW\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // ---------------------------------------------------------
    // ImGui context + backends
    // ---------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Init engine (scene + camera) once
    init_engine_once();

    bool show_demo_window = false;

    double last_time = glfwGetTime();

    // ---------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double current_time = glfwGetTime();
        double dt = current_time - last_time;
        last_time = current_time;

        // Update editor camera from input (WASD, Q/E, RMB look)
        update_editor_camera_from_input(window, dt);

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // -------------------------------------------------
        // Dockspace
        // -------------------------------------------------
        {
            ImGuiWindowFlags window_flags =
                ImGuiWindowFlags_MenuBar |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus;

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            ImGui::Begin("DockSpaceRoot", nullptr, window_flags);
            ImGui::PopStyleVar(2);

            ImGuiID dockspace_id = ImGui::GetID("DusktracerDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Exit")) {
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            ImGui::End(); // DockSpaceRoot
        }

        // -------------------------------------------------
        // Scene Hierarchy (with selection)
        // -------------------------------------------------
        ImGui::Begin("Scene Hierarchy");
        ImGui::Text("Objects:");
        ImGui::Separator();
        for (size_t i = 0; i < g_scene.objects.size(); ++i) {
            bool is_selected = (int)i == g_selected_object;
            if (ImGui::Selectable(g_scene.objects[i].name.c_str(), is_selected)) {
                g_selected_object = (int)i;
            }
        }
        if (g_scene.objects.empty()) {
            ImGui::TextDisabled("No objects in scene.");
        }
        ImGui::End();

        // -------------------------------------------------
        // Inspector
        // -------------------------------------------------
        ImGui::Begin("Inspector");
        ImGui::Text("Camera");
        ImGui::Separator();
        ImGui::Text("Position: (%.2f, %.2f, %.2f)",
            g_editor_cam.position.x(),
            g_editor_cam.position.y(),
            g_editor_cam.position.z());
        ImGui::Text("Yaw: %.2f, Pitch: %.2f", g_editor_cam.yaw, g_editor_cam.pitch);

        ImGui::Separator();
        ImGui::Text("Controls:");
        ImGui::Text("  WASD = move, Q/E = down/up");
        ImGui::Text("  RMB drag in Viewport = look");

        ImGui::Separator();
        ImGui::Text("Editor Camera Settings");
        ImGui::SliderFloat("Move speed",  &g_camera_move_speed, 0.1f, 20.0f);
        ImGui::SliderFloat("Look sens",   &g_camera_look_sens,  0.01f, 1.0f);
        ImGui::SliderFloat("FOV",         &g_editor_cam.vfov,   20.0f, 90.0f);

        if (ImGui::Button("Reset Camera")) {
            g_editor_cam.position = point3(3, 3, 2);
            g_editor_cam.vfov     = 40.0;

            point3 target  = point3(0, 0, -1);
            vec3   forward = unit_vector(target - g_editor_cam.position);
            g_editor_cam.yaw   = std::atan2(forward.z(), forward.x());
            g_editor_cam.pitch = std::asin(forward.y());
            g_editor_cam.update_basis();
        }

        // -------------------------------------------------
        // Render Settings (sampling)
        // -------------------------------------------------
        ImGui::Separator();
        ImGui::Text("Render Settings");

        int spp = g_camera.samples_per_pixel;
        if (ImGui::DragInt("Samples per pixel", &spp, 1, 1, 4096)) {
            if (spp < 1)  spp = 1;
            g_camera.samples_per_pixel = spp;
        }

        int max_depth = g_camera.max_depth;
        if (ImGui::DragInt("Max bounce depth", &max_depth, 1, 1, 128)) {
            if (max_depth < 1) max_depth = 1;
            g_camera.max_depth = max_depth;
        }

        ImGui::TextDisabled("Higher = cleaner but slower.");

        ImGui::Separator();
        ImGui::Text("Render Resolution");

        static int res_w = g_camera.image_width;
        static int res_h = g_camera.image_height;

        if (ImGui::InputInt("Width", &res_w)) {
            if (res_w < 16) res_w = 16;
        }
        if (ImGui::InputInt("Height", &res_h)) {
            if (res_h < 16) res_h = 16;
        }

        if (ImGui::Button("Apply Resolution")) {
            g_camera.image_width  = res_w;
            g_camera.image_height = res_h;
        }
        ImGui::TextDisabled("Changes take effect next render.");

        ImGui::Separator();
        ImGui::Text("Selection");

        if (g_selected_object >= 0 &&
            g_selected_object < (int)g_scene.objects.size())
        {
            auto& obj = g_scene.objects[g_selected_object];
            ImGui::Text("Selected object:");
            ImGui::BulletText("Name: %s", obj.name.c_str());

            // -----------------------------
            // Simple transform controls
            // -----------------------------
            point3 pos = obj.center;
            float pos_f[3] = { (float)pos.x(), (float)pos.y(), (float)pos.z() };
            if (ImGui::DragFloat3("Position", pos_f, 0.05f)) {
                obj.center = point3(pos_f[0], pos_f[1], pos_f[2]);
            }

            float radius_f = (float)obj.radius;
            if (ImGui::DragFloat("Radius", &radius_f, 0.01f, 0.01f, 1000.0f)) {
                obj.radius = radius_f;
            }

            ImGui::Separator();
            ImGui::Text("Material Binding");

            // -------- Material index combo (which material this object uses) --------
            int current_mat = obj.material_index;
            if (current_mat < 0 || current_mat >= (int)g_scene.materials.size()) {
                current_mat = -1;
            }

            const char* current_label = "<none>";
            if (current_mat >= 0) {
                current_label = g_scene.materials[current_mat].name.c_str();
            }

            if (ImGui::BeginCombo("Material", current_label)) {
                for (int i = 0; i < (int)g_scene.materials.size(); ++i) {
                    bool is_sel = (i == current_mat);
                    // Unique label in case names repeat
                    std::string label = g_scene.materials[i].name + "##mat_" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), is_sel)) {
                        obj.material_index = i;
                        current_mat        = i;
                    }
                    if (is_sel) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // If we have a valid material bound, expose its class + params
            if (obj.material_index >= 0 &&
                obj.material_index < (int)g_scene.materials.size())
            {
                auto& mat = g_scene.materials[obj.material_index];

                ImGui::Separator();
                ImGui::Text("Material Class");

                // -------- Material class (maps to concrete classes in material.h) ----
                const char* model_label = "Unknown";
                switch (mat.model) {
                    case scene_material_model::lambert:    model_label = "Lambert";    break;
                    case scene_material_model::metal:      model_label = "Metal";      break;
                    case scene_material_model::dielectric: model_label = "Dielectric"; break;
                    case scene_material_model::pbr:        model_label = "PBR";        break;
                    default:                               model_label = "Unknown";    break;
                }

                if (ImGui::BeginCombo("Class", model_label)) {
                    struct Option { const char* label; scene_material_model model; };
                    Option opts[] = {
                        { "Lambert (Shirley diffuse)",   scene_material_model::lambert    },
                        { "Metal (Shirley metal)",       scene_material_model::metal      },
                        { "Dielectric (glass)",          scene_material_model::dielectric },
                        { "PBR GGX (your shader)",       scene_material_model::pbr        },
                    };

                    for (const auto& opt : opts) {
                        bool selected = (mat.model == opt.model);
                        if (ImGui::Selectable(opt.label, selected)) {
                            mat.model = opt.model;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::Separator();
                ImGui::Text("Material Parameters");

                // Base color (used by basically everything)
                {
                    float base[3] = {
                        (float)mat.base_color.x(),
                        (float)mat.base_color.y(),
                        (float)mat.base_color.z()
                    };
                    if (ImGui::ColorEdit3("Base Color", base)) {
                        mat.base_color = colour(base[0], base[1], base[2]);
                    }
                }

                // Class-specific controls
                switch (mat.model) {
                case scene_material_model::lambert:
                    ImGui::TextDisabled("Lambert: pure diffuse (Shirley lambertian).");
                    break;

                case scene_material_model::metal:
                    ImGui::TextDisabled("Metal: colored metal BRDF (Shirley metal).");
                    // Hook up fuzz here if you add it to scene_material.
                    break;

                case scene_material_model::dielectric:
                {
                    ImGui::TextDisabled("Dielectric: refractive material (glass).");

                    float ior_f = (float)mat.ior;
                    if (ImGui::SliderFloat("IOR", &ior_f, 1.0f, 2.5f)) {
                        mat.ior = ior_f;
                    }
                } break;

                case scene_material_model::pbr:
                {
                    ImGui::TextDisabled("PBR GGX: uses your pbr_material in material.h");

                    float metallic_f  = (float)mat.metallic;
                    float roughness_f = (float)mat.roughness;
                    float norm_str_f  = (float)mat.normal_strength;

                    if (ImGui::SliderFloat("Metallic", &metallic_f, 0.0f, 1.0f)) {
                        mat.metallic = metallic_f;
                    }
                    if (ImGui::SliderFloat("Roughness", &roughness_f, 0.02f, 1.0f)) {
                        mat.roughness = roughness_f;
                    }
                    if (ImGui::SliderFloat("Normal Strength", &norm_str_f, 0.0f, 4.0f)) {
                        mat.normal_strength = norm_str_f;
                    }

                    float f0[3] = {
                        (float)mat.dielectric_F0.x(),
                        (float)mat.dielectric_F0.y(),
                        (float)mat.dielectric_F0.z()
                    };
                    if (ImGui::ColorEdit3("Dielectric F0", f0)) {
                        mat.dielectric_F0 = colour(f0[0], f0[1], f0[2]);
                    }

                    ImGui::TextDisabled("Normal map / textures are still driven by scene/asset system.");
                } break;

                default:
                    ImGui::TextDisabled("Unknown material model – extend UI if you add more.");
                    break;
                }

                ImGui::Separator();
                ImGui::TextDisabled("Edit transform/material/sampling, then re-render.");
            }
            else {
                ImGui::TextDisabled("Object has no valid material bound.");
            }
        }
        else {
            ImGui::TextDisabled("No object selected.");
        }

        ImGui::End();

        // -------------------------------------------------
        // Debug Camera window: shows editor camera + viewport state
        // -------------------------------------------------
        ImGui::Begin("Debug Camera");
        ImGui::Text("Editor cam position:");
        ImGui::Text("  x = %.3f", g_editor_cam.position.x());
        ImGui::Text("  y = %.3f", g_editor_cam.position.y());
        ImGui::Text("  z = %.3f", g_editor_cam.position.z());
        ImGui::Separator();
        ImGui::Text("Yaw   = %.3f", g_editor_cam.yaw);
        ImGui::Text("Pitch = %.3f", g_editor_cam.pitch);
        ImGui::Separator();
        ImGui::Text("Viewport focused: %s", g_viewport_focused ? "true" : "false");
        ImGui::Text("Viewport hovered: %s", g_viewport_hovered ? "true" : "false");
        ImGui::Separator();
        ImGui::Text("Samples per pixel: %d", g_camera.samples_per_pixel);
        ImGui::Text("Max depth:         %d", g_camera.max_depth);
        ImGui::End();

        // -------------------------------------------------
        // Viewport: Render button + display ray-traced texture
        // -------------------------------------------------
        ImGui::Begin("Viewport");

        g_viewport_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        g_viewport_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        ImVec2 vp_size = ImGui::GetContentRegionAvail();

        if (ImGui::Button("Render Current View")) {
            // Sync camera to current viewport aspect and editor pose
            sync_camera_from_editor(vp_size.x, vp_size.y);

            // Build world from scene and run the ray tracer
            hittable_list world = build_world_from_scene(g_scene);

            g_cancel_flag.store(false);
            render_result img = g_renderer.render(world, g_camera, &g_cancel_flag);

            UploadRenderToTexture(img);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Move camera / edit objects / sampling, then click Render.");

        ImGui::Separator();

        if (g_rtHasImage && g_rtTexture != 0) {
            // Preserve aspect ratio inside available viewport
            float img_aspect = (float)g_rtWidth / (float)g_rtHeight;
            float vp_aspect  = vp_size.x / vp_size.y;

            ImVec2 image_size = vp_size;
            if (vp_aspect > img_aspect) {
                image_size.x = vp_size.y * img_aspect;
            } else {
                image_size.y = vp_size.x / img_aspect;
            }

            ImVec2 cursor = ImGui::GetCursorPos();
            ImVec2 avail  = vp_size;
            ImVec2 centered_pos = ImVec2(
                cursor.x + 0.5f * (avail.x - image_size.x),
                cursor.y + 0.5f * (avail.y - image_size.y)
            );
            ImGui::SetCursorPos(centered_pos);

            ImGui::Image(
                (ImTextureID)(intptr_t)g_rtTexture,
                image_size,
                ImVec2(0, 0),   // UVs, no vertical flip
                ImVec2(1, 1)
            );
        } else {
            ImGui::Text("No render yet. Click 'Render Current View'.");
        }

        ImGui::End(); // Viewport

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // -------------------------------------------------
        // Render ImGui
        // -------------------------------------------------
        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }

    // Cleanup
    if (g_rtTexture != 0) {
        glDeleteTextures(1, &g_rtTexture);
        g_rtTexture = 0;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
