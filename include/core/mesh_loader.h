#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include <memory>
#include <string>
#include <functional>
#include <limits>
#include <iostream>
#include <unordered_map>   // <-- added

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "triangle.h"
#include "hittable_list.h"

// Simple Z-up -> Y-up rotation (-90° around X):
// x' =  x
// y' =  z
// z' = -y
inline vec3 rotate_Zup_to_Yup(const vec3& v) {
    return vec3(
        v.x(),
        v.z(),
        -v.y()
    );
}

// -----------------------------------------------------------------------------
// 1) Single-material version (what you already had)
// -----------------------------------------------------------------------------
inline std::shared_ptr<hittable_list> load_mesh_as_triangles(
    const std::string& filepath,
    const std::shared_ptr<material>& mat,
    bool z_up = false,              // if true: treat source as Z-up and rotate to Y-up
    bool normalise_to_unit = true,  // if true: scale to fit within unit cube
    double user_scale = 1.0
) {
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        filepath,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_GenUVCoords |
        aiProcess_ImproveCacheLocality |
        aiProcess_OptimizeMeshes
    );

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        throw std::runtime_error(
            "Assimp load failed for '" + filepath + "': " +
            importer.GetErrorString()
        );
    }

    std::cout << "\nMaterials in FBX:\n";
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* m = scene->mMaterials[i];

        aiString name;
        m->Get(AI_MATKEY_NAME, name);

        std::cout << "  [" << i << "] " << name.C_Str() << "\n";
    }

    auto world = std::make_shared<hittable_list>();

    // ----- PASS 1: compute bounds (after optional Z->Y rotation) -----
    point3 minp( 1e30, 1e30, 1e30 );
    point3 maxp(-1e30,-1e30,-1e30 );

    std::function<void(aiNode*)> computeBounds;
    computeBounds = [&](aiNode* node) {
        for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                vec3 p(mesh->mVertices[v].x,
                       mesh->mVertices[v].y,
                       mesh->mVertices[v].z);

                if (z_up) {
                    p = rotate_Zup_to_Yup(p);
                }

                minp = point3(
                    std::min(minp.x(), p.x()),
                    std::min(minp.y(), p.y()),
                    std::min(minp.z(), p.z())
                );

                maxp = point3(
                    std::max(maxp.x(), p.x()),
                    std::max(maxp.y(), p.y()),
                    std::max(maxp.z(), p.z())
                );
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            computeBounds(node->mChildren[i]);
    };

    computeBounds(scene->mRootNode);

    point3 center = 0.5 * (minp + maxp);
    vec3 extent = maxp - minp;

    double base_scale = 1.0;
    if (normalise_to_unit) {
        double max_extent = std::max({ extent.x(), extent.y(), extent.z() });
        if (max_extent <= 0.0) max_extent = 1.0;
        base_scale = 1.0 / max_extent;
    }
    double scale = base_scale * user_scale;

    // Pivot so feet are on y=0, centred in X/Z
    double pivot_x = 0.5 * (minp.x() + maxp.x());
    double pivot_z = 0.5 * (minp.z() + maxp.z());
    double pivot_y = minp.y();  // bottom of model (feet)

    vec3 pivot(pivot_x, pivot_y, pivot_z);

    std::cout << "Loaded mesh '" << filepath << "'\n";
    std::cout << "  min = (" << minp.x() << ", " << minp.y() << ", " << minp.z() << ")\n";
    std::cout << "  max = (" << maxp.x() << ", " << maxp.y() << ", " << maxp.z() << ")\n";
    std::cout << "  center = (" << center.x() << ", " << center.y() << ", " << center.z() << ")\n";
    std::cout << "  z_up = " << (z_up ? "true" : "false")
              << ", normalise_to_unit = " << (normalise_to_unit ? "true" : "false")
              << ", base_scale = " << base_scale
              << ", user_scale = " << user_scale
              << ", final_scale = " << scale << "\n";

    long long tri_count = 0;

    // ----- PASS 2: build triangles -----
    std::function<void(aiNode*)> processNode;
    processNode = [&](aiNode* node) {
        for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];

            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;

                int i0 = face.mIndices[0];
                int i1 = face.mIndices[1];
                int i2 = face.mIndices[2];

                // positions
                vec3 p0(mesh->mVertices[i0].x, mesh->mVertices[i0].y, mesh->mVertices[i0].z);
                vec3 p1(mesh->mVertices[i1].x, mesh->mVertices[i1].y, mesh->mVertices[i1].z);
                vec3 p2(mesh->mVertices[i2].x, mesh->mVertices[i2].y, mesh->mVertices[i2].z);

                if (z_up) {
                    p0 = rotate_Zup_to_Yup(p0);
                    p1 = rotate_Zup_to_Yup(p1);
                    p2 = rotate_Zup_to_Yup(p2);
                }

                p0 = (p0 - pivot) * scale;
                p1 = (p1 - pivot) * scale;
                p2 = (p2 - pivot) * scale;

                // normals
                vec3 n0(mesh->mNormals[i0].x, mesh->mNormals[i0].y, mesh->mNormals[i0].z);
                vec3 n1(mesh->mNormals[i1].x, mesh->mNormals[i1].y, mesh->mNormals[i1].z);
                vec3 n2(mesh->mNormals[i2].x, mesh->mNormals[i2].y, mesh->mNormals[i2].z);

                if (z_up) {
                    n0 = rotate_Zup_to_Yup(n0);
                    n1 = rotate_Zup_to_Yup(n1);
                    n2 = rotate_Zup_to_Yup(n2);
                }

                n0 = unit_vector(n0);
                n1 = unit_vector(n1);
                n2 = unit_vector(n2);

                // UVs
                double u0 = 0, v0u = 0;
                double u1 = 0, v1u = 0;
                double u2 = 0, v2u = 0;

                if (mesh->mTextureCoords[0]) {
                    u0 = mesh->mTextureCoords[0][i0].x;
                    v0u = mesh->mTextureCoords[0][i0].y;

                    u1 = mesh->mTextureCoords[0][i1].x;
                    v1u = mesh->mTextureCoords[0][i1].y;

                    u2 = mesh->mTextureCoords[0][i2].x;
                    v2u = mesh->mTextureCoords[0][i2].y;
                }

                // tangents/bitangents
                vec3 t0(mesh->mTangents[i0].x,   mesh->mTangents[i0].y,   mesh->mTangents[i0].z);
                vec3 t1(mesh->mTangents[i1].x,   mesh->mTangents[i1].y,   mesh->mTangents[i1].z);
                vec3 t2(mesh->mTangents[i2].x,   mesh->mTangents[i2].y,   mesh->mTangents[i2].z);

                vec3 b0(mesh->mBitangents[i0].x, mesh->mBitangents[i0].y, mesh->mBitangents[i0].z);
                vec3 b1(mesh->mBitangents[i1].x, mesh->mBitangents[i1].y, mesh->mBitangents[i1].z);
                vec3 b2(mesh->mBitangents[i2].x, mesh->mBitangents[i2].y, mesh->mBitangents[i2].z);

                if (z_up) {
                    t0 = rotate_Zup_to_Yup(t0);
                    t1 = rotate_Zup_to_Yup(t1);
                    t2 = rotate_Zup_to_Yup(t2);

                    b0 = rotate_Zup_to_Yup(b0);
                    b1 = rotate_Zup_to_Yup(b1);
                    b2 = rotate_Zup_to_Yup(b2);
                }

                t0 = unit_vector(t0);
                t1 = unit_vector(t1);
                t2 = unit_vector(t2);

                b0 = unit_vector(b0);
                b1 = unit_vector(b1);
                b2 = unit_vector(b2);

                world->add(std::make_shared<triangle>(
                    point3(p0.x(), p0.y(), p0.z()),
                    point3(p1.x(), p1.y(), p1.z()),
                    point3(p2.x(), p2.y(), p2.z()),
                    n0, n1, n2,
                    u0, v0u,
                    u1, v1u,
                    u2, v2u,
                    t0, t1, t2,
                    b0, b1, b2,
                    mat
                ));

                ++tri_count;
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i]);
    };

    processNode(scene->mRootNode);

    std::cout << "  Triangles: " << tri_count << "\n";

    return world;
}

