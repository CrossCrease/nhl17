// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <vector>

#include "common/assert.h"

#include "common/interval_set.h"
#include "common/unique_function.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/amdgpu/regs_primitive.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"
#include "vulkan/vulkan.hpp"

namespace tracy {
class VkCtxScope;
}

namespace Vulkan {

class Instance;

struct RenderAttachment {
    vk::ImageView image_view;
    vk::ImageLayout image_layout;
    std::array<u32, 4> clear_value;
    union {
        u32 is_clear;
        struct {
            bool has_depth;
            bool depth_clear;
            bool has_stencil;
            bool stencil_clear;
        };
    };
};
static_assert(std::has_unique_object_representations_v<RenderAttachment>);

struct RenderState {
    std::array<RenderAttachment, 8> color_attachments;
    RenderAttachment depth_stencil_attachment;
    u16 width;
    u16 height;
    u16 num_layers;
    u16 num_color_attachments;

    bool operator==(const RenderState& other) const noexcept {
        return std::memcmp(this, &other, sizeof(RenderState)) == 0;
    }
};
static_assert(std::has_unique_object_representations_v<RenderState>);

struct SubmitInfo {
    std::array<vk::Semaphore, 4> wait_semas;
    std::array<u64, 4> wait_ticks;
    std::array<vk::Semaphore, 4> signal_semas;
    std::array<u64, 4> signal_ticks;
    vk::Fence fence;
    u32 num_wait_semas;
    u32 num_signal_semas;

    void AddWait(vk::Semaphore semaphore, u64 tick = 1) {
        wait_semas[num_wait_semas] = semaphore;
        wait_ticks[num_wait_semas++] = tick;
    }

    void AddSignal(vk::Semaphore semaphore, u64 tick = 1) {
        signal_semas[num_signal_semas] = semaphore;
        signal_ticks[num_signal_semas++] = tick;
    }

    void AddSignal(vk::Fence fence) {
        this->fence = fence;
    }
};

using Viewports = boost::container::static_vector<vk::Viewport, AmdGpu::NUM_VIEWPORTS>;
using Scissors = boost::container::static_vector<vk::Rect2D, AmdGpu::NUM_VIEWPORTS>;
using ColorWriteMasks = std::array<vk::ColorComponentFlags, AmdGpu::NUM_COLOR_BUFFERS>;
struct StencilOps {
    vk::StencilOp fail_op{};
    vk::StencilOp pass_op{};
    vk::StencilOp depth_fail_op{};
    vk::CompareOp compare_op{};

    bool operator==(const StencilOps& other) const {
        return fail_op == other.fail_op && pass_op == other.pass_op &&
               depth_fail_op == other.depth_fail_op && compare_op == other.compare_op;
    }
};
struct DynamicState {
    struct {
        bool viewports : 1;
        bool scissors : 1;

        bool depth_test_enabled : 1;
        bool depth_write_enabled : 1;
        bool depth_compare_op : 1;

        bool depth_bounds_test_enabled : 1;
        bool depth_bounds : 1;

        bool depth_bias_enabled : 1;
        bool depth_bias : 1;

        bool stencil_test_enabled : 1;
        bool stencil_front_ops : 1;
        bool stencil_front_reference : 1;
        bool stencil_front_write_mask : 1;
        bool stencil_front_compare_mask : 1;
        bool stencil_back_ops : 1;
        bool stencil_back_reference : 1;
        bool stencil_back_write_mask : 1;
        bool stencil_back_compare_mask : 1;

        bool primitive_restart_enable : 1;
        bool rasterizer_discard_enable : 1;
        bool cull_mode : 1;
        bool front_face : 1;

        bool blend_constants : 1;
        bool color_write_masks : 1;
        bool line_width : 1;
        bool feedback_loop_enabled : 1;
    } dirty_state{};
    u32 generation{};

    Viewports viewports{};
    Scissors scissors{};

    bool depth_test_enabled{};
    bool depth_write_enabled{};
    vk::CompareOp depth_compare_op{};

    bool depth_bounds_test_enabled{};
    float depth_bounds_min{};
    float depth_bounds_max{};

    bool depth_bias_enabled{};
    float depth_bias_constant{};
    float depth_bias_clamp{};
    float depth_bias_slope{};

