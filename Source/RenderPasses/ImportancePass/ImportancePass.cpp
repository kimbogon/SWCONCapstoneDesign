/***************************************************************************
 # ImportancePass.cpp
 #
 # GBufferRT 의 모든 feature 채널을 입력으로 받아 픽셀별 중요도를 계산한다.
 #
 # [추가된 입력 채널]
 # - faceNormalW  : geometry normal (kGBufferChannels 소속)
 # - specRough    : specular reflectance + roughness (glossiness = 1 - roughness)
 # - mvecW        : world-space motion vector
 # - objectID     : mesh instance 고유 번호 (신규)
 # - luminance    : BT.601 luminance from diffuse (신규)
 # - centerDist   : 화면 중심 거리 (신규)
 #
 # [기존 채널 유지]
 # - diffuseOpacity, guideNormalW, linearZ, shadowCount, importanceVis
 **************************************************************************/
#include "ImportancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
const std::string kShaderFile = "RenderPasses/ImportancePass/ImportancePass.cs.slang";

// ---- 입력 채널 이름 (GBufferRT reflect() 의 channel.name 과 정확히 일치해야 한다) ----
const std::string kInputDiffuseOpacity = "diffuseOpacity";   // RGBA32Float
const std::string kInputGuideNormalW   = "guideNormalW";     // RGBA32Float  (detail normal)
const std::string kInputFaceNormalW    = "faceNormalW";      // RGBA32Float  (geometry normal)
const std::string kInputSpecRough      = "specRough";        // RGBA32Float  (specular + roughness)
const std::string kInputMotionVectorW  = "mvecW";            // RGBA16Float  (world-space motion)
const std::string kInputLinearZ        = "linearZ";          // RG32Float
const std::string kInputShadowCount    = "shadowCount";      // R32Uint

// [신규] GBufferRT 에 새로 추가된 채널들
//
const std::string kInputObjID          = "objectID";         // R32Uint
const std::string kInputLuminance      = "luminance";        // R32Float
const std::string kInputCenterDist     = "centerDist";       // R32Float


// ---- 출력 채널 이름 ----
const std::string kOutputImportance    = "importance";
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

    // ---- 입력 (모두 optional — 연결되지 않아도 Pass가 로드된다) ----

    reflector.addInput(kInputDiffuseOpacity, "Diffuse albedo and opacity (RGBA32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    reflector.addInput(kInputGuideNormalW, "Detail(shading) normal in world space (RGBA32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // [FaceNormal] geometry normal — GBufferRT.faceNormalW 채널
    reflector.addInput(kInputFaceNormalW, "Geometry face normal in world space (RGBA32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // [SpecRough] specular reflectance(RGB) + roughness(A) — glossiness = 1 - roughness
    reflector.addInput(kInputSpecRough, "Specular reflectance and roughness (RGBA32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // [MotionVectorW] world-space motion vector
    reflector.addInput(kInputMotionVectorW, "Motion vector in world space (RGBA16Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    reflector.addInput(kInputLinearZ, "Linear Z and slope (RG32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    reflector.addInput(kInputShadowCount, "Number of occluded lights per pixel (R32Uint)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // [ObjectID] mesh instance 고유 번호 (신규)
    reflector.addInput(kInputObjID, "Geometry instance index / object ID (R32Uint)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // [Luminance] GBufferRT 에서 미리 계산된 luminance (신규)
    reflector.addInput(kInputLuminance, "Per-pixel luminance from diffuse albedo (R32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // [CenterDist] 화면 중심으로부터의 거리 (신규)
    reflector.addInput(kInputCenterDist, "Normalized distance from screen center (R32Float)")
        .flags(RenderPassReflection::Field::Flags::Optional);

    // ---- 출력 ----

    reflector.addOutput(kOutputImportance, "Per-pixel importance [0,1] (R32Float)")
        .format(ResourceFormat::R32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    reflector.addOutput(kOutputImportanceVis, "Importance grayscale visualization (RGBA8Unorm)")
        .format(ResourceFormat::RGBA8Unorm)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);

    return reflector;
}

void ImportancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // ---- 출력 텍스처 참조 ----
    ref<Texture> pImportance    = renderData.getTexture(kOutputImportance);
    ref<Texture> pImportanceVis = renderData.getTexture(kOutputImportanceVis);
    FALCOR_ASSERT(pImportance);

    const uint32_t w = pImportance->getWidth();
    const uint32_t h = pImportance->getHeight();

    // ---- 셰이더 변수 바인딩 ----
    auto var = mpComputePass->getRootVar();
    var["CB"]["frameDim"] = uint2(w, h);

    // 입력 바인딩 (nullptr 허용 — 셰이더에서 define 으로 guard)
    var["gDiffuseOpacity"]  = renderData.getTexture(kInputDiffuseOpacity);
    var["gGuideNormalW"]    = renderData.getTexture(kInputGuideNormalW);
    var["gFaceNormalW"]     = renderData.getTexture(kInputFaceNormalW);    // [신규]
    var["gSpecRough"]       = renderData.getTexture(kInputSpecRough);      // [신규]
    var["gMotionVectorW"]   = renderData.getTexture(kInputMotionVectorW);  // [신규]
    var["gLinearZ"]         = renderData.getTexture(kInputLinearZ);
    var["gShadowCount"]     = renderData.getTexture(kInputShadowCount);
    var["gObjectID"]        = renderData.getTexture(kInputObjID);       // [신규]
    var["gLuminanceTex"]    = renderData.getTexture(kInputLuminance);      // [신규] 이름 충돌 방지
    var["gCenterDist"]      = renderData.getTexture(kInputCenterDist);     // [신규]
    var["gImportance"]      = pImportance;
    var["gImportanceVis"]   = pImportanceVis;

    // ---- 입력 유효성 define 설정 ----
    auto& prog = *mpComputePass->getProgram();
    prog.addDefine("HAS_DIFFUSE_OPACITY",  renderData.getTexture(kInputDiffuseOpacity)  ? "1" : "0");
    prog.addDefine("HAS_GUIDE_NORMAL",     renderData.getTexture(kInputGuideNormalW)    ? "1" : "0");
    prog.addDefine("HAS_FACE_NORMAL",      renderData.getTexture(kInputFaceNormalW)     ? "1" : "0"); // [신규]
    prog.addDefine("HAS_SPEC_ROUGH",       renderData.getTexture(kInputSpecRough)       ? "1" : "0"); // [신규]
    prog.addDefine("HAS_MOTION_VECTOR_W",  renderData.getTexture(kInputMotionVectorW)   ? "1" : "0"); // [신규]
    prog.addDefine("HAS_LINEAR_Z",         renderData.getTexture(kInputLinearZ)         ? "1" : "0");
    prog.addDefine("HAS_SHADOW_COUNT",     renderData.getTexture(kInputShadowCount)     ? "1" : "0");
    prog.addDefine("HAS_OBJ_ID",           renderData.getTexture(kInputObjID)        ? "1" : "0"); // [신규]
    prog.addDefine("HAS_LUMINANCE",        renderData.getTexture(kInputLuminance)        ? "1" : "0"); // [신규]
    prog.addDefine("HAS_CENTER_DIST",      renderData.getTexture(kInputCenterDist)      ? "1" : "0"); // [신규]
    prog.addDefine("HAS_IMPORTANCE_VIS",   pImportanceVis                               ? "1" : "0");

    mpComputePass->execute(pRenderContext, w, h);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ImportancePass>();
}
