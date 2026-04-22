/***************************************************************************
 # SampleCountPass.h
 #
 # Modified: now reads an importance texture (R32Float) instead of using
 # a centre-radius heuristic.  Sample count is 1, 2, or 4 based on
 # importance thresholds.
 #
 # [디버그 출력 추가]
 # sampleCountVis 채널: 픽셀별 sample count (1/2/4 spp) 를 정규화된
 # RGBA8Unorm 텍스처로 시각화하여 RenderGraph output으로 노출한다.
 #   1 spp → 어두운 색 (0.25)
 #   2 spp → 중간 색  (0.50)
 #   4 spp → 밝은 색  (1.00)
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
