#pragma once

#include <array>
#include <cmath>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include "gpu.hpp"

struct Vertex
{
    glm::vec3 position;
    float tex_coord_x;
    glm::vec3 normal;
    float tex_coord_y;
};

struct Mesh
{
    uint32_t index_count;
    GPUBuffer vertex_buffer;
    GPUBuffer index_buffer;
    VkDeviceAddress vertex_buffer_address;

    size_t material_idx;
};

struct Object
{
    size_t mesh_idx;
};

struct Material
{
    VkDescriptorSet diffuse_set;
    GPUImage diffuse;
};

struct Camera
{
    glm::vec3 eye;
    glm::vec3 rotation;
    glm::vec3 up;
    float fov_y;
    float aspect;
    float z_near;
    float z_far;

    [[nodiscard]] glm::mat4 get_matrix() const
    {
        glm::vec3 forward(
            std::cos(glm::radians(this->rotation.x)) * std::cos(glm::radians(this->rotation.y)),
            std::sin(glm::radians(this->rotation.x)),
            std::cos(glm::radians(this->rotation.x)) * std::sin(glm::radians(this->rotation.y))
        );

        glm::mat4 view = glm::lookAtRH(this->eye, this->eye + forward, this->up);
        glm::mat4 proj = glm::perspectiveRH(this->fov_y, this->aspect, this->z_near, this->z_far);
        return proj * view;
    }
};

struct Scene
{
    std::array<float, 3> background_color;
    Camera camera;

    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Object> objects;
};