    bool stencil_test_enabled{};
    StencilOps stencil_front_ops{};
    u32 stencil_front_reference{};
    u32 stencil_front_write_mask{};
    u32 stencil_front_compare_mask{};
    StencilOps stencil_back_ops{};
    u32 stencil_back_reference{};
    u32 stencil_back_write_mask{};
    u32 stencil_back_compare_mask{};

    bool primitive_restart_enable{};
    bool rasterizer_discard_enable{};
    vk::CullModeFlags cull_mode{};
    vk::FrontFace front_face{};

    std::array<float, 4> blend_constants{};
    ColorWriteMasks color_write_masks{};
    float line_width{};
    vk::ImageAspectFlags feedback_loop_enabled{};

    /// Commits the dynamic state to the provided command buffer.
    void Commit(const Instance& instance, const vk::CommandBuffer& cmdbuf);

    /// Clears the dirty flags exactly as Commit does, without recording anything. Used after a
    /// copy of this state was handed to the recording thread.
    void MarkCommitted(const Instance& instance);

    template <typename Cmd>
    void CommitTo(const Instance& instance, const Cmd& cmdbuf);

    /// Returns true when Commit would have anything to record.
    bool IsDirty() const noexcept {
        static_assert(sizeof(dirty_state) <= sizeof(u64));
        u64 bits = 0;
        std::memcpy(&bits, &dirty_state, sizeof(dirty_state));
        return bits != 0;
    }

    /// Invalidates all dynamic state to be flushed into the next command buffer.
    void Invalidate() {
        std::memset(&dirty_state, 0xFF, sizeof(dirty_state));
        ++generation;
    }

    /// Changes on every Invalidate(): a new command buffer, or another pass that bound its own
    /// pipeline. State cached by the rasterizer (bound pipeline, vertex input) is only reused
    /// while this value is unchanged.
    u32 Generation() const noexcept {
        return generation;
    }

    void SetViewports(const Viewports& viewports_) {
        if (!std::ranges::equal(viewports, viewports_)) {
            viewports = viewports_;
            dirty_state.viewports = true;
        }
    }

    void SetScissors(const Scissors& scissors_) {
        if (!std::ranges::equal(scissors, scissors_)) {
            scissors = scissors_;
            dirty_state.scissors = true;
        }
    }

    void SetDepthTestEnabled(const bool enabled) {
        if (depth_test_enabled != enabled) {
            depth_test_enabled = enabled;
            dirty_state.depth_test_enabled = true;
        }
    }

    void SetDepthWriteEnabled(const bool enabled) {
        if (depth_write_enabled != enabled) {
            depth_write_enabled = enabled;
            dirty_state.depth_write_enabled = true;
        }
    }

    void SetDepthCompareOp(const vk::CompareOp compare_op) {
        if (depth_compare_op != compare_op) {
            depth_compare_op = compare_op;
            dirty_state.depth_compare_op = true;
        }
    }

    void SetDepthBoundsTestEnabled(const bool enabled) {
        if (depth_bounds_test_enabled != enabled) {
            depth_bounds_test_enabled = enabled;
            dirty_state.depth_bounds_test_enabled = true;
        }
    }

    void SetDepthBounds(const float min, const float max) {
        if (depth_bounds_min != min || depth_bounds_max != max) {
            depth_bounds_min = min;
            depth_bounds_max = max;
            dirty_state.depth_bounds = true;
        }
    }

    void SetDepthBiasEnabled(const bool enabled) {
        if (depth_bias_enabled != enabled) {
            depth_bias_enabled = enabled;
            dirty_state.depth_bias_enabled = true;
        }
    }

    void SetDepthBias(const float constant, const float clamp, const float slope) {
        if (depth_bias_constant != constant || depth_bias_clamp != clamp ||
            depth_bias_slope != slope) {
            depth_bias_constant = constant;
            depth_bias_clamp = clamp;
            depth_bias_slope = slope;
            dirty_state.depth_bias = true;
        }
    }

    void SetStencilTestEnabled(const bool enabled) {
        if (stencil_test_enabled != enabled) {
            stencil_test_enabled = enabled;
            dirty_state.stencil_test_enabled = true;
        }
    }

    void SetStencilOps(const StencilOps& front_ops, const StencilOps& back_ops) {
        if (stencil_front_ops != front_ops) {
            stencil_front_ops = front_ops;
            dirty_state.stencil_front_ops = true;
        }
        if (stencil_back_ops != back_ops) {
            stencil_back_ops = back_ops;
            dirty_state.stencil_back_ops = true;
        }
    }