// -----------------------------------------------------------------------------
// 2) Multi-material version: map FBX material name -> your material
// -----------------------------------------------------------------------------
inline std::shared_ptr<hittable_list> load_mesh_as_triangles(
    const std::string& filepath,
    const std::unordered_map<std::string, std::shared_ptr<material>>& material_table,
    std::shared_ptr<material> default_mat,
    bool z_up = false,
    bool normalise_to_unit = true,
    double user_scale = 1.0
) {
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        filepath,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_GenUVCoords |
        aiProcess_ImproveCacheLocality |
        aiProcess_OptimizeMeshes
    );

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        throw std::runtime_error(
            "Assimp load failed for '" + filepath + "': " +
            importer.GetErrorString()
        );
    }

    std::cout << "\nMaterials in FBX:\n";
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* m = scene->mMaterials[i];

        aiString name;
        m->Get(AI_MATKEY_NAME, name);

        std::cout << "  [" << i << "] " << name.C_Str() << "\n";
    }

    auto world = std::make_shared<hittable_list>();

    // ----- PASS 1: compute bounds (after optional Z->Y rotation) -----
    point3 minp( 1e30, 1e30, 1e30 );
    point3 maxp(-1e30,-1e30,-1e30 );

    std::function<void(aiNode*)> computeBounds;
    computeBounds = [&](aiNode* node) {
        for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                vec3 p(mesh->mVertices[v].x,
                       mesh->mVertices[v].y,
                       mesh->mVertices[v].z);

                if (z_up) {
                    p = rotate_Zup_to_Yup(p);
                }

                minp = point3(
                    std::min(minp.x(), p.x()),
                    std::min(minp.y(), p.y()),
                    std::min(minp.z(), p.z())
                );

                maxp = point3(
                    std::max(maxp.x(), p.x()),
                    std::max(maxp.y(), p.y()),
                    std::max(maxp.z(), p.z())
                );
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            computeBounds(node->mChildren[i]);
    };

    computeBounds(scene->mRootNode);

    point3 center = 0.5 * (minp + maxp);
    vec3 extent = maxp - minp;

    double base_scale = 1.0;
    if (normalise_to_unit) {
        double max_extent = std::max({ extent.x(), extent.y(), extent.z() });
        if (max_extent <= 0.0) max_extent = 1.0;
        base_scale = 1.0 / max_extent;
    }
    double scale = base_scale * user_scale;

    // Pivot so feet are on y=0, centred in X/Z
    double pivot_x = 0.5 * (minp.x() + maxp.x());
    double pivot_z = 0.5 * (minp.z() + maxp.z());
    double pivot_y = minp.y();  // bottom of model (feet)

    vec3 pivot(pivot_x, pivot_y, pivot_z);

    std::cout << "Loaded mesh '" << filepath << "' (multi-material)\n";
    std::cout << "  min = (" << minp.x() << ", " << minp.y() << ", " << minp.z() << ")\n";
    std::cout << "  max = (" << maxp.x() << ", " << maxp.y() << ", " << maxp.z() << ")\n";
    std::cout << "  center = (" << center.x() << ", " << center.y() << ", " << center.z() << ")\n";
    std::cout << "  z_up = " << (z_up ? "true" : "false")
              << ", normalise_to_unit = " << (normalise_to_unit ? "true" : "false")
              << ", base_scale = " << base_scale
              << ", user_scale = " << user_scale
              << ", final_scale = " << scale << "\n";

    long long tri_count = 0;

    // ----- PASS 2: build triangles with per-mesh material -----
    std::function<void(aiNode*)> processNode;
    processNode = [&](aiNode* node) {
        for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];

            // pick material for this mesh
            unsigned int matIndex = mesh->mMaterialIndex;
            aiString aiName;
            scene->mMaterials[matIndex]->Get(AI_MATKEY_NAME, aiName);
            std::string matName = aiName.C_Str();

            std::shared_ptr<material> useMat = default_mat;
            auto it = material_table.find(matName);
            if (it != material_table.end()) {
                useMat = it->second;
            } else {
                std::cout << "WARNING: no material mapping for '" << matName
                          << "', using default.\n";
            }

            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;

                int i0 = face.mIndices[0];
                int i1 = face.mIndices[1];
                int i2 = face.mIndices[2];

                // positions
                vec3 p0(mesh->mVertices[i0].x, mesh->mVertices[i0].y, mesh->mVertices[i0].z);
                vec3 p1(mesh->mVertices[i1].x, mesh->mVertices[i1].y, mesh->mVertices[i1].z);
                vec3 p2(mesh->mVertices[i2].x, mesh->mVertices[i2].y, mesh->mVertices[i2].z);

                if (z_up) {
                    p0 = rotate_Zup_to_Yup(p0);
                    p1 = rotate_Zup_to_Yup(p1);
                    p2 = rotate_Zup_to_Yup(p2);
                }

                p0 = (p0 - pivot) * scale;
                p1 = (p1 - pivot) * scale;
                p2 = (p2 - pivot) * scale;

                // normals
                vec3 n0(mesh->mNormals[i0].x, mesh->mNormals[i0].y, mesh->mNormals[i0].z);
                vec3 n1(mesh->mNormals[i1].x, mesh->mNormals[i1].y, mesh->mNormals[i1].z);
                vec3 n2(mesh->mNormals[i2].x, mesh->mNormals[i2].y, mesh->mNormals[i2].z);

                if (z_up) {
                    n0 = rotate_Zup_to_Yup(n0);
                    n1 = rotate_Zup_to_Yup(n1);
                    n2 = rotate_Zup_to_Yup(n2);
                }

                n0 = unit_vector(n0);
                n1 = unit_vector(n1);
                n2 = unit_vector(n2);

                // UVs
                double u0 = 0, v0u = 0;
                double u1 = 0, v1u = 0;
                double u2 = 0, v2u = 0;

                if (mesh->mTextureCoords[0]) {
                    u0 = mesh->mTextureCoords[0][i0].x;
                    v0u = mesh->mTextureCoords[0][i0].y;

                    u1 = mesh->mTextureCoords[0][i1].x;
                    v1u = mesh->mTextureCoords[0][i1].y;

                    u2 = mesh->mTextureCoords[0][i2].x;
                    v2u = mesh->mTextureCoords[0][i2].y;
                }

                // tangents/bitangents
                vec3 t0(mesh->mTangents[i0].x,   mesh->mTangents[i0].y,   mesh->mTangents[i0].z);
                vec3 t1(mesh->mTangents[i1].x,   mesh->mTangents[i1].y,   mesh->mTangents[i1].z);
                vec3 t2(mesh->mTangents[i2].x,   mesh->mTangents[i2].y,   mesh->mTangents[i2].z);

                vec3 b0(mesh->mBitangents[i0].x, mesh->mBitangents[i0].y, mesh->mBitangents[i0].z);
                vec3 b1(mesh->mBitangents[i1].x, mesh->mBitangents[i1].y, mesh->mBitangents[i1].z);
                vec3 b2(mesh->mBitangents[i2].x, mesh->mBitangents[i2].y, mesh->mBitangents[i2].z);

                if (z_up) {
                    t0 = rotate_Zup_to_Yup(t0);
                    t1 = rotate_Zup_to_Yup(t1);
                    t2 = rotate_Zup_to_Yup(t2);

                    b0 = rotate_Zup_to_Yup(b0);
                    b1 = rotate_Zup_to_Yup(b1);
                    b2 = rotate_Zup_to_Yup(b2);
                }

                t0 = unit_vector(t0);
                t1 = unit_vector(t1);
                t2 = unit_vector(t2);

                b0 = unit_vector(b0);
                b1 = unit_vector(b1);
                b2 = unit_vector(b2);

                world->add(std::make_shared<triangle>(
                    point3(p0.x(), p0.y(), p0.z()),
                    point3(p1.x(), p1.y(), p1.z()),
                    point3(p2.x(), p2.y(), p2.z()),
                    n0, n1, n2,
                    u0, v0u,
                    u1, v1u,
                    u2, v2u,
                    t0, t1, t2,
                    b0, b1, b2,
                    useMat
                ));

                ++tri_count;
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i]);
    };

    processNode(scene->mRootNode);

    std::cout << "  Triangles: " << tri_count << "\n";

    return world;
}

#endif // MESH_LOADER_H
