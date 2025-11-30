# Dusktracer — Hybrid Ray Tracer + Interactive Editor

[![CMake](https://img.shields.io/badge/build-cmake-blue.svg)](https://cmake.org)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

Dusktracer is a compact hybrid renderer built off Peter Shirley's teachings, featuring:
- **CPU path tracing** (GGX PBR, dielectric Fresnel, microfacet BRDFs)
- **GPU raster preview** for instant feedback (OpenGL, correct TRS)
- **Interactive ImGui editor** with gizmo, picking, materials, and scene graph
- **Assimp-powered import pipeline** (OBJ/FBX + textures)

It serves as a hands-on exploration of real-time editor systems, physically-based shading, transform-correct ray tracing, and asset pipelines.

---

## Active Editor
<img width="2560" alt="Dusktracer Editor" src="https://github.com/user-attachments/assets/dc21bea5-6b67-4773-aad5-7f60da5d536c" />

---

## Features
### Rendering
- **GGX PBR** with metallic/roughness workflow  
- **Dielectric Fresnel** (Schlick/Shirley) & microfacet sampling  
- Correct **inverse-transpose normals** under non-uniform scale  
- Unified transform wrapper for spheres, cubes, and meshes  
- BVH acceleration & AABB optimisations

### Editor Features
- **GPU-based picking** via RGB8 ID buffer  
- **Screen-space gizmo:** axis translation with closest-point ray math  
- **Scene Hierarchy**, **Inspector**, and **Material Editor**  
- Drag & drop **mesh/texture import**

### Raster Preview
- Modern OpenGL (VAO/VBO/EBO)  
- Perfect TRS parity with CPU ray tracer (Rz → Ry → Rx)  
- Per-mesh material slot binding

---

## What I Learned Building This
- Implementing **physically-based BRDFs** (GGX, Smith, Fresnel)  
- Building a **material system** with textured base/rough/metal/normal maps  
- Writing robust **inverse + inverse-transpose** transform math  
- Designing an **editor → render-world** translation layer  
- GPU picking pipelines, gizmo interaction, and editor UX  
- BVH/AABB performance tradeoffs and ray-offset fixes  
- End-to-end **mesh import → normalization → GPU upload**

---

## Technical Highlights
- CPU path tracer (multi-bounce, MIS-inspired sampling)
- Energy-conserving metallic/roughness shading
- Tangent-space normal mapping with adjustable strength
- Stable gizmo translation via line–ray closest points
- Raster preview shares exact TRS math with ray tracer
- Reverse-mapping transforms for correct ray intersection
- In-editor material slot binding for multi-material meshes