    void SetStencilReferences(const u32 front_reference, const u32 back_reference) {
        if (stencil_front_reference != front_reference) {
            stencil_front_reference = front_reference;
            dirty_state.stencil_front_reference = true;
        }
        if (stencil_back_reference != back_reference) {
            stencil_back_reference = back_reference;
            dirty_state.stencil_back_reference = true;
        }
    }

    void SetStencilWriteMasks(const u32 front_write_mask, const u32 back_write_mask) {
        if (stencil_front_write_mask != front_write_mask) {
            stencil_front_write_mask = front_write_mask;
            dirty_state.stencil_front_write_mask = true;
        }
        if (stencil_back_write_mask != back_write_mask) {
            stencil_back_write_mask = back_write_mask;
            dirty_state.stencil_back_write_mask = true;
        }
    }

    void SetStencilCompareMasks(const u32 front_compare_mask, const u32 back_compare_mask) {
        if (stencil_front_compare_mask != front_compare_mask) {
            stencil_front_compare_mask = front_compare_mask;
            dirty_state.stencil_front_compare_mask = true;
        }
        if (stencil_back_compare_mask != back_compare_mask) {
            stencil_back_compare_mask = back_compare_mask;
            dirty_state.stencil_back_compare_mask = true;
        }
    }

    void SetPrimitiveRestartEnabled(const bool enabled) {
        if (primitive_restart_enable != enabled) {
            primitive_restart_enable = enabled;
            dirty_state.primitive_restart_enable = true;
        }
    }

    void SetCullMode(const vk::CullModeFlags cull_mode_) {
        if (cull_mode != cull_mode_) {
            cull_mode = cull_mode_;
            dirty_state.cull_mode = true;
        }
    }

    void SetFrontFace(const vk::FrontFace front_face_) {
        if (front_face != front_face_) {
            front_face = front_face_;
            dirty_state.front_face = true;
        }
    }

    void SetBlendConstants(const std::array<float, 4> blend_constants_) {
        if (blend_constants != blend_constants_) {
            blend_constants = blend_constants_;
            dirty_state.blend_constants = true;
        }
    }

    void SetRasterizerDiscardEnabled(const bool enabled) {
        if (rasterizer_discard_enable != enabled) {
            rasterizer_discard_enable = enabled;
            dirty_state.rasterizer_discard_enable = true;
        }
    }

    void SetColorWriteMasks(const ColorWriteMasks& color_write_masks_) {
        if (!std::ranges::equal(color_write_masks, color_write_masks_)) {
            color_write_masks = color_write_masks_;
            dirty_state.color_write_masks = true;
        }
    }

    void SetLineWidth(const float width) {
        if (line_width != width) {
            line_width = width;
            dirty_state.line_width = true;
        }
    }

    void SetAttachmentFeedbackLoopEnabled(const vk::ImageAspectFlags enabled) {
        if (feedback_loop_enabled != enabled) {
            feedback_loop_enabled = enabled;
            dirty_state.feedback_loop_enabled = true;
        }
    }
};

/// A block of recorded commands that the recording thread replays into a Vulkan command buffer.
/// Commands and the data they point to are stored inline, so recording allocates nothing.
class CommandChunk final {
public:
    static constexpr size_t Capacity = 256 * 1024;

    CommandChunk() = default;
    ~CommandChunk() {
        Reset();
    }
    CommandChunk(const CommandChunk&) = delete;
    CommandChunk& operator=(const CommandChunk&) = delete;

    /// Number of bytes still free, ignoring alignment.
    [[nodiscard]] size_t Free() const noexcept {
        return Capacity - offset;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return first == nullptr;
    }

    [[nodiscard]] u32 Count() const noexcept {
        return count;
    }

    /// Returns aligned storage inside the chunk, or nullptr when it does not fit.
    void* Allocate(size_t size, size_t align) noexcept {
        const size_t start = (offset + align - 1) & ~(align - 1);
        if (start + size > Capacity) {
            return nullptr;
        }
        offset = start + size;
        return storage.get() + start;
    }

    template <typename F>
    bool Record(F&& func) {
        using Type = TypedCommand<std::decay_t<F>>;
        void* const ptr = Allocate(sizeof(Type), alignof(Type));
        if (!ptr) {
            return false;
        }
        Command* const command = new (ptr) Type(std::forward<F>(func));
        if (last) {
            last->next = command;
        } else {
            first = command;
        }
        last = command;
        ++count;
        return true;
    }

