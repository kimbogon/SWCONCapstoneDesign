/***************************************************************************
 # SampleCountPass.cpp
 **************************************************************************/
#include "SampleCountPass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
    // Shader file (relative to the Falcor render-pass search paths).
    const std::string kShaderFile = "RenderPasses/SampleCountPass/SampleCountPass.cs.slang";

    // Input channel – optional vbuffer (reserved for future depth-discontinuity heuristics).
    const std::string kInputVBuffer = "vbuffer";

    // Output channel name – must match the string PathTracer expects.
    const std::string kOutputSampleCount = "sampleCount";

    // Property keys.
    const std::string kPropHighSamples  = "highSamples";
    const std::string kPropLowSamples   = "lowSamples";
    const std::string kPropCentreRadius = "centreRadius";
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
        if      (key == kPropHighSamples)  mHighSamples  = value;
        else if (key == kPropLowSamples)   mLowSamples   = value;
        else if (key == kPropCentreRadius) mCentreRadius = value;
        else logWarning("SampleCountPass: Unknown property '{}'.", key);
    }
}

Properties SampleCountPass::getProperties() const
{
    Properties props;
    props[kPropHighSamples]  = mHighSamples;
    props[kPropLowSamples]   = mLowSamples;
    props[kPropCentreRadius] = mCentreRadius;
    return props;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

RenderPassReflection SampleCountPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Optional vbuffer input – not used in the simple heuristic but keeps the
    // pass compatible with graphs where it sits after VBufferRT.
    reflector.addInput(kInputVBuffer, "Visibility buffer (optional)")
             .flags(RenderPassReflection::Field::Flags::Optional);

    // R8Uint output texture.  The size is inherited from the render graph
    // (RenderPassReflection::Field::Type::Texture2D default behaviour).
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
    // Retrieve the output texture that the render graph has allocated for us.
    ref<Texture> pOutput = renderData.getTexture(kOutputSampleCount);
    FALCOR_ASSERT(pOutput);

    const uint32_t width  = pOutput->getWidth();
    const uint32_t height = pOutput->getHeight();

    // Bind shader variables.
    auto var = mpComputePass->getRootVar();
    var["CB"]["frameDim"]     = uint2(width, height);
    var["CB"]["highSamples"]  = mHighSamples;
    var["CB"]["lowSamples"]   = mLowSamples;
    var["CB"]["centreRadius"] = mCentreRadius;
    var["gOutput"]            = pOutput;

    // Dispatch one thread per pixel.
    mpComputePass->execute(pRenderContext, width, height);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SampleCountPass::renderUI(Gui::Widgets& widget)
{
    widget.var("High sample count", mHighSamples, 1u, 255u);
    widget.tooltip("Samples per pixel in the centre disc.");
    widget.var("Low sample count",  mLowSamples,  1u, 255u);
    widget.tooltip("Samples per pixel outside the centre disc.");
    widget.var("Centre radius (fraction)", mCentreRadius, 0.0f, 1.0f, 0.01f);
    widget.tooltip("Radius of the high-sample disc as a fraction of min(width, height).");
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SampleCountPass>();
}
