/***************************************************************************
 # SampleCountPass.cpp
 #
 # Modified: importance-based sample count instead of centre-radius heuristic.
 #
 # [디버그 출력 추가]
 # sampleCountVis: sample count 값(1/2/4)을 정규화된 RGBA8Unorm 텍스처로 변환하여
 # RenderGraph의 별도 output 채널로 노출한다.
 # 셰이더 내에서 count → normalized float 매핑 후 gSampleCountVis UAV에 기록한다.
 **************************************************************************/
#include "SampleCountPass.h"

namespace
{
    // Shader file (relative to the Falcor render-pass search paths).
    const std::string kShaderFile = "RenderPasses/SampleCountPass/SampleCountPass.cs.slang";

    // Channel names — must match reflect().
    const std::string kInputImportance      = "importance";
    const std::string kOutputSampleCount    = "sampleCount";
    // [디버그 시각화] sample count 시각화 출력 채널 이름.
    const std::string kOutputSampleCountVis = "sampleCountVis";

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

    // [디버그 시각화] sample count 값을 정규화하여 밝기로 표현하는 RGBA8Unorm 텍스처.
    // 1spp → 0.25(어두움), 2spp → 0.5(중간), 4spp → 1.0(밝음)
    // Mogwai의 Output 탭에서 "SampleCountPass.sampleCountVis" 로 선택해 확인한다.
    reflector.addOutput(kOutputSampleCountVis, "Sample count visualization (RGBA8Unorm)")
        .format(ResourceFormat::RGBA8Unorm)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    return reflector;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void SampleCountPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // -----------------------------------------------------------------------
    // 입출력 텍스처 참조 획득
    // -----------------------------------------------------------------------
    ref<Texture> pImportance       = renderData.getTexture(kInputImportance);
    ref<Texture> pOutput           = renderData.getTexture(kOutputSampleCount);
    // [디버그 시각화] RenderGraph가 할당한 시각화 텍스처를 가져온다.
    ref<Texture> pSampleCountVis   = renderData.getTexture(kOutputSampleCountVis);
    FALCOR_ASSERT(pOutput);

    const uint32_t w = pOutput->getWidth();
    const uint32_t h = pOutput->getHeight();

    // -----------------------------------------------------------------------
    // 셰이더 변수 바인딩
    // -----------------------------------------------------------------------
    auto var = mpComputePass->getRootVar();
    var["CB"]["frameDim"]      = uint2(w, h);
    var["CB"]["thresholdLow"]  = mThresholdLow;
    var["CB"]["thresholdHigh"] = mThresholdHigh;
    var["gImportance"]         = pImportance;
    var["gOutput"]             = pOutput;
    // [디버그 시각화] 시각화 UAV 바인딩. 연결되지 않은 경우(nullptr)에도 셰이더에서 guard한다.
    var["gSampleCountVis"]     = pSampleCountVis;

    // [디버그 시각화] 시각화 텍스처가 실제로 연결되었는지 셰이더에 알린다.
    mpComputePass->getProgram()->addDefine("HAS_SAMPLE_COUNT_VIS",
        pSampleCountVis ? "1" : "0");

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