    void Execute(vk::CommandBuffer cmdbuf) const {
        for (const Command* command = first; command; command = command->next) {
            command->Execute(cmdbuf);
        }
    }

    void Reset() noexcept {
        for (Command* command = first; command;) {
            Command* const next = command->next;
            command->~Command();
            command = next;
        }
        first = last = nullptr;
        offset = 0;
        count = 0;
    }

    vk::CommandBuffer target{};

private:
    struct Command {
        virtual ~Command() = default;
        virtual void Execute(vk::CommandBuffer cmdbuf) const = 0;
        Command* next{};
    };

    template <typename F>
    struct TypedCommand final : Command {
        template <typename G>
        explicit TypedCommand(G&& func_) : func{std::forward<G>(func_)} {}
        void Execute(vk::CommandBuffer cmdbuf) const override {
            func(cmdbuf);
        }
        F func;
    };

    std::unique_ptr<u8[]> storage{new u8[Capacity]};
    size_t offset{};
    Command* first{};
    Command* last{};
    u32 count{};
};

using SessionFunc = Common::UniqueFunction<void>;
using SubmitFunc = Common::UniqueFunction<void, SubmitInfo&>;

class Scheduler {
public:
    /// With threaded_recording, commands recorded through Record() are replayed into the Vulkan
    /// command buffer by a separate thread, so the driver's recording cost leaves the caller's
    /// thread. CommandBuffer() first waits for that thread, so direct recording stays correct.
    explicit Scheduler(const Instance& instance, bool threaded_recording = false);
    ~Scheduler();

    /// Sends the current execution context to the GPU
    /// and increments the scheduler timeline semaphore.
    void Flush(SubmitInfo& info);

    /// Sends the current execution context to the GPU
    /// and increments the scheduler timeline semaphore.
    void Flush();

    /// Sends the current execution context to the GPU and waits for it to complete.
    void Finish();

    /// Waits for the given tick to trigger on the GPU.
    void Wait(u64 tick);

    /// Attempts to execute operations whose tick the GPU has caught up with.
    void PopPendingOperations();

    /// Starts a new rendering scope with provided state.
    void BeginRendering(const RenderState& new_state);

    /// Ends current rendering scope.
    void EndRendering();

    /// Starts a new session.
    void BeginSession();

    /// Returns the current command buffer used for uploads.
    vk::CommandBuffer UploadCommandBuffer();

    /// Sets a function to be called on every session finalization.
    void SetSessionCallback(SessionFunc&& on_session) {
        this->on_session = std::move(on_session);
    }

    /// Sets a function to be called on every scheduler submission.
    void SetSubmitCallback(SubmitFunc&& on_submit) {
        this->on_submit = std::move(on_submit);
    }

    /// Returns the current render state.
    const RenderState& GetRenderState() const {
        return render_state;
    }

    /// Returns the current pipeline dynamic state tracking.
    DynamicState& GetDynamicState() {
        return dynamic_state;
    }

    /// Returns the current command buffer for direct recording. Waits until every command
    /// passed to Record() has been recorded into it.
    vk::CommandBuffer CommandBuffer() {
        if (threaded) {
            SyncRecording();
        }
        return sessions.back().primary;
    }

    /// Whether Record() hands commands to the recording thread.
    [[nodiscard]] bool IsThreaded() const noexcept {
        return threaded;
    }

    /// Records a command. func(vk::CommandBuffer) runs later on the recording thread, in order
    /// with everything else recorded on this scheduler; it must capture what it uses by value
    /// or point to data copied with StreamCopy(). Without threaded recording it runs now.
    template <typename F>
    void Record(F&& func) {
        if (!threaded) {
            func(sessions.back().primary);
            return;
        }
        if (!chunk->Record(func)) {
            DispatchChunk();
            const bool recorded = chunk->Record(func);
            ASSERT(recorded);
        }
        recorded_commands.store(recorded_commands.load(std::memory_order_relaxed) + 1,
                                std::memory_order_relaxed);
        if (chunk->Count() >= DispatchThreshold) {
            DispatchChunk();
        }
    }

    /// Makes sure the next bytes of StreamCopy() data and the Record() call that uses them land
    /// in the same chunk. Call it before StreamCopy().
    void Reserve(size_t bytes) {
        if (threaded && chunk->Free() < bytes + 2048) {
            DispatchChunk();
        }
    }

