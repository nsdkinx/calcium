# D3D12 GPU backend (M1, Windows).

add_library(gpu_d3d12 STATIC
    d3d12_device.cpp
)

target_link_libraries(gpu_d3d12
    PUBLIC d3d12 dxgi calcium_core calcium_geometry calcium_platform calcium_gpu
)
calcium_configure_module(gpu_d3d12)
