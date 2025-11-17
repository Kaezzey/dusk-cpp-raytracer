#include "../../include/external/glfw3.h"
#include "../../include/core/dusktracer.h"

#include <chrono>
#include <fstream>

#include "../../include/core/camera.h"
#include "../../include/core/editor_camera.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/material.h"
#include "../../include/core/sphere.h"

struct CameraBasis {
    vec3 forward;
    vec3 right;
    vec3 up;
};

CameraBasis compute_camera_basis(const CameraState& state) {
    vec3 forward{
        std::cos(state.pitch) * std::cos(state.yaw),
        std::sin(state.pitch),
        std::cos(state.pitch) * std::sin(state.yaw)
    };

    forward = unit_vector(forward);

    vec3 world_up(0, 1, 0);
    vec3 right = unit_vector(cross(forward, world_up));
    vec3 up    = unit_vector(cross(right, forward));

    return { forward, right, up };
}


struct InputContext {
    CameraState* cam_state;
    bool first_mouse = true;
    double last_x = 0.0;
    double last_y = 0.0;
};

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* ctx = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->cam_state) return;
    CameraState& state = *ctx->cam_state;

    if (ctx->first_mouse) {
        ctx->last_x = xpos;
        ctx->last_y = ypos;
        ctx->first_mouse = false;
    }

    double xoffset = xpos - ctx->last_x;
    double yoffset = ctx->last_y - ypos; // inverted Y
    ctx->last_x = xpos;
    ctx->last_y = ypos;

    xoffset *= state.mouse_sensitivity;
    yoffset *= state.mouse_sensitivity;

    state.yaw   += xoffset;
    state.pitch += yoffset;

    const double max_pitch = 1.55; // ~89 deg
    if (state.pitch >  max_pitch) state.pitch =  max_pitch;
    if (state.pitch < -max_pitch) state.pitch = -max_pitch;
}

void process_keyboard(GLFWwindow* window, CameraState& state, double dt) {
    CameraBasis basis = compute_camera_basis(state);

    double speed = state.move_speed * dt;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        state.pos = state.pos + basis.forward * speed;

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        state.pos = state.pos - basis.forward * speed;

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        state.pos = state.pos - basis.right * speed;

    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        state.pos = state.pos + basis.right * speed;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        state.pos = state.pos + basis.up * speed;

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        state.pos = state.pos - basis.up * speed;
}


hittable_list scene(){

    hittable_list world;

    auto material_ground = make_shared<lambertian>(colour(0.8, 0.8, 0.0));
    auto material_center = make_shared<lambertian>(colour(0.1, 0.2, 0.5));
    auto material_left   = make_shared<dielectric>(1.50);
    auto material_bubble = make_shared<dielectric>(1.00 / 1.50);

    world.add(make_shared<sphere>(point3( 0.0, -100.5, -1.0), 100.0, material_ground));
    world.add(make_shared<sphere>(point3( 0.0,    0.0, -1.2),   0.5, material_center));
    world.add(make_shared<sphere>(point3(-1.0,    0.0, -1.0),   0.5, material_left));
    world.add(make_shared<sphere>(point3(-1.0,    0.0, -1.0),   0.4, material_bubble));
    world.add(make_shared<sphere>(point3( 1.0,    0.0, -1.0),   0.5, material_left));

    return world;
}

camera build_duskcam (const CameraState& state, double aspect_ratio, int image_width, int samples_per_pixel, int max_depth) {
    camera cam;

    vec3 forward{
        std::cos(state.pitch) * std::cos(state.yaw),
        std::sin(state.pitch),
        std::cos(state.pitch) * std::sin(state.yaw)
    };

    point3 lookfrom = state.pos;
    point3 lookat   = state.pos + forward;
    vec3 vup(0,1,0);

    cam.aspect_ratio = aspect_ratio;
    cam.image_width  = image_width;
    cam.samples_per_pixel = samples_per_pixel;
    cam.max_depth = max_depth;

    cam.vfov  = state.vfov;
    cam.lookfrom = lookfrom;
    cam.lookat = lookat;
    cam.vup = vup;

    cam.defocus_angle = 0.0;
    cam.focus_dist = state.focus_dist;

    return cam;
}

struct PreviewSphere {
    point3 center;
    double radius;
    vec3 colour;
};

std::vector<PreviewSphere> preview_spheres;

void draw_sphere_immediate(const point3& c, double r, int stacks = 16, int slices = 16)
{
    for (int i = 0; i < stacks; ++i) {
        double phi1 = pi * double(i) / stacks;
        double phi2 = pi * double(i + 1) / stacks;

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j) {
            double theta = 2.0 * pi * double(j) / slices;

            double x1 = std::sin(phi1) * std::cos(theta);
            double y1 = std::cos(phi1);
            double z1 = std::sin(phi1) * std::sin(theta);

            double x2 = std::sin(phi2) * std::cos(theta);
            double y2 = std::cos(phi2);
            double z2 = std::sin(phi2) * std::sin(theta);

            // first ring
            glNormal3d(x1, y1, z1);
            glVertex3d(c.x() + r * x1, c.y() + r * y1, c.z() + r * z1);

            // second ring
            glNormal3d(x2, y2, z2);
            glVertex3d(c.x() + r * x2, c.y() + r * y2, c.z() + r * z2);
        }
        glEnd();
    }
}

