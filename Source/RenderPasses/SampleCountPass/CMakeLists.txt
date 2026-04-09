add_plugin(SampleCountPass)

target_sources(SampleCountPass PRIVATE
    SampleCountPass.cs.slang
    SampleCountPass.cpp
    SampleCountPass.h
)

target_copy_shaders(SampleCountPass RenderPasses/SampleCountPass)

target_source_group(SampleCountPass "RenderPasses")
