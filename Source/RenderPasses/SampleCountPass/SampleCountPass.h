/***************************************************************************
 # SampleCountPass.h
 #
 # A simple RenderPass that outputs a per-pixel R8Uint "sampleCount" texture
 # for use with PathTracer's adaptive (variable) sample count mode.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"

using namespace Falcor;

/**
 * SampleCountPass
 *
 * Outputs a 2D R8Uint texture named "sampleCount".
 * Each pixel stores the number of path-tracer samples that pixel should receive.
 *
 * Current heuristic (simple & deterministic):
 *   - Pixels within a configurable radius around the screen centre → highSamples
 *   - All other pixels                                              → lowSamples
 *
 * Both values are exposed as UI properties so they can be tuned at runtime
 * without shader recompilation.
 */
class SampleCountPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(SampleCountPass, "SampleCountPass",
                        "Outputs a per-pixel sample count texture for PathTracer adaptive sampling.");

    static ref<SampleCountPass> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<SampleCountPass>(pDevice, props);
    }

    SampleCountPass(ref<Device> pDevice, const Properties& props);

    // RenderPass interface
    virtual Properties          getProperties()  const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void                execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void                renderUI(Gui::Widgets& widget) override;

private:
    void parseProperties(const Properties& props);

    // -----------------------------------------------------------------------
    // Configuration (exposed as properties / UI)
    // -----------------------------------------------------------------------
    uint32_t mHighSamples   = 4;   ///< Sample count for "important" pixels.
    uint32_t mLowSamples    = 1;   ///< Sample count for the rest.
    float    mCentreRadius  = 0.25f; ///< Fraction of min(width,height) defining the centre disc.

    // -----------------------------------------------------------------------
    // GPU resources
    // -----------------------------------------------------------------------
    ref<ComputePass> mpComputePass; ///< The compute pass that fills the texture.
};
