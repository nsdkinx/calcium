// D3D12 backend — internal, never public (docs/02-architecture.md §6).
//
// Implements GraphicsDevice/Swapchain/RenderPass with the canonical D3D12
// waitable-swapchain frame loop:
//
//   wait on the frame-latency waitable  → a back buffer is free (vsync-paced)
//   reset the per-buffer command allocator
//   record the clear
//   execute + Present(1, 0)             → vsync-locked scanout
//
// The waitable object paces the compositor to the display; Present(1) locks
// scanout to vsync. Both are the documented "extrapolate from cadence" path
// for presentation prediction (the timing model lives in the SDL3 backend;
// see src/backends/platform_sdl3).

#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "calcium/gpu/graphics_device.hpp"
#include "calcium/gpu/render_pass.hpp"
#include "calcium/gpu/swapchain.hpp"

namespace ca::gpu::backend {
// Called by the umbrella (src/calcium.cpp) at startup.
void register_d3d12_backend();
} // namespace ca::gpu::backend

namespace ca::gpu {

// The backend's windowing surface: an IDXGISwapChain3 with a waitable object.
// Owned by the compositor, used on the compositor thread only.
class D3D12Swapchain final : public Swapchain {
public:
    D3D12Swapchain(Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain,
                   Microsoft::WRL::ComPtr<ID3D12Device> device,
                   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap,
                   std::uint32_t frames_in_flight, geometry::Size size_pixels);

    [[nodiscard]] geometry::Size size() const override { return size_pixels_; }
    void resize(geometry::Size size) override;
    [[nodiscard]] std::uint32_t frames_in_flight() const noexcept override {
        return frames_in_flight_;
    }

    [[nodiscard]] IDXGISwapChain3* native() const noexcept { return swapchain_.Get(); }
    [[nodiscard]] ID3D12Device* device() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D12Resource* back_buffer(std::uint32_t index) const noexcept {
        return back_buffers_[index].Get();
    }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rtv(std::uint32_t index) const noexcept;
    /// The frame-latency waitable object: signaled when a back buffer is free.
    [[nodiscard]] void* pacing_handle() const noexcept {
        return pacing_handle_;
    }

    /// Waits until the GPU has finished the work recorded for `index`'s
    /// previous frame (the waitable guarantees the buffer is free; the fence
    /// guarantees the command allocator is reusable). Called by the frame
    /// loop's pass.
    void wait_for_buffer_idle(std::uint32_t index);
    /// Signals that the frame recorded for `index` has been submitted (must
    /// be queued after that frame's ExecuteCommandLists).
    void signal_buffer_submitted(std::uint32_t index,
                                 ID3D12CommandQueue* queue);

private:
    void create_back_buffers();

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> back_buffers_[3];
    Microsoft::WRL::ComPtr<ID3D12Fence> fences_[3];
    HANDLE fence_events_[3] = {};
    std::uint64_t fence_values_[3] = {};
    void* pacing_handle_ = nullptr;
    std::uint32_t frames_in_flight_ = 2;
    std::uint32_t rtv_descriptor_size_ = 0;
    geometry::Size size_pixels_;
};

class D3D12Device final : public GraphicsDevice {
public:
    static core::Result<std::unique_ptr<GraphicsDevice>> create(
        const Configuration& configuration);

    ~D3D12Device() override;

    [[nodiscard]] AdapterInfo adapter_info() const override;
    [[nodiscard]] std::string_view api_name() const noexcept override {
        return "d3d12";
    }

    [[nodiscard]] core::Result<std::unique_ptr<Swapchain>> create_swapchain(
        gpu::WindowHandle window_handle, geometry::Size initial_size) override;

    [[nodiscard]] core::Result<std::unique_ptr<RenderPass>> begin_clear_pass(
        Swapchain& swapchain, const float clear_color[4]) override;

private:
    D3D12Device(Microsoft::WRL::ComPtr<ID3D12Device> device,
                Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue,
                AdapterInfo adapter_info)
        : device_(std::move(device)),
          queue_(std::move(queue)),
          adapter_info_(std::move(adapter_info)) {}

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    AdapterInfo adapter_info_;
};

} // namespace ca::gpu
