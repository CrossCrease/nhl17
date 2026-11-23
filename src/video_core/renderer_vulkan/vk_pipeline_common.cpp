// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/static_vector.hpp>

#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

Pipeline::Pipeline(const Instance& instance_, Scheduler& scheduler_, DescriptorHeap& desc_heap_,
                   const Shader::Profile& profile_, vk::PipelineCache pipeline_cache,
                   bool is_compute_ /*= false*/)
    : instance{instance_}, scheduler{scheduler_}, desc_heap{desc_heap_}, profile{profile_},
      is_compute{is_compute_} {}

Pipeline::~Pipeline() = default;

void Pipeline::BindResources(DescriptorWrites& set_writes,
                             const Shader::PushData& push_data) const {
    const auto bind_point =
        IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
    const vk::PipelineLayout layout = *pipeline_layout;

    const auto stage_flags = IsCompute() ? vk::ShaderStageFlagBits::eCompute : AllGraphicsStageBits;
    scheduler.Record([layout, stage_flags, push_data](vk::CommandBuffer cmdbuf) {
        cmdbuf.pushConstants(layout, stage_flags, 0u, sizeof(push_data), &push_data);
    });

    // Bind descriptor set.
    if (set_writes.empty()) {
        return;
    }

    if (uses_push_descriptors) {
        PushDescriptors(bind_point, layout, set_writes);
        return;
    }

    const auto desc_set = desc_heap.Commit(*desc_layout);
    for (auto& set_write : set_writes) {
        set_write.dstSet = desc_set;
    }
    instance.GetDevice().updateDescriptorSets(set_writes, {});
    scheduler.Record([bind_point, layout, desc_set](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindDescriptorSets(bind_point, layout, 0, desc_set, {});
    });
}

void Pipeline::PushDescriptors(vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
                               std::span<const vk::WriteDescriptorSet> set_writes) const {
    if (!scheduler.IsThreaded()) {
        scheduler.CommandBuffer().pushDescriptorSetKHR(bind_point, layout, 0, set_writes);
        return;
    }
    // The writes point into the caller's descriptor info arrays, which change with the next
    // draw. Copy them, and the infos they point to, next to the recorded command.
    size_t num_images = 0;
    size_t num_buffers = 0;
    size_t num_views = 0;
    for (const auto& write : set_writes) {
        num_images += write.pImageInfo ? write.descriptorCount : 0;
        num_buffers += write.pBufferInfo ? write.descriptorCount : 0;
        num_views += write.pTexelBufferView ? write.descriptorCount : 0;
        ASSERT(write.pNext == nullptr);
    }
    scheduler.Reserve(set_writes.size_bytes() + num_images * sizeof(vk::DescriptorImageInfo) +
                      num_buffers * sizeof(vk::DescriptorBufferInfo) +
                      num_views * sizeof(vk::BufferView) + 64);
    auto* const writes = const_cast<vk::WriteDescriptorSet*>(
        scheduler.StreamCopy(set_writes.data(), set_writes.size()));
    for (size_t i = 0; i < set_writes.size(); ++i) {
        auto& write = writes[i];
        if (write.pImageInfo) {
            write.pImageInfo = scheduler.StreamCopy(write.pImageInfo, write.descriptorCount);
        }
        if (write.pBufferInfo) {
            write.pBufferInfo = scheduler.StreamCopy(write.pBufferInfo, write.descriptorCount);
        }
        if (write.pTexelBufferView) {
            write.pTexelBufferView =
                scheduler.StreamCopy(write.pTexelBufferView, write.descriptorCount);
        }
    }
    const u32 num_writes = static_cast<u32>(set_writes.size());
    scheduler.Record([bind_point, layout, writes, num_writes](vk::CommandBuffer cmdbuf) {
        cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, num_writes, writes);
    });
}

std::string Pipeline::GetDebugString() const {
    std::string stage_desc;
    for (const auto& stage : stages) {
        if (stage) {
            const auto shader_name = PipelineCache::GetShaderName(stage->hw_stage, stage->pgm_hash);
            if (stage_desc.empty()) {
                stage_desc = shader_name;
            } else {
                stage_desc = fmt::format("{},{}", stage_desc, shader_name);
            }
        }
    }
    return stage_desc;
}

} // namespace Vulkan
