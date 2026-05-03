/***************************************************************************
 # SampleCountPass.h
 #
 # [변경 내용] 고정 임계값 방식(thresholdLow / thresholdHigh) 제거.
 # 대신 평균 중요도(Avg_Imp) 기반 샘플 분배로 교체.
 #
 # 알고리즘:
 #   S_p = clamp(round(I_p / Avg_Imp * sTarget), 1, maxSamples)
 #
 # GPU Reduction 구조 (3-pass dispatch):
 #   reduceMain   : 16×16 타일별 부분합 → gTilePartialSums 버퍼
 #   finalizeMain : 부분합 합산 → gAvgImpBuf[0] = Avg_Imp
 #   main         : 픽셀별 sampleCount 계산 + sampleCountVis 기록
 #
 # [디버그 출력]
 # sampleCountVis 채널: 픽셀별 sample count 를 (count/maxSamples) 로 정규화한
 # RGBA8Unorm 텍스처로 시각화하여 RenderGraph output 으로 노출.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
using namespace Falcor;

class SampleCountPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(SampleCountPass, "SampleCountPass",
        "Outputs per-pixel sample count using average-importance-based distribution.");

    static ref<SampleCountPass> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<SampleCountPass>(pDevice, props);
    }

    SampleCountPass(ref<Device> pDevice, const Properties& props);

    // -----------------------------------------------------------------------
    // RenderPass interface
    // -----------------------------------------------------------------------
    virtual Properties          getProperties()  const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void                execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void                renderUI(Gui::Widgets& widget) override;

private:
    void parseProperties(const Properties& props);

    /// GPU reduction 버퍼를 해상도에 맞게 (재)생성한다.
    /// 해상도가 바뀌거나 처음 execute() 가 호출될 때 호출된다.
    void prepareBuffers(uint32_t width, uint32_t height);

    // -----------------------------------------------------------------------
    // Configuration (exposed as properties / UI)
    // -----------------------------------------------------------------------

    /// 목표 평균 spp.  Avg_Imp 픽셀의 샘플 수가 이 값이 되도록 스케일한다.
    uint32_t mSTarget    = 4u;

    /// 픽셀당 최대 spp.  PathTracer 가 처리 가능한 상한.
    uint32_t mMaxSamples = 16u;

    // -----------------------------------------------------------------------
    // GPU resources
    // -----------------------------------------------------------------------

    /// Pass 1: 타일별 부분합 커널 (reduceMain 진입점).
    ref<ComputePass> mpReducePass;

    /// Pass 2: 전체 합산 → Avg_Imp 커널 (finalizeMain 진입점).
    ref<ComputePass> mpFinalizePass;

    /// Pass 3: 픽셀별 샘플 수 계산 커널 (main 진입점).
    ref<ComputePass> mpComputePass;

    /// Pass 1 → Pass 2: 타일별 부분합 저장 버퍼 (float × numTiles).
    ref<Buffer> mpTilePartialSums;

    /// Pass 2 결과: Avg_Imp 저장 버퍼 (float × 1).
    ref<Buffer> mpAvgImpBuf;

    /// 마지막으로 버퍼를 생성했을 때의 해상도 (변경 감지용).
    uint2 mLastFrameDim = {0u, 0u};
};
