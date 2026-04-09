/***************************************************************************
 # ImportancePass.cpp
 **************************************************************************/
#include "ImportancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
    const std::string kShaderFile = "RenderPasses/ImportancePass/ImportancePass.cs.slang";

    // Input channel names — must match GBufferRT reflect() exactly.
    // "diffuseOpacity" provides albedo (RGBA32Float).
    // "guideNormalW"   provides world-space guide normal (RGBA32Float).
    // "linearZ"        provides linear Z and slope (RG32Float).
    const std::string kInputDiffuseOpacity = "diffuseOpacity";
    const std::string kInputGuideNormalW   = "guideNormalW";
    const std::string kInputLinearZ        = "linearZ";

    // Output channel name – must match the string PathTracer expects.
    const std::string kOutputImportance = "importance";
}

ImportancePass::ImportancePass(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
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

    // Output importance map.
    reflector.addOutput(kOutputImportance, "Per-pixel importance [0,1] (R32Float)")
        .format(ResourceFormat::R32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    return reflector;
}

void ImportancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    ref<Texture> pImportance = renderData.getTexture(kOutputImportance);
    FALCOR_ASSERT(pImportance);

    const uint32_t w = pImportance->getWidth();
    const uint32_t h = pImportance->getHeight();

    auto var = mpComputePass->getRootVar();
    var["CB"]["frameDim"] = uint2(w, h);

    // Bind inputs (may be nullptr if not connected — shader handles that via is_valid).
    var["gDiffuseOpacity"] = renderData.getTexture(kInputDiffuseOpacity);
    var["gGuideNormalW"]   = renderData.getTexture(kInputGuideNormalW);
    var["gLinearZ"]        = renderData.getTexture(kInputLinearZ);
    var["gImportance"]     = pImportance;

    // Define validity flags so the shader knows which inputs are available.
    mpComputePass->getProgram()->addDefine("HAS_DIFFUSE_OPACITY", renderData.getTexture(kInputDiffuseOpacity) ? "1" : "0");
    mpComputePass->getProgram()->addDefine("HAS_GUIDE_NORMAL",    renderData.getTexture(kInputGuideNormalW)   ? "1" : "0");
    mpComputePass->getProgram()->addDefine("HAS_LINEAR_Z",        renderData.getTexture(kInputLinearZ)        ? "1" : "0");

    // const uint32_t groupSize = 16;
    // mpComputePass->execute(pRenderContext, { div_round_up(w, groupSize), div_round_up(h, groupSize), 1u });

    mpComputePass->execute(pRenderContext, w, h);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ImportancePass>();
}
