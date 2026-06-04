add_plugin(OverlayPass)

target_sources(OverlayPass PRIVATE
    OverlayPass.cpp
    OverlayPass.h
)

target_copy_shaders(OverlayPass RenderPasses/OverlayPass)

target_source_group(OverlayPass "RenderPasses")
