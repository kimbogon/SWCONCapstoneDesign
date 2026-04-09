add_plugin(ImportancePass)

target_sources(ImportancePass PRIVATE
    ImportancePass.cs.slang
    ImportancePass.cpp
    ImportancePass.h
)

target_copy_shaders(ImportancePass RenderPasses/ImportancePass)

target_source_group(ImportancePass "RenderPasses")
