// D3D12 backend implementation.

#include "d3d12_device.hpp"

#include <windows.h>

#include <array>
#include <memory>
#include <string>

#include "gpu/backend.hpp"  // ca::gpu::backend::register_gpu_backend

// Windows SDK headers under /W4 emit a handful of noise warnings; they are
// third-party headers and not under Calcium's warning policy.
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4251 4324 5204 4201 4464 4625 4626 6246)
#endif

namespace ca::gpu {

namespace {

constexpr std::uint32_t k_frames_in_flight = 2;
constexpr DXGI_FORMAT k_back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

Microsoft::WRL::ComPtr<IDXGIFactory2> create_dxgi_factory(bool enable_debug_layer) {
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    const UINT flags = enable_debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0;
    if (FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)))) {
        // Debug-layer creation fails when the graphics tools aren't installed;
        // retry without it so the backend still comes up.
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    }
    return factory;
}

void enable_d3d12_debug_layer() {
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug1> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
    }
#endif
}

struct AdapterChoice {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    bool is_hardware = false;
    std::string name;
};

AdapterChoice pick_adapter(const Microsoft::WRL::ComPtr<IDXGIFactory2>& factory) {
    // Highest-performance hardware adapter first; WARP as the honest fallback
    // (headless machines, RDP sessions, remote CI).
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(index, &adapter))) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        Microsoft::WRL::ComPtr<ID3D12Device> probe;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&probe)))) {
            const int wide_length = WideCharToMultiByte(CP_UTF8, 0, desc.Description,
                                                        -1, nullptr, 0, nullptr, nullptr);
            std::string name(static_cast<size_t>(wide_length > 0 ? wide_length : 0),
                             '\0');
            if (wide_length > 0) {
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name.data(),
                                    wide_length, nullptr, nullptr);
                name.pop_back();  // strip the trailing NUL
            }
            return {adapter, true, std::move(name)};
        }
    }

    // EnumWarpAdapter lives on IDXGIFactory4+ (dxgi1_3).
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
    if (factory.As(&factory4) == S_OK) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> warp;
        if (factory4->EnumWarpAdapter(IID_PPV_ARGS(&warp)) == S_OK) {
            return {warp, false, "Microsoft Basic Render Driver (WARP)"};
        }
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// D3D12Swapchain
// ---------------------------------------------------------------------------

D3D12Swapchain::D3D12Swapchain(
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain,
    Microsoft::WRL::ComPtr<ID3D12Device> device,
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap,
    std::uint32_t frames_in_flight, geometry::Size size_pixels)
    : swapchain_(std::move(swapchain)),
      device_(std::move(device)),
      rtv_heap_(std::move(rtv_heap)),
      frames_in_flight_(frames_in_flight),
      size_pixels_(size_pixels) {
    rtv_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    pacing_handle_ = swapchain_->GetFrameLatencyWaitableObject();
    for (std::uint32_t index = 0; index < frames_in_flight_; ++index) {
        device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fences_[index]));
        fence_events_[index] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    create_back_buffers();
}

void D3D12Swapchain::wait_for_buffer_idle(std::uint32_t index) {
    if (fences_[index] == nullptr || fence_values_[index] == 0) {
        return;  // the buffer has never been submitted; nothing to wait for
    }
    if (fences_[index]->GetCompletedValue() >= fence_values_[index]) {
        return;
    }
    fences_[index]->SetEventOnCompletion(fence_values_[index],
                                         fence_events_[index]);
    WaitForSingleObject(fence_events_[index], INFINITE);
}

void D3D12Swapchain::signal_buffer_submitted(std::uint32_t index,
                                             ID3D12CommandQueue* queue) {
    queue->Signal(fences_[index].Get(), ++fence_values_[index]);
}

