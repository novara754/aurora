#include "app.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "vkerr.hpp"

[[nodiscard]] bool App::init()
{
    spdlog::trace("App::init: starting initialization");

    if (!m_engine.init())
    {
        spdlog::error("App::init: failed to initialize engine");
        return false;
    }
    spdlog::trace("App::init: engine initialized");

    if (!m_forward_pass.init())
    {
        spdlog::error("App::init: failed to forward render pass");
        return false;
    }
    spdlog::trace("App::init: forward pass initialized");

    if (!m_imgui_pass.init())
    {
        spdlog::error("App::init: failed to imgui render pass");
        return false;
    }
    spdlog::trace("App::init: imgui pass initialized");

    {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VKERR(
            vkCreateSampler(m_engine.get_device(), &sampler_info, nullptr, &m_sampler),
            "App::init: failed to create default sampler"
        );
        m_deletion_queue.add([this] { vkDestroySampler(m_engine.get_device(), m_sampler, nullptr); }
        );
    }
    spdlog::trace("App::init: created default sampler");

    if (!create_scene_from_file("../assets/sponza/sponza.gltf", m_scene))
    {
        destroy_scene(m_scene);
        spdlog::error("App::init: failed to load scene from file");
        return false;
    }
    m_deletion_queue.add([this] { destroy_scene(m_scene); });
    spdlog::trace("App::init: loaded scene");

    spdlog::trace("App::init: initialization complete");
    return true;
}

void App::run()
{
    spdlog::trace("App::run: entering main loop");
    m_last_frame_time = SDL_GetTicks();
    while (true)
    {
        double now = SDL_GetTicks();
        m_delta_time = now - m_last_frame_time;
        m_last_frame_time = now;

        SDL_Event event;
        if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                spdlog::trace("App::run: got quit event");
                return;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                spdlog::trace("App::run: got resize event");
                if (!m_engine.refresh_swapchain())
                {
                    spdlog::error("App::run: failed to recreate swapchain for resize");
                    break;
                }
            }
            else if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
            {
                m_disable_render = true;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
            {
                m_disable_render = false;
            }

            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (m_disable_render)
        {
            SDL_Delay(100);
            continue;
        }

        if (!render_frame())
        {
            spdlog::error("App::run: failed to render frame");
            return;
        }
    }
    spdlog::trace("App::run: exited main loop");
}

