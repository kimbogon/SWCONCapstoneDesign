/***************************************************************************
 # ImportancePass.cpp
 **************************************************************************/
#include "ImportancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
const std::string kShaderFile = "RenderPasses/ImportancePass/ImportancePass.cs.slang";

// Input channel names — must match GBufferRT reflect() exactly.
const std::string kInputDiffuseOpacity = "diffuseOpacity";
const std::string kInputGuideNormalW = "guideNormalW";
const std::string kInputLinearZ = "linearZ";
// [ShadowCount] Shadow count input from GBufferRT (R32Uint).
const std::string kInputShadowCount = "shadowCount";

// Output channel name – must match the string PathTracer expects.
const std::string kOutputImportance = "importance";
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
    reflector.addInput(kInputGuideNormalW, "Guide normal in world space (RGBA32Float)").flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addInput(kInputLinearZ, "Linear Z and slope (RG32Float)").flags(RenderPassReflection::Field::Flags::Optional);
    // [ShadowCount] Shadow count input — optional.
    reflector.addInput(kInputShadowCount, "Number of occluded lights per pixel (R32Uint)")
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

    // Bind inputs (may be nullptr if not connected — shader handles that via defines).
    var["gDiffuseOpacity"] = renderData.getTexture(kInputDiffuseOpacity);
    var["gGuideNormalW"] = renderData.getTexture(kInputGuideNormalW);
    var["gLinearZ"] = renderData.getTexture(kInputLinearZ);
    // [ShadowCount] Bind shadow count input.
    var["gShadowCount"] = renderData.getTexture(kInputShadowCount);
    var["gImportance"] = pImportance;

    // Define validity flags so the shader knows which inputs are available.
    mpComputePass->getProgram()->addDefine("HAS_DIFFUSE_OPACITY", renderData.getTexture(kInputDiffuseOpacity) ? "1" : "0");
    mpComputePass->getProgram()->addDefine("HAS_GUIDE_NORMAL", renderData.getTexture(kInputGuideNormalW) ? "1" : "0");
    mpComputePass->getProgram()->addDefine("HAS_LINEAR_Z", renderData.getTexture(kInputLinearZ) ? "1" : "0");
    // [ShadowCount] Define validity flag for shadow count.
    mpComputePass->getProgram()->addDefine("HAS_SHADOW_COUNT", renderData.getTexture(kInputShadowCount) ? "1" : "0");

    mpComputePass->execute(pRenderContext, w, h);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ImportancePass>();
}