void D3D12Swapchain::create_back_buffers() {
    for (std::uint32_t index = 0; index < frames_in_flight_; ++index) {
        back_buffers_[index].Reset();
        swapchain_->GetBuffer(index, IID_PPV_ARGS(&back_buffers_[index]));
        device_->CreateRenderTargetView(back_buffers_[index].Get(), nullptr,
                                        rtv(index));
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Swapchain::rtv(std::uint32_t index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * rtv_descriptor_size_;
    return handle;
}

void D3D12Swapchain::resize(geometry::Size size) {
    if (size.width <= 0.0f || size.height <= 0.0f || size == size_pixels_) {
        return;
    }
    // ResizeBuffers requires the back buffers unbound.
    for (auto& buffer : back_buffers_) {
        buffer.Reset();
    }
    const auto width = static_cast<UINT>(size.width);
    const auto height = static_cast<UINT>(size.height);
    if (FAILED(swapchain_->ResizeBuffers(frames_in_flight_, width, height,
                                         k_back_buffer_format,
                                         DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) {
        return;
    }
    size_pixels_ = size;
    create_back_buffers();
}

// ---------------------------------------------------------------------------
// D3D12RenderPass: one clear frame
// ---------------------------------------------------------------------------

class D3D12RenderPass final : public RenderPass {
public:
    D3D12RenderPass(Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue,
                    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list,
                    D3D12Swapchain* swapchain, std::uint32_t back_buffer_index,
                    core::Timestamp acquired_at)
        : queue_(std::move(queue)),
          list_(std::move(list)),
          swapchain_(swapchain),
          back_buffer_index_(back_buffer_index),
          acquired_at_(acquired_at) {}

    void end_and_present() override {
        list_->Close();
        ID3D12CommandList* const lists[] = {list_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        // SyncInterval = 1: scanout is locked to vsync.
        const HRESULT present_hr = swapchain_->native()->Present(1, 0);
        std::fprintf(stderr, "DBG present hr=%08x\n", (unsigned)present_hr);
        // The fence is signaled AFTER Present, in GPU queue order: it fires
        // only when the present has been consumed by scanout, so the next
        // frame's wait on this buffer guarantees both the allocator AND the
        // buffer itself are reusable (presenting a still-queued buffer twice
        // removes the device).
        swapchain_->signal_buffer_submitted(back_buffer_index_, queue_.Get());
        submitted_at_ = core::Timestamp::now();
    }

    [[nodiscard]] core::Timestamp acquired_at() const noexcept override {
        return acquired_at_;
    }

    [[nodiscard]] core::Timestamp submitted_at() const noexcept override {
        return submitted_at_;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
    D3D12Swapchain* swapchain_;
    std::uint32_t back_buffer_index_;
    core::Timestamp acquired_at_;
    core::Timestamp submitted_at_;
};

// ---------------------------------------------------------------------------
// D3D12Device
// ---------------------------------------------------------------------------

core::Result<std::unique_ptr<GraphicsDevice>> D3D12Device::create(
    const Configuration& configuration) {
    if (configuration.enable_debug_layer) {
        enable_d3d12_debug_layer();
    }
    auto factory = create_dxgi_factory(configuration.enable_debug_layer);
    if (factory == nullptr) {
        return core::Result<std::unique_ptr<GraphicsDevice>>{
            core::ErrorCode::backend_failure, "DXGI factory creation failed"};
    }

    const AdapterChoice choice = pick_adapter(factory);
    if (choice.adapter == nullptr) {
        return core::Result<std::unique_ptr<GraphicsDevice>>{
            core::ErrorCode::backend_failure,
            "no D3D12-capable adapter and no WARP fallback"};
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    const HRESULT device_hr = D3D12CreateDevice(choice.adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                IID_PPV_ARGS(&device));
    if (FAILED(device_hr)) {
        return core::Result<std::unique_ptr<GraphicsDevice>>{
            core::ErrorCode::backend_failure, "D3D12CreateDevice failed"};
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    const HRESULT queue_hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
    if (FAILED(queue_hr)) {
        return core::Result<std::unique_ptr<GraphicsDevice>>{
            core::ErrorCode::backend_failure, "command queue creation failed"};
    }

    return core::Result<std::unique_ptr<GraphicsDevice>>{
        std::unique_ptr<GraphicsDevice>{new D3D12Device(
            std::move(device), std::move(queue),
            AdapterInfo{.name = choice.name, .is_hardware = choice.is_hardware})}};
}

D3D12Device::~D3D12Device() {
    // Drain the queue so the device releases cleanly.
    if (queue_ != nullptr && device_ != nullptr) {
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        if (SUCCEEDED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(&fence)))) {
            queue_->Signal(fence.Get(), 1);
            if (fence->GetCompletedValue() < 1) {
                HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (event != nullptr) {
                    fence->SetEventOnCompletion(1, event);
                    WaitForSingleObject(event, INFINITE);
                    CloseHandle(event);
                }
            }
        }
    }
}

GraphicsDevice::AdapterInfo D3D12Device::adapter_info() const {
    return adapter_info_;
}

core::Result<std::unique_ptr<Swapchain>> D3D12Device::create_swapchain(
    gpu::WindowHandle window_handle, geometry::Size initial_size) {
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return core::Result<std::unique_ptr<Swapchain>>{
            core::ErrorCode::invalid_argument,
            "invalid window handle for swapchain"};
    }

    auto factory = create_dxgi_factory(false);
    if (factory == nullptr) {
        return core::Result<std::unique_ptr<Swapchain>>{
            core::ErrorCode::backend_failure, "DXGI factory creation failed"};
    }

    DXGI_SWAP_CHAIN_DESC1 desc{
        .Width = static_cast<UINT>(initial_size.width > 0 ? initial_size.width : 1),
        .Height = static_cast<UINT>(initial_size.height > 0 ? initial_size.height : 1),
        .Format = k_back_buffer_format,
        .Stereo = FALSE,
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = k_frames_in_flight,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT,
    };

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    const HRESULT swap_result =
        factory->CreateSwapChainForHwnd(queue_.Get(), hwnd, &desc, nullptr, nullptr,
                                        &swapchain1);
    if (FAILED(swap_result)) {
        return core::Result<std::unique_ptr<Swapchain>>{
            core::ErrorCode::backend_failure, "CreateSwapChainForHwnd failed"};
    }
    // Fullscreen transitions are the window manager's job; DXGI's automatic
    // handling steals focus and fights with SDL.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    if (FAILED(swapchain1.As(&swapchain3))) {
        return core::Result<std::unique_ptr<Swapchain>>{
            core::ErrorCode::backend_failure, "IDXGISwapChain3 unavailable"};
    }
    swapchain3->SetMaximumFrameLatency(k_frames_in_flight);

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = k_frames_in_flight,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
    if (FAILED(device_->CreateDescriptorHeap(&rtv_heap_desc,
                                             IID_PPV_ARGS(&rtv_heap)))) {
        return core::Result<std::unique_ptr<Swapchain>>{
            core::ErrorCode::backend_failure, "RTV heap creation failed"};
    }

    return core::Result<std::unique_ptr<Swapchain>>{
        std::unique_ptr<Swapchain>{new D3D12Swapchain(
            std::move(swapchain3), device_, std::move(rtv_heap),
            k_frames_in_flight, initial_size)}};
}

core::Result<std::unique_ptr<RenderPass>> D3D12Device::begin_clear_pass(
    Swapchain& swapchain, const float clear_color[4]) {
    D3D12Swapchain& d3d12_swapchain = static_cast<D3D12Swapchain&>(swapchain);

    // The buffer we are about to present: the flip-model index advances only
    // when the previous present is consumed by scanout, so this is the buffer
    // whose present is still (or last was) in flight.
    const std::uint32_t index =
        d3d12_swapchain.native()->GetCurrentBackBufferIndex();
    // Wait for THIS buffer's previous present to be consumed: the fence is
    // signaled on the GPU queue after Present, so it fires at scanout. This
    // paces the loop to the display cadence (docs/02-architecture.md §3.1)
    // and guarantees the buffer is safe to present again — the waitable's
    // "a slot is free" signal alone does not.
    d3d12_swapchain.wait_for_buffer_idle(index);
    // The wait released: the frame's vsync anchor and the start of its work.
    const core::Timestamp acquired_at = core::Timestamp::now();

    // One allocator + list per back buffer, protected by the same fence.
    static thread_local std::array<
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, k_frames_in_flight>
        allocators;
    static thread_local std::array<
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>, k_frames_in_flight>
        lists;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator = allocators[index];
    if (allocator == nullptr) {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&allocator)))) {
            return core::Result<std::unique_ptr<RenderPass>>{
                core::ErrorCode::backend_failure,
                "command allocator creation failed"};
        }
    }
    const HRESULT reset_hr = allocator->Reset();
    if (FAILED(reset_hr)) {
        std::fprintf(stderr, "DBG reset hr=%08x idx=%u\n", (unsigned)reset_hr, index);
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& list = lists[index];
    if (list == nullptr) {
        const HRESULT hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      allocator.Get(), nullptr,
                                                      IID_PPV_ARGS(&list));
        std::fprintf(stderr, "DBG list hr=%08x idx=%u\n", (unsigned)hr, index);
        if (FAILED(hr)) {
            return core::Result<std::unique_ptr<RenderPass>>{
                core::ErrorCode::backend_failure, "command list creation failed"};
        }
    }
    list->Reset(allocator.Get(), nullptr);

    ID3D12Resource* const back_buffer = d3d12_swapchain.back_buffer(index);
    const D3D12_RESOURCE_BARRIER to_rt{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition =
            {
                .pResource = back_buffer,
                .Subresource = 0,
                .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
            },
    };
    list->ResourceBarrier(1, &to_rt);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_swapchain.rtv(index);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);

    const D3D12_RESOURCE_BARRIER to_present{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition =
            {
                .pResource = back_buffer,
                .Subresource = 0,
                .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
            },
    };
    list->ResourceBarrier(1, &to_present);

    return core::Result<std::unique_ptr<RenderPass>>{
        std::unique_ptr<RenderPass>{new D3D12RenderPass(
            queue_, std::move(list), &d3d12_swapchain, index, acquired_at)}};
}

} // namespace ca::gpu

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

namespace ca::gpu::backend {

void register_d3d12_backend() {
    register_gpu_backend([](const GraphicsDevice::Configuration& configuration) {
        return D3D12Device::create(configuration);
    });
}

} // namespace ca::gpu::backend
