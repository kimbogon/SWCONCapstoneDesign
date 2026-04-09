/***************************************************************************
 # SampleCountPass.h
 #
 # Modified: now reads an importance texture (R32Float) instead of using
 # a centre-radius heuristic.  Sample count is 1, 2, or 4 based on
 # importance thresholds.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
using namespace Falcor;

class SampleCountPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(SampleCountPass, "SampleCountPass", "Outputs a per-pixel sample count texture for PathTracer adaptive sampling.");
    static ref<SampleCountPass> create(ref<Device> pDevice, const Properties& props) { return make_ref<SampleCountPass>(pDevice, props); }
    SampleCountPass(ref<Device> pDevice, const Properties& props);

    // RenderPass interface
    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;

private:
    void parseProperties(const Properties& props);

    // -----------------------------------------------------------------------
    // Configuration (exposed as properties / UI)
    // -----------------------------------------------------------------------
    float mThresholdLow  = 0.33f;  ///< importance < this -> 1 spp
    float mThresholdHigh = 0.66f;  ///< importance >= this -> 4 spp, else 2 spp

    // -----------------------------------------------------------------------
    // GPU resources
    // -----------------------------------------------------------------------
    ref<ComputePass> mpComputePass; ///< The compute pass that fills the texture.
};
