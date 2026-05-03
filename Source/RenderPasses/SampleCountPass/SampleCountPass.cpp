/***************************************************************************
 # SampleCountPass.cpp
 #
 # [변경 내용]
 # - 고정 임계값(thresholdLow / thresholdHigh) 제거.
 # - 3-pass dispatch 로 GPU Reduction → Avg_Imp → 픽셀별 샘플 수 계산.
 #
 # [3-pass dispatch 구조]
 #   1. reduceMain   : 타일별 importance 부분합 → gTilePartialSums
 #   2. finalizeMain : 부분합 합산 → gAvgImpBuf[0] = Avg_Imp
 #   3. main         : S_p = clamp(round(I_p / Avg_Imp * sTarget), 1, maxSamples)
 #
 # [디버그 출력]
 # sampleCountVis: sample count 를 정규화한 RGBA8Unorm 텍스처.
 # HAS_SAMPLE_COUNT_VIS define 으로 활성화 여부를 제어한다.
 **************************************************************************/
#include "SampleCountPass.h"

namespace
{
    // ---- 셰이더 파일 경로 ------------------------------------------------
    const std::string kShaderFile =
        "RenderPasses/SampleCountPass/SampleCountPass.cs.slang";

    // ---- 채널 이름 (reflect() 와 일치해야 함) ----------------------------
    const std::string kInputImportance      = "importance";
    const std::string kOutputSampleCount    = "sampleCount";
    const std::string kOutputSampleCountVis = "sampleCountVis"; ///< [디버그 시각화]

    // ---- Property 키 ------------------------------------------------------
    const std::string kPropSTarget    = "sTarget";
    const std::string kPropMaxSamples = "maxSamples";
}

// ===========================================================================
// Construction / Properties
// ===========================================================================

SampleCountPass::SampleCountPass(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    parseProperties(props);

    // 세 커널(reduceMain / finalizeMain / main) 을 하나의 ComputePass 로 컴파일.
    // ComputePass 는 기본적으로 "main" 진입점만 가지므로,
    // reduceMain / finalizeMain 은 별도 Program 으로 생성한다.
    // → 셰이더 파일을 공유하되 진입점(entrypoint)만 다르게 지정한다.
    ProgramDesc descReduce;
    descReduce.addShaderLibrary(kShaderFile).csEntry("reduceMain");
    mpReducePass = ComputePass::create(mpDevice, descReduce);

    ProgramDesc descFinalize;
    descFinalize.addShaderLibrary(kShaderFile).csEntry("finalizeMain");
    mpFinalizePass = ComputePass::create(mpDevice, descFinalize);

    ProgramDesc descMain;
    descMain.addShaderLibrary(kShaderFile).csEntry("main");
    mpComputePass = ComputePass::create(mpDevice, descMain);
}

void SampleCountPass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if      (key == kPropSTarget)    mSTarget    = value;
        else if (key == kPropMaxSamples) mMaxSamples = value;
        else logWarning("SampleCountPass: Unknown property '{}'.", key);
    }
}

Properties SampleCountPass::getProperties() const
{
    Properties props;
    props[kPropSTarget]    = mSTarget;
    props[kPropMaxSamples] = mMaxSamples;
    return props;
}

// ===========================================================================
// prepareBuffers  —  해상도에 따라 reduction 버퍼를 (재)생성
// ===========================================================================

void SampleCountPass::prepareBuffers(uint32_t width, uint32_t height)
{
    uint2 dim = {width, height};
    if (dim.x == mLastFrameDim.x && dim.y == mLastFrameDim.y)
        return; // 해상도 변화 없음 — 재생성 불필요

    mLastFrameDim = dim;

    // 타일 수 계산 (16×16 타일)
    uint32_t tilesX   = (width  + 15u) / 16u;
    uint32_t tilesY   = (height + 15u) / 16u;
    uint32_t numTiles = tilesX * tilesY;

    // ---- gTilePartialSums : float × numTiles --------------------------------
    // Falcor 8: Buffer::createStructured 대신 mpDevice->createStructuredBuffer() 사용.
    // CpuAccess::None 대신 MemoryType::DeviceLocal 사용.
    // UAV + ShaderResource 둘 다 필요: Pass1 이 쓰고 Pass2 가 읽는다.
    mpTilePartialSums = mpDevice->createStructuredBuffer(
        sizeof(float),             ///< structSize
        numTiles,                  ///< elementCount
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource,
        MemoryType::DeviceLocal,   ///< GPU 전용 메모리 (Falcor 8의 CpuAccess::None 대체)
        nullptr,                   ///< 초기 데이터 없음
        false                      ///< createCounter = false
    );
    mpTilePartialSums->setName("SampleCountPass::TilePartialSums");

    // ---- gAvgImpBuf : float × 1 --------------------------------------------
    mpAvgImpBuf = mpDevice->createStructuredBuffer(
        sizeof(float),
        1u,
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource,
        MemoryType::DeviceLocal,
        nullptr,
        false
    );
    mpAvgImpBuf->setName("SampleCountPass::AvgImpBuf");

    logInfo("SampleCountPass: Buffers resized for {}x{} ({} tiles).", width, height, numTiles);
}

// ===========================================================================
// Reflect
// ===========================================================================