[[nodiscard]] bool App::render_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    build_ui();

    ImGui::Render();

    VkCommandBuffer cmd_buffer;
    uint32_t swapchain_image_idx;
    if (!m_engine.start_frame(cmd_buffer, swapchain_image_idx))
    {
        spdlog::error("App::render_frame: failed to start frame");
        return false;
    }

    m_forward_pass.render(cmd_buffer, m_scene);

    transition_image(
        cmd_buffer,
        m_forward_pass.get_output_image().image,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    transition_image(
        cmd_buffer,
        m_engine.get_swapchain().images[swapchain_image_idx],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    blit_image(
        cmd_buffer,
        m_forward_pass.get_output_image().image,
        m_forward_pass.get_output_image().extent,
        m_engine.get_swapchain().images[swapchain_image_idx],
        VkExtent3D{
            .width = m_engine.get_swapchain().extent.width,
            .height = m_engine.get_swapchain().extent.height,
            .depth = 1,
        }
    );

    m_imgui_pass.render(cmd_buffer, swapchain_image_idx);

    transition_image(
        cmd_buffer,
        m_engine.get_swapchain().images[swapchain_image_idx],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

    if (!m_engine.finish_frame(swapchain_image_idx))
    {
        spdlog::error("App::render_frame: failed to finish frame");
        return false;
    }

    return true;
}

void App::build_ui()
{
    ImGui::Begin(
        "Statistics",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize
    );
    {
        ImGui::Text("Frame Time (sec): %f", m_delta_time / 1000.0);
    }
    ImGui::End();

    ImGui::Begin(
        "Settings",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize
    );
    {
        ImGui::SeparatorText("General");
        ImGui::ColorEdit3("Background", m_scene.background_color.data());
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat3("Position", glm::value_ptr(m_scene.camera.eye), 0.1f);
        ImGui::SliderFloat("Pitch", &m_scene.camera.rotation.x, -90.0f, 90.0f);
        ImGui::SliderFloat("Yaw", &m_scene.camera.rotation.y, -180.0f, 180.0f);
    }
    ImGui::End();
}

[[nodiscard]] bool
App::create_mesh(std::span<Vertex> vertices, std::span<uint32_t> indices, Mesh &out_mesh)
{
    VkDeviceSize vertex_buffer_size = vertices.size() * sizeof(Vertex);
    VkDeviceSize index_buffer_size = indices.size() * sizeof(uint32_t);

    GPUBuffer transfer_buffer;
    if (!m_engine.create_buffer(
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            vertex_buffer_size + index_buffer_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            transfer_buffer
        ))
    {
        spdlog::error("App::create_mesh: failed to allocate transfer buffer");
        return false;
    }

    if (!m_engine.create_buffer(
            VMA_MEMORY_USAGE_GPU_ONLY,
            vertex_buffer_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            out_mesh.vertex_buffer
        ))
    {
        m_engine.destroy_buffer(transfer_buffer);
        spdlog::error("App::create_mesh: failed to allocate vertex buffer");
        return false;
    }

    if (!m_engine.create_buffer(
            VMA_MEMORY_USAGE_GPU_ONLY,
            index_buffer_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            out_mesh.index_buffer
        ))
    {
        m_engine.destroy_buffer(transfer_buffer);
        m_engine.destroy_buffer(out_mesh.vertex_buffer);
        spdlog::error("App::create_mesh: failed to allocate index buffer");
        return false;
    }

    std::memcpy(transfer_buffer.allocation_info.pMappedData, vertices.data(), vertex_buffer_size);
    std::memcpy(
        reinterpret_cast<uint8_t *>(transfer_buffer.allocation_info.pMappedData) +
            vertex_buffer_size,
        indices.data(),
        index_buffer_size
    );

    if (!m_engine.immediate_submit([&](VkCommandBuffer cmd_buffer) {
            VkBufferCopy copy_vertex_region{
                .srcOffset = 0,
                .dstOffset = 0,
                .size = vertex_buffer_size,
            };
            vkCmdCopyBuffer(
                cmd_buffer,
                transfer_buffer.buffer,
                out_mesh.vertex_buffer.buffer,
                1,
                &copy_vertex_region
            );

            VkBufferCopy copy_index_region{
                .srcOffset = vertex_buffer_size,
                .dstOffset = 0,
                .size = index_buffer_size,
            };
            vkCmdCopyBuffer(
                cmd_buffer,
                transfer_buffer.buffer,
                out_mesh.index_buffer.buffer,
                1,
                &copy_index_region
            );
        }))
    {
        m_engine.destroy_buffer(transfer_buffer);
        m_engine.destroy_buffer(out_mesh.vertex_buffer);
        m_engine.destroy_buffer(out_mesh.index_buffer);

        spdlog::error("App::create_mesh: failed to copy data to vertex buffer");
        return false;
    }

    VkBufferDeviceAddressInfo address_info = {};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = out_mesh.vertex_buffer.buffer;
    out_mesh.vertex_buffer_address = vkGetBufferDeviceAddress(m_engine.get_device(), &address_info);

    out_mesh.index_count = indices.size();

    m_engine.destroy_buffer(transfer_buffer);

    return true;
}

void App::destroy_mesh(Mesh &mesh)
{
    m_engine.destroy_buffer(mesh.vertex_buffer);
    m_engine.destroy_buffer(mesh.index_buffer);
}

[[nodiscard]] bool App::create_material_from_file(
    const std::string &diffuse_path, VkDescriptorSetLayout set_layout, Material &out_material
)
{
    if (!m_engine.create_image_from_file(diffuse_path, out_material.diffuse))
    {
        spdlog::error("App::create_material_from_file: failed to load diffuse image");
        return false;
    }

    VkDescriptorSetAllocateInfo set_info = {};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = m_engine.get_descriptor_pool();
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &set_layout;
    if (VkResult res =
            vkAllocateDescriptorSets(m_engine.get_device(), &set_info, &out_material.diffuse_set);
        res != VK_SUCCESS)
    {
        m_engine.destroy_image(out_material.diffuse);
        spdlog::error(
            "App::create_material_from_file: failed to allocate descriptor set: res = {}",
            static_cast<int>(res)
        );
        return false;
    }

    VkDescriptorImageInfo image_info{
        .sampler = m_sampler,
        .imageView = out_material.diffuse.view,
        .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = out_material.diffuse_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(m_engine.get_device(), 1, &write, 0, nullptr);

    return true;
}

void App::destroy_material(Material &material)
{
    m_engine.destroy_image(material.diffuse);
}

[[nodiscard]] bool App::create_scene_from_file(const std::string &path, Scene &out_scene)
{
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path.c_str(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs
    );
    if (scene == nullptr)
    {
        spdlog::error("App::create_scene_from_file: failed to load file");
        return false;
    }

    if (scene->mRootNode == nullptr)
    {
        spdlog::error("App::create_scene_from_file: file has no root node");
        return false;
    }

    for (size_t mat_idx = 0; mat_idx < scene->mNumMaterials; ++mat_idx)
    {
        Material material;

        const aiMaterial *ai_material = scene->mMaterials[mat_idx];
        if (ai_material->GetTextureCount(aiTextureType_DIFFUSE) == 0)
        {
            spdlog::warn(
                "App::create_scene_from_file: no diffuse texture for material #{} (`{}`)",
                mat_idx,
                ai_material->GetName().C_Str()
            );

            if (!create_material_from_file(
                    "../assets/white.png",
                    m_forward_pass.get_descriptor_set_layout(),
                    material
                ))
            {
                destroy_scene(out_scene);
                spdlog::error("App::create_scene_from_file: failed to create fallback material");
                return false;
            }
        }
        else
        {
            aiString diffuse_name;
            ai_material->GetTexture(aiTextureType_DIFFUSE, 0, &diffuse_name);

            std::string diffuse_path = std::string("../assets/sponza/") + diffuse_name.C_Str();
            if (!create_material_from_file(
                    diffuse_path,
                    m_forward_pass.get_descriptor_set_layout(),
                    material
                ))
            {
                destroy_scene(out_scene);
                spdlog::error(
                    "App::create_scene_from_file: failed to create material #{} (`{}`)",
                    mat_idx,
                    ai_material->GetName().C_Str()
                );
                return false;
            }
        }

        out_scene.materials.emplace_back(material);
    }

    for (size_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const aiMesh *ai_mesh = scene->mMeshes[mesh_idx];
        for (size_t vertex_idx = 0; vertex_idx < ai_mesh->mNumVertices; ++vertex_idx)
        {
            Vertex vertex{
                .position =
                    {
                        ai_mesh->mVertices[vertex_idx].x,
                        ai_mesh->mVertices[vertex_idx].y,
                        ai_mesh->mVertices[vertex_idx].z,
                    },
                .tex_coord_x = ai_mesh->mTextureCoords[0][vertex_idx].x,
                .normal =
                    {
                        ai_mesh->mNormals[vertex_idx].x,
                        ai_mesh->mNormals[vertex_idx].y,
                        ai_mesh->mNormals[vertex_idx].z,
                    },
                .tex_coord_y = ai_mesh->mTextureCoords[0][vertex_idx].y,
            };
            vertices.emplace_back(vertex);
        }

        for (size_t face_idx = 0; face_idx < ai_mesh->mNumFaces; ++face_idx)
        {
            const aiFace *face = &ai_mesh->mFaces[face_idx];
            for (size_t index_idx = 0; index_idx < face->mNumIndices; ++index_idx)
            {
                indices.emplace_back(static_cast<uint32_t>(face->mIndices[index_idx]));
            }
        }

        Mesh mesh;
        if (!create_mesh(vertices, indices, mesh))
        {
            destroy_scene(out_scene);
            spdlog::error("App::create_scene_from_file: failed to create mesh #{}", mesh_idx);
            return false;
        }
        mesh.material_idx = ai_mesh->mMaterialIndex;
        out_scene.meshes.emplace_back(mesh);
    }

    std::vector nodes_to_process{scene->mRootNode};
    while (!nodes_to_process.empty())
    {
        const aiNode *node = nodes_to_process.back();
        nodes_to_process.pop_back();

        for (size_t i = 0; i < node->mNumChildren; ++i)
        {
            nodes_to_process.emplace_back(node->mChildren[i]);
        }

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            out_scene.objects.emplace_back(Object{
                .mesh_idx = node->mMeshes[i],
            });
        }
    }

    spdlog::debug("scene has {} objects", out_scene.objects.size());

    return true;
}

void App::destroy_scene(Scene &scene)
{
    for (auto &mesh : scene.meshes)
    {
        destroy_mesh(mesh);
    }

    for (auto &material : scene.materials)
    {
        destroy_material(material);
    }

    scene.meshes.clear();
    scene.materials.clear();
    scene.objects.clear();
}