    /// Copies data next to the recorded commands; the pointer stays valid until the next
    /// Record() call has run. Without threaded recording the original data is returned.
    template <typename T>
    const T* StreamCopy(const T* data, size_t count) {
        if (!threaded || count == 0) {
            return data;
        }
        void* const ptr = chunk->Allocate(sizeof(T) * count, alignof(T));
        ASSERT_MSG(ptr, "Recording data does not fit; call Reserve first");
        std::memcpy(ptr, data, sizeof(T) * count);
        return static_cast<const T*>(ptr);
    }

    /// Waits until the recording thread has recorded everything passed to Record().
    void SyncRecording();

    /// Counters for frame statistics: direct recordings that had to wait for the recording
    /// thread, and commands recorded through Record(), counted since start.
    struct RecordingStats {
        u64 syncs;
        u64 commands;
    };
    RecordingStats GetRecordingStats() const noexcept {
        return {sync_count.load(std::memory_order_relaxed),
                recorded_commands.load(std::memory_order_relaxed)};
    }

    /// Returns the current command buffer tick.
    [[nodiscard]] u64 CurrentTick() const noexcept {
        return work_semaphore.CurrentTick();
    }

    /// Returns true when a tick has been triggered by the GPU.
    [[nodiscard]] bool IsFree(u64 tick) noexcept {
        if (work_semaphore.IsFree(tick)) {
            return true;
        }
        work_semaphore.Refresh();
        return work_semaphore.IsFree(tick);
    }

    /// Returns the scheduler timeline semaphore.
    [[nodiscard]] Semaphore* GetWorkSemaphore() noexcept {
        return &work_semaphore;
    }

    /// Defers an operation until the gpu has reached the current cpu tick.
    /// Will be run when submitting or calling PopPendingOperations.
    void DeferOperation(Common::UniqueFunction<void>&& func) {
        std::unique_lock lk(pending_ops_mutex);
        pending_ops.emplace(std::move(func), CurrentTick());
    }

    /// Defers an operation until the gpu has reached the current cpu tick.
    /// Runs as soon as possible in another thread.
    void DeferPriorityOperation(Common::UniqueFunction<void>&& func) {
        {
            std::unique_lock lk(priority_pending_ops_mutex);
            priority_pending_ops.emplace(std::move(func), CurrentTick());
        }
        priority_pending_ops_cv.notify_one();
    }

    static std::mutex submit_mutex;

private:
    void EndSession();

    void SubmitExecution(SubmitInfo& info);

    void PriorityPendingOpsThread(std::stop_token stoken);

    void DispatchChunk();

    void RecordingThread(std::stop_token stoken);

private:
    const Instance& instance;
    Semaphore work_semaphore;
    CommandPool command_pool;
    DynamicState dynamic_state;
    SessionFunc on_session{};
    SubmitFunc on_submit{};
    struct Session {
        vk::CommandBuffer upload{};
        vk::CommandBuffer primary{};
    };
    std::vector<Session> sessions;
    std::condition_variable_any event_cv;
    struct PendingOp {
        Common::UniqueFunction<void> callback;
        u64 gpu_tick;
    };
    std::queue<PendingOp> pending_ops;
    std::recursive_mutex pending_ops_mutex;
    std::queue<PendingOp> priority_pending_ops;
    std::mutex priority_pending_ops_mutex;
    std::condition_variable_any priority_pending_ops_cv;
    std::jthread priority_pending_ops_thread;
    RenderState render_state;
    bool is_rendering = false;
    tracy::VkCtxScope* profiler_scope{};

    // Threaded recording. Commands are handed over in chunks of up to this many.
    static constexpr u32 DispatchThreshold = 384;
    bool threaded{};
    CommandPool upload_pool;
    std::unique_ptr<CommandChunk> chunk;
    std::mutex chunk_mutex;
    std::condition_variable_any chunk_cv;
    std::queue<std::unique_ptr<CommandChunk>> chunk_queue;
    std::vector<std::unique_ptr<CommandChunk>> free_chunks;
    u64 dispatched_chunks{};
    std::atomic<u64> completed_chunks{};
    std::atomic<bool> waiting_for_recording{};
    std::mutex done_mutex;
    std::condition_variable done_cv;
    std::atomic<u64> sync_count{};
    std::atomic<u64> recorded_commands{};
    std::jthread recording_thread;
};

} // namespace Vulkan
