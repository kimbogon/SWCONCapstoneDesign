/***************************************************************************
 # ImportancePass.cpp
 #
 # [디버그 출력 추가]
 # importanceVis: importance float 값을 grayscale RGBA8Unorm 텍스처로 변환하여
 # RenderGraph의 별도 output 채널로 노출한다.
 # 셰이더 내에서 직접 gImportanceVis UAV에 float → float4(v,v,v,1) 형태로 기록한다.
 **************************************************************************/
#include "ImportancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
const std::string kShaderFile = "RenderPasses/ImportancePass/ImportancePass.cs.slang";

// Input channel names — must match GBufferRT reflect() exactly.
const std::string kInputDiffuseOpacity  = "diffuseOpacity";
const std::string kInputGuideNormalW    = "guideNormalW";
const std::string kInputLinearZ         = "linearZ";
// [ShadowCount] Shadow count input from GBufferRT (R32Uint).
const std::string kInputShadowCount     = "shadowCount";

// Output channel names.
const std::string kOutputImportance    = "importance";
// [디버그 시각화] grayscale 시각화 출력 채널 이름.
const std::string kOutputImportanceVis = "importanceVis";
} // namespace

ImportancePass::ImportancePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    mpComputePass = ComputePass::create(mpDevice, kShaderFile, "main");
}

Properties ImportancePass::getProperties() const
{
    return Properties();
}

RenderPassReflection ImportancePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Inputs from GBufferRT — all optional so the pass still loads if a channel is disconnected.
    reflector.addInput(kInputDiffuseOpacity, "Diffuse albedo and opacity (RGBA32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addInput(kInputGuideNormalW, "Guide normal in world space (RGBA32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addInput(kInputLinearZ, "Linear Z and slope (RG32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);
    // [ShadowCount] Shadow count input — optional.
    reflector.addInput(kInputShadowCount, "Number of occluded lights per pixel (R32Uint)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // Output importance map (R32Float) — PathTracer / SampleCountPass가 읽는 실제 데이터.
    reflector.addOutput(kOutputImportance, "Per-pixel importance [0,1] (R32Float)")
        .format(ResourceFormat::R32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    // [디버그 시각화] importance 값을 grayscale로 시각화한 RGBA8Unorm 텍스처.
    // Mogwai의 Output 탭에서 "ImportancePass.importanceVis" 로 선택해 확인한다.
    reflector.addOutput(kOutputImportanceVis, "Importance grayscale visualization (RGBA8Unorm)")
        .format(ResourceFormat::RGBA8Unorm)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    return reflector;
}

void ImportancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // -----------------------------------------------------------------------
    // 출력 텍스처 참조 획득
    // -----------------------------------------------------------------------
    ref<Texture> pImportance    = renderData.getTexture(kOutputImportance);
    // [디버그 시각화] RenderGraph가 할당한 시각화 텍스처를 가져온다.
    ref<Texture> pImportanceVis = renderData.getTexture(kOutputImportanceVis);
    FALCOR_ASSERT(pImportance);

    const uint32_t w = pImportance->getWidth();
    const uint32_t h = pImportance->getHeight();

    // -----------------------------------------------------------------------
    // 셰이더 변수 바인딩
    // -----------------------------------------------------------------------
    auto var = mpComputePass->getRootVar();
    var["CB"]["frameDim"] = uint2(w, h);

    // Bind inputs (may be nullptr if not connected — shader handles that via defines).
    var["gDiffuseOpacity"] = renderData.getTexture(kInputDiffuseOpacity);
    var["gGuideNormalW"]   = renderData.getTexture(kInputGuideNormalW);
    var["gLinearZ"]        = renderData.getTexture(kInputLinearZ);
    // [ShadowCount] Bind shadow count input.
    var["gShadowCount"]    = renderData.getTexture(kInputShadowCount);
    var["gImportance"]     = pImportance;
    // [디버그 시각화] 시각화 UAV 바인딩. 연결되지 않은 경우(nullptr)에도 셰이더에서 guard한다.
    var["gImportanceVis"]  = pImportanceVis;

    // Define validity flags so the shader knows which inputs are available.
    mpComputePass->getProgram()->addDefine("HAS_DIFFUSE_OPACITY",
        renderData.getTexture(kInputDiffuseOpacity) ? "1" : "0");
    mpComputePass->getProgram()->addDefine("HAS_GUIDE_NORMAL",
        renderData.getTexture(kInputGuideNormalW) ? "1" : "0");
    mpComputePass->getProgram()->addDefine("HAS_LINEAR_Z",
        renderData.getTexture(kInputLinearZ) ? "1" : "0");
    // [ShadowCount] Define validity flag for shadow count.
    mpComputePass->getProgram()->addDefine("HAS_SHADOW_COUNT",
        renderData.getTexture(kInputShadowCount) ? "1" : "0");
    // [디버그 시각화] 시각화 텍스처가 실제로 연결되었는지 셰이더에 알린다.
    mpComputePass->getProgram()->addDefine("HAS_IMPORTANCE_VIS",
        pImportanceVis ? "1" : "0");

    mpComputePass->execute(pRenderContext, w, h);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ImportancePass>();
}