RenderPassReflection SampleCountPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // ImportancePass 의 R32Float importance 텍스처 입력.
    reflector.addInput(kInputImportance, "Per-pixel importance [0,1] (R32Float)")
        .format(ResourceFormat::R32Float);

    // R8Uint sampleCount 출력 — PathTracer 가 Texture2D<uint> 로 읽는다.
    reflector.addOutput(kOutputSampleCount, "Per-pixel sample count (R8Uint)")
        .format(ResourceFormat::R8Uint)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    // [디버그 시각화] count/maxSamples 로 정규화한 RGBA8Unorm 텍스처.
    // Mogwai Output 탭에서 "SampleCountPass.sampleCountVis" 로 선택 가능.
    reflector.addOutput(kOutputSampleCountVis, "Sample count visualization (RGBA8Unorm)")
        .format(ResourceFormat::RGBA8Unorm)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    return reflector;
}

// ===========================================================================
// Execute  —  3-pass dispatch
// ===========================================================================

void SampleCountPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // -------------------------------------------------------------------------
    // 텍스처 참조 획득
    // -------------------------------------------------------------------------
    ref<Texture> pImportance     = renderData.getTexture(kInputImportance);
    ref<Texture> pOutput         = renderData.getTexture(kOutputSampleCount);
    ref<Texture> pSampleCountVis = renderData.getTexture(kOutputSampleCountVis); ///< [디버그 시각화]
    FALCOR_ASSERT(pImportance && pOutput);

    const uint32_t w = pOutput->getWidth();
    const uint32_t h = pOutput->getHeight();

    // -------------------------------------------------------------------------
    // 버퍼 준비 (해상도 변경 시 재생성)
    // -------------------------------------------------------------------------
    prepareBuffers(w, h);

    const uint32_t tilesX    = (w + 15u) / 16u;
    const uint32_t tilesY    = (h + 15u) / 16u;
    const uint32_t numTiles  = tilesX * tilesY;
    const uint32_t totalPix  = w * h;

    // =========================================================================
    // Pass 1 : reduceMain — 타일별 부분합
    // =========================================================================
    {
        auto var = mpReducePass->getRootVar();
        // Constant buffer
        var["CB"]["frameDim"]    = uint2(w, h);
        var["CB"]["sTarget"]     = mSTarget;
        var["CB"]["maxSamples"]  = mMaxSamples;
        var["CB"]["numTiles"]    = numTiles;
        var["CB"]["totalPixels"] = totalPix;
        // Resources
        var["gImportance"]       = pImportance;
        var["gTilePartialSums"]  = mpTilePartialSums;
        // (다른 리소스는 이 패스에서 미사용 — 바인딩하지 않아도 Falcor 가 dummy 를 채움)

        // 타일 그리드 크기로 dispatch.
        mpReducePass->execute(pRenderContext, tilesX * 16u, tilesY * 16u);
    }

    // UAV 쓰기 완료 보장 (Pass 1 → Pass 2 종속성).
    pRenderContext->uavBarrier(mpTilePartialSums.get());

    // =========================================================================
    // Pass 2 : finalizeMain — 전체 합산 → Avg_Imp
    // =========================================================================
    {
        auto var = mpFinalizePass->getRootVar();
        var["CB"]["frameDim"]    = uint2(w, h);
        var["CB"]["sTarget"]     = mSTarget;
        var["CB"]["maxSamples"]  = mMaxSamples;
        var["CB"]["numTiles"]    = numTiles;
        var["CB"]["totalPixels"] = totalPix;
        var["gTilePartialSums"]  = mpTilePartialSums;
        var["gAvgImpBuf"]        = mpAvgImpBuf;

        // 단일 스레드(1×1×1 그룹) dispatch.
        mpFinalizePass->execute(pRenderContext, 1u, 1u);
    }

    // UAV 쓰기 완료 보장 (Pass 2 → Pass 3 종속성).
    pRenderContext->uavBarrier(mpAvgImpBuf.get());

    // =========================================================================
    // Pass 3 : main — 픽셀별 샘플 수 계산
    // =========================================================================

    // [디버그 시각화] 시각화 텍스처 연결 여부에 따라 define 조정.
    mpComputePass->getProgram()->addDefine("HAS_SAMPLE_COUNT_VIS",
        pSampleCountVis ? "1" : "0");

    {
        auto var = mpComputePass->getRootVar();
        var["CB"]["frameDim"]    = uint2(w, h);
        var["CB"]["sTarget"]     = mSTarget;
        var["CB"]["maxSamples"]  = mMaxSamples;
        var["CB"]["numTiles"]    = numTiles;
        var["CB"]["totalPixels"] = totalPix;
        var["gImportance"]       = pImportance;
        var["gAvgImpBuf"]        = mpAvgImpBuf;
        var["gOutput"]           = pOutput;
        var["gSampleCountVis"]   = pSampleCountVis; ///< nullptr 도 허용 (셰이더에서 guard)

        // 픽셀 그리드 dispatch.
        mpComputePass->execute(pRenderContext, w, h);
    }
}

// ===========================================================================
// UI
// ===========================================================================

void SampleCountPass::renderUI(Gui::Widgets& widget)
{
    // sTarget: 평균 목표 spp.
    int sTargetInt = static_cast<int>(mSTarget);
    if (widget.var("Target avg spp (S_target)", sTargetInt, 1, static_cast<int>(mMaxSamples), 1))
        mSTarget = static_cast<uint32_t>(sTargetInt);
    widget.tooltip("Avg spp across all pixels. Pixels with importance = Avg_Imp get exactly this many samples.");

    // maxSamples: 픽셀당 최대 spp.
    int maxSamplesInt = static_cast<int>(mMaxSamples);
    if (widget.var("Max spp (maxSamples)", maxSamplesInt, 1, 256, 1))
        mMaxSamples = static_cast<uint32_t>(maxSamplesInt);
    widget.tooltip("Hard upper bound per pixel. Must be <= PathTracer's maxSamplesPerPixel.");
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SampleCountPass>();
}