void render_preview_scene(GLFWwindow* window,
                          const CameraState& cam_state,
                          const std::vector<PreviewSphere>& spheres)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    double aspect = (height > 0) ? double(width) / double(height) : 1.0;

    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);

    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ----- Projection (perspective) -----
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    double fov_rad   = cam_state.vfov * pi / 180.0;
    double nearPlane = 0.1;
    double farPlane  = 100.0;
    double top       = nearPlane * std::tan(fov_rad / 2.0);
    double bottom    = -top;
    double right     = top * aspect;
    double left      = -right;

    glFrustum(left, right, bottom, top, nearPlane, farPlane);

    // ----- View matrix from CameraState -----
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    CameraBasis basis = compute_camera_basis(cam_state);

    vec3 f = basis.forward;
    vec3 r = basis.right;
    vec3 u = basis.up;

    point3 p = cam_state.pos;

    double V[16] = {
        r.x(),  u.x(), -f.x(), 0,
        r.y(),  u.y(), -f.y(), 0,
        r.z(),  u.z(), -f.z(), 0,
        -dot(r,p), -dot(u,p), dot(f,p), 1
    };

    glLoadMatrixd(V);

    // Simple fixed-function lighting
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    float light_pos[] = { 1.0f, 1.0f, 1.0f, 0.0f }; // directional
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    float diffuse[] = { 0.8f, 0.8f, 0.9f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);

    // ----- Draw all preview spheres -----
    for (const auto& s : spheres) {
        draw_sphere_immediate(s.center, s.radius, 16, 16);
    }

    glDisable(GL_LIGHTING);
}


int main() {
    // --- GLFW init ---
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(800, 600, "Dusktracer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // --- Scene setup ---
    hittable_list world = scene();

    // Build preview_spheres from the ray-tracing scene
    preview_spheres.clear();

    for (const auto& obj : world.objects) {   // objects is std::vector<shared_ptr<hittable>>
        if (auto sp = std::dynamic_pointer_cast<sphere>(obj)) {

            auto mat = sp->mat; // make mat public or add getter

            vec3 preview_col(0.8, 0.8, 0.8); // default

            if (auto lam = std::dynamic_pointer_cast<lambertian>(mat))
                preview_col = lam->albedo;   // lambertian colour

            if (auto metal = std::dynamic_pointer_cast<metal>(mat))
                preview_col = metal->albedo;

            if (auto glass = std::dynamic_pointer_cast<dielectric>(mat))
                preview_col = vec3(0.5, 0.8, 1.0); // light blue for glass

            preview_spheres.push_back({ sp->centre, sp->radius, preview_col });
        }
    }

    // --- Camera state init ---
    CameraState cam_state{};
    cam_state.pos = point3(0.0, 0.0, 0.0);  // tweak to taste
    cam_state.yaw = 0.0;
    cam_state.pitch = 0.0;
    cam_state.vfov = 40.0;
    cam_state.focus_dist = 1.0;
    cam_state.move_speed = 2.5;
    cam_state.mouse_sensitivity = 0.002;

    // Input context for mouse callback
    InputContext input_ctx{ &cam_state };
    glfwSetWindowUserPointer(window, &input_ctx);
    glfwSetCursorPosCallback(window, mouse_callback);

    // Timing
    auto last_time = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        auto now = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        glfwPollEvents();
        process_keyboard(window, cam_state, dt);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        // Press R to render current camera view (still offline for now)
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            camera cam = build_duskcam(cam_state, 16.0/9.0, 400, 50, 50);

            std::ofstream file("output.ppm");
            
            cam.render(world, file); // currently outputs PPM to stdout in your code
        }

        glClear(GL_COLOR_BUFFER_BIT);
        
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = (height > 0) ? float(width) / float(height) : 1.0f;

        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- Set up projection matrix ---
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        double fov = cam_state.vfov;        // in degrees
        double nearPlane = 0.1;
        double farPlane  = 100.0;
        double top    = nearPlane * std::tan(0.5 * fov * pi / 180.0);
        double bottom = -top;
        double right  = top * aspect;
        double left   = -right;
        glFrustum(left, right, bottom, top, nearPlane, farPlane);

        // --- Set up view matrix from CameraState ---
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        vec3 forward{
            std::cos(cam_state.pitch) * std::cos(cam_state.yaw),
            std::sin(cam_state.pitch),
            std::cos(cam_state.pitch) * std::sin(cam_state.yaw)
        };

        point3 eye = cam_state.pos;
        point3 center = eye + forward;
        vec3 up(0, 1, 0);

        // manual gluLookAt
        auto ux = unit_vector(cross(up, forward));
        auto uy = unit_vector(cross(forward, ux));

        double ex = eye.x(), ey = eye.y(), ez = eye.z();
        double m[16] = {
            ux.x(),  uy.x(),  -forward.x(), 0,
            ux.y(),  uy.y(),  -forward.y(), 0,
            ux.z(),  uy.z(),  -forward.z(), 0,
            -dot(ux, eye), -dot(uy, eye), dot(forward, eye), 1
        };
        glLoadMatrixd(m);

        // simple flat color for now
        glColor3f(0.8f, 0.8f, 0.9f);

        // --- Draw all spheres from the scene ---
        for (const auto& s : preview_spheres) {
            glColor3f(s.colour.x(), s.colour.y(), s.colour.z());
            draw_sphere_immediate(s.center, s.radius);
        }

        render_preview_scene(window, cam_state, preview_spheres);

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

