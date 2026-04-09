/***************************************************************************
 # SampleCountPass.cpp
 #
 # Modified: importance-based sample count instead of centre-radius heuristic.
 **************************************************************************/
#include "SampleCountPass.h"

namespace
{
    // Shader file (relative to the Falcor render-pass search paths).
    const std::string kShaderFile = "RenderPasses/SampleCountPass/SampleCountPass.cs.slang";

    // Channel names — must match reflect().
    const std::string kInputImportance    = "importance";
    const std::string kOutputSampleCount  = "sampleCount";

    // Property keys.
    const std::string kPropThresholdLow  = "thresholdLow";
    const std::string kPropThresholdHigh = "thresholdHigh";
}

// ---------------------------------------------------------------------------
// Construction / Properties
// ---------------------------------------------------------------------------

SampleCountPass::SampleCountPass(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    parseProperties(props);

    // Create the compute pass.  The shader is compiled lazily on first execute
    // if mpComputePass is nullptr, but we create it up front so errors surface
    // at construction time rather than at first render.
    mpComputePass = ComputePass::create(mpDevice, kShaderFile, "main");
}

void SampleCountPass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kPropThresholdLow)       mThresholdLow  = value;
        else if (key == kPropThresholdHigh) mThresholdHigh = value;
        else logWarning("SampleCountPass: Unknown property '{}'.", key);
    }
}

Properties SampleCountPass::getProperties() const
{
    Properties props;
    props[kPropThresholdLow]  = mThresholdLow;
    props[kPropThresholdHigh] = mThresholdHigh;
    return props;
}

// ---------------------------------------------------------------------------
// Reflect
// ---------------------------------------------------------------------------

RenderPassReflection SampleCountPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Importance input from ImportancePass (R32Float).
    reflector.addInput(kInputImportance, "Per-pixel importance [0,1] (R32Float)")
        .format(ResourceFormat::R32Float);

    // R8Uint output — PathTracer reads this as Texture2D<uint>.
    reflector.addOutput(kOutputSampleCount, "Per-pixel sample count (R8Uint)")
        .format(ResourceFormat::R8Uint)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    return reflector;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void SampleCountPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Retrieve the input/output texture that the render graph has allocated for us.
    ref<Texture> pImportance = renderData.getTexture(kInputImportance);
    ref<Texture> pOutput     = renderData.getTexture(kOutputSampleCount);
    FALCOR_ASSERT(pOutput);

    const uint32_t w = pOutput->getWidth();
    const uint32_t h = pOutput->getHeight();

    // Bind shader variables.
    auto var = mpComputePass->getRootVar();
    var["CB"]["frameDim"]       = uint2(w, h);
    var["CB"]["thresholdLow"]   = mThresholdLow;
    var["CB"]["thresholdHigh"]  = mThresholdHigh;
    var["gImportance"]          = pImportance;
    var["gOutput"]              = pOutput;

    /*
    const uint32_t groupSize = 16;
    mpComputePass->execute(pRenderContext, { div_round_up(w, groupSize), div_round_up(h, groupSize), 1u });
    */

    
    // Dispatch one thread per pixel.
    mpComputePass->execute(pRenderContext, w, h);
    
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SampleCountPass::renderUI(Gui::Widgets& widget)
{
    widget.var("Low threshold",  mThresholdLow,  0.0f, 1.0f, 0.01f);
    widget.tooltip("importance < this => 1 spp");
    widget.var("High threshold", mThresholdHigh, 0.0f, 1.0f, 0.01f);
    widget.tooltip("importance >= this => 4 spp, else 2 spp");
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SampleCountPass>();
}
