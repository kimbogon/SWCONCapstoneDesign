#include "ImportancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
const std::string kShaderFile = "RenderPasses/ImportancePass/ImportancePass.cs.slang";

// Input channel names (must match GBufferRT output channel names exactly)
const std::string kInDiffuseOpacity = "diffuseOpacity";
const std::string kInGuideNormalW   = "guideNormalW";
const std::string kInFaceNormalW    = "faceNormalW";
const std::string kInSpecRough      = "specRough";
const std::string kInMotionVectorW  = "mvecW";
const std::string kInLinearZ        = "linearZ";
const std::string kInShadowCount    = "shadowCount";
const std::string kInObjID          = "objectID";
const std::string kInLuminance      = "luminance";
const std::string kInCenterDist     = "centerDist";
const std::string kInMetalness      = "metalness";

// Output channel names
const std::string kOutImportance    = "importance";
const std::string kOutImportanceVis = "importanceVis";
} // namespace

// ---------------------------------------------------------------------------
ImportancePass::ImportancePass(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    mpComputePass = ComputePass::create(mpDevice, kShaderFile, "main");

    // 1-element uint32 UAV for center-pixel category written by the shader
    mpCenterCatBuf = mpDevice->createBuffer(
        sizeof(uint32_t),
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource,
        MemoryType::DeviceLocal);

    // Staging buffer for CPU readback (1-frame latency)
    mpCenterCatStaging = mpDevice->createBuffer(
        sizeof(uint32_t),
        ResourceBindFlags::None,
        MemoryType::ReadBack);
}

// ---------------------------------------------------------------------------
Properties ImportancePass::getProperties() const { return Properties(); }

// ---------------------------------------------------------------------------
RenderPassReflection ImportancePass::reflect(const CompileData&)
{
    RenderPassReflection r;
    const auto opt = RenderPassReflection::Field::Flags::Optional;

    r.addInput(kInDiffuseOpacity, "Diffuse albedo + opacity").flags(opt);
    r.addInput(kInGuideNormalW,   "Detail (shading) normal W").flags(opt);
    r.addInput(kInFaceNormalW,    "Geometry face normal W").flags(opt);
    r.addInput(kInSpecRough,      "Specular + roughness").flags(opt);
    r.addInput(kInMotionVectorW,  "World-space motion vector").flags(opt);
    r.addInput(kInLinearZ,        "Linear Z + slope").flags(opt);
    r.addInput(kInShadowCount,    "Shadow count").flags(opt);
    r.addInput(kInObjID,          "Object/instance ID").flags(opt);
    r.addInput(kInLuminance,      "Pre-computed luminance").flags(opt);
    r.addInput(kInCenterDist,     "Distance to screen center").flags(opt);
    r.addInput(kInMetalness,      "Per-pixel metalness").flags(opt);

    r.addOutput(kOutImportance, "Per-pixel importance [0,1]")
        .format(ResourceFormat::R32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);
    r.addOutput(kOutImportanceVis, "Importance visualization")
        .format(ResourceFormat::RGBA8Unorm)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource);
    return r;
}

// ---------------------------------------------------------------------------
bool ImportancePass::onKeyEvent(const KeyboardEvent& e)
{
    const bool down = (e.type == KeyboardEvent::Type::KeyPressed ||
                       e.type == KeyboardEvent::Type::KeyRepeated);
    const bool up   = (e.type == KeyboardEvent::Type::KeyReleased);
    if (!down && !up) return false;

    const bool v = down;
    switch (e.key)
    {
    case Input::Key::W: mKeyW = v; break;
    case Input::Key::A: mKeyA = v; break;
    case Input::Key::S: mKeyS = v; break;
    case Input::Key::D: mKeyD = v; break;
    default: return false;
    }
    return false; // do not consume; CameraController also needs it
}

// ---------------------------------------------------------------------------
bool ImportancePass::onMouseEvent(const MouseEvent& e)
{
    if (e.type == MouseEvent::Type::Move)
    {
        const float dx = e.pos.x - mMouseUV[0];
        const float dy = e.pos.y - mMouseUV[1];
        mMouseDeltaAccum    += std::sqrt(dx * dx + dy * dy);
        mMouseUV[0]          = e.pos.x;
        mMouseUV[1]          = e.pos.y;
        mMouseMovedThisFrame = true;
    }
    return false;
}

// ---------------------------------------------------------------------------
void ImportancePass::readbackCenterCategory()
{
    if (!mpCenterCatStaging) return;
    const void* pRaw = mpCenterCatStaging->map();
    if (pRaw)
    {
        std::memcpy(&mCenterCategory, pRaw, sizeof(uint32_t));
        mpCenterCatStaging->unmap();
    }
}

// ---------------------------------------------------------------------------
void ImportancePass::updateTaskState(float dt)
{
    // 1) Camera-movement detection via ViewProj matrix comparison
    bool camMoved = false;
    if (mpScene && mpScene->getCamera())
    {
        const float4x4 vp = mpScene->getCamera()->getViewProjMatrix();
        for (int i = 0; i < 4 && !camMoved; ++i)
            for (int j = 0; j < 4 && !camMoved; ++j)
                if (std::abs(vp[i][j] - mPrevViewProj[i][j]) > 1e-6f)
                    camMoved = true;
        mPrevViewProj = vp;
    }
    mCameraMoved = camMoved;

    // 2) Accumulate hold timers
    const bool wasdDown    = mKeyW || mKeyA || mKeyS || mKeyD;
    if (wasdDown) mSteerHold += dt; else mSteerHold = 0.f;

    const bool stillCam    = !camMoved;
    const bool stillMouse  = !mMouseMovedThisFrame;
    const bool enemyCenter = (mCenterCategory == 1u);
    const bool itemCenter  = (mCenterCategory == 2u);

    // if (enemyCenter) mEnemyHold += dt; else mEnemyHold = 0.f;
    // if (itemCenter)  mItemHold  += dt; else mItemHold  = 0.f;

    if (enemyCenter)                          mAimHold   += dt; else mAimHold   = 0.f;
    if (stillCam && stillMouse && itemCenter) mPointHold += dt; else mPointHold = 0.f;

    // 3) Priority: Aiming > Pointing > Steering
    TaskType t = TaskType::None;
    if      (mAimHold   >= mHoldThreshold) t = TaskType::Aiming;
    else if (mPointHold >= mHoldThreshold) t = TaskType::Pointing;
    else if (mSteerHold >= mHoldThreshold) t = TaskType::Steering;

    mCB.activeTask = static_cast<uint32_t>(t);

    // Reset per-frame accumulators
    mMouseDeltaAccum     = 0.f;
    mMouseMovedThisFrame = false;
}

// ---------------------------------------------------------------------------
void ImportancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Delta-time
    const auto now = std::chrono::steady_clock::now();
    const float dt = mFirstTick
        ? 0.f
        : std::chrono::duration<float>(now - mLastTick).count();
    mLastTick  = now;
    mFirstTick = false;

    // 1) Readback last frame's GPU result (1-frame latency)
    readbackCenterCategory();

    // 2) Update task FSM
    updateTaskState(dt);

    // Fetch output textures
    ref<Texture> pImportance    = renderData.getTexture(kOutImportance);
    ref<Texture> pImportanceVis = renderData.getTexture(kOutImportanceVis);
    FALCOR_ASSERT(pImportance);
    const uint32_t w = pImportance->getWidth();
    const uint32_t h = pImportance->getHeight();

    // Fill constant buffer
    mCB.frameDim[0]       = w;
    mCB.frameDim[1]       = h;
    mCB.invFrameDim[0]    = 1.f / static_cast<float>(w);
    mCB.invFrameDim[1]    = 1.f / static_cast<float>(h);
    // mCB.activeTask: updateTaskState() 에서 초기화
    mCB.debugView         = static_cast<uint32_t>(mDebugView);
    mCB.hasImportanceVis  = pImportanceVis ? 1u : 0u;
    mCB.centerUV[0]       = 0.5f;
    mCB.centerUV[1]       = 0.5f;
    mCB.centerRadiusUV    = mCenterRadiusUV;
    mCB.taskImportance    = mTaskImportance;
    mCB.enemyRedThreshold = mEnemyRedThreshold;
    mCB.itemGBThreshold   = mItemGBThreshold;
    mCB.lumLow            = mLumLow;
    mCB.lumHigh           = mLumHigh;
    mCB.motionA           = mMotionA;
    mCB.motionB           = mMotionB;
    mCB.motionC           = mMotionC;
    mCB.centerWeightMin   = mCenterWeightMin;
    mCB.steerFloorY       = mSteerFloorY;

    // Bind shader resources
    auto var = mpComputePass->getRootVar();
    var["CB"].setBlob(&mCB, sizeof(mCB));

    var["gDiffuseOpacity"] = renderData.getTexture(kInDiffuseOpacity);
    var["gGuideNormalW"]   = renderData.getTexture(kInGuideNormalW);
    var["gFaceNormalW"]    = renderData.getTexture(kInFaceNormalW);
    var["gSpecRough"]      = renderData.getTexture(kInSpecRough);
    var["gMotionVectorW"]  = renderData.getTexture(kInMotionVectorW);
    var["gLinearZ"]        = renderData.getTexture(kInLinearZ);
    var["gShadowCount"]    = renderData.getTexture(kInShadowCount);
    var["gObjectID"]       = renderData.getTexture(kInObjID);
    var["gLuminanceTex"]   = renderData.getTexture(kInLuminance);
    var["gCenterDist"]     = renderData.getTexture(kInCenterDist);
    var["gMetalness"]      = renderData.getTexture(kInMetalness);

    var["gImportance"]     = pImportance;
    var["gImportanceVis"]  = pImportanceVis;
    var["gCenterCategory"] = mpCenterCatBuf;

    // Preprocessor defines driven by texture availability
    DefineList defines;
    auto addTexDef = [&](const char* name, ref<Texture> tex)
    {
        defines[name] = tex ? "1" : "0";
    };
    addTexDef("HAS_DIFFUSE_OPACITY", renderData.getTexture(kInDiffuseOpacity));
    addTexDef("HAS_GUIDE_NORMAL",    renderData.getTexture(kInGuideNormalW));
    addTexDef("HAS_FACE_NORMAL",     renderData.getTexture(kInFaceNormalW));
    addTexDef("HAS_SPEC_ROUGH",      renderData.getTexture(kInSpecRough));
    addTexDef("HAS_MOTION_VECTOR_W", renderData.getTexture(kInMotionVectorW));
    addTexDef("HAS_LINEAR_Z",        renderData.getTexture(kInLinearZ));
    addTexDef("HAS_SHADOW_COUNT",    renderData.getTexture(kInShadowCount));
    addTexDef("HAS_OBJ_ID",          renderData.getTexture(kInObjID));
    addTexDef("HAS_LUMINANCE",       renderData.getTexture(kInLuminance));
    addTexDef("HAS_CENTER_DIST",     renderData.getTexture(kInCenterDist));
    addTexDef("HAS_METALNESS",       renderData.getTexture(kInMetalness));
    defines["HAS_IMPORTANCE_VIS"] = pImportanceVis ? "1" : "0";
    mpComputePass->getProgram()->addDefines(defines);

    // Dispatch
    mpComputePass->execute(pRenderContext, w, h);

    // Copy GPU result to staging for next-frame CPU readback
    pRenderContext->copyResource(mpCenterCatStaging.get(), mpCenterCatBuf.get());
}

// ---------------------------------------------------------------------------
void ImportancePass::renderUI(Gui::Widgets& widget)
{
    static const Gui::DropdownList kDbgList = {
        { static_cast<uint32_t>(DebugView::Importance),     "Importance (grayscale)" },
        { static_cast<uint32_t>(DebugView::TaskOverlay),    "Task overlay" },
        { static_cast<uint32_t>(DebugView::ImageSpaceOnly), "Image-space only" },
        { static_cast<uint32_t>(DebugView::TaskObjectOnly), "Task-object only" },
    };

    uint32_t dv = static_cast<uint32_t>(mDebugView);
    if (widget.dropdown("Debug view", kDbgList, dv))
        mDebugView = static_cast<DebugView>(dv);

    static const char* kTaskNames[] = { "None", "Aiming", "Pointing", "Steering" };
    const uint32_t taskIdx = (mCB.activeTask < 4u) ? mCB.activeTask : 0u;
    widget.text(std::string("Active task: ") + kTaskNames[taskIdx]);
    widget.text("Center category: " + std::to_string(mCenterCategory) + "  (1=enemy, 2=item)");

    widget.var("Hold threshold (s)", mHoldThreshold,      0.05f, 2.0f,  0.01f);
    widget.var("Center radius (UV)", mCenterRadiusUV,     0.0f,  0.5f,  0.005f);
    widget.var("Task importance",    mTaskImportance,     0.0f,  1.0f,  0.01f);
    widget.var("Enemy red thr",      mEnemyRedThreshold,  0.0f,  1.0f,  0.01f);
    widget.var("Item GB thr",        mItemGBThreshold,    0.0f,  1.0f,  0.01f);
    widget.var("Lum low",            mLumLow,             0.0f,  0.5f,  0.005f);
    widget.var("Lum high",           mLumHigh,            0.5f,  1.0f,  0.005f);
    widget.var("Motion A",           mMotionA,            0.0f,  5.0f,  0.01f);
    widget.var("Motion B",           mMotionB,            0.0f,  8.0f,  0.05f);
    widget.var("Motion C",           mMotionC,            0.0f,  2.0f,  0.01f);
    widget.var("Center weight min",  mCenterWeightMin,    0.0f,  1.0f,  0.01f);
    widget.var("Steering floor Y",   mSteerFloorY,        0.0f,  1.0f,  0.01f);

    widget.text("Aim hold:   " + std::to_string(mAimHold));
    widget.text("Point hold: " + std::to_string(mPointHold));
    widget.text("Steer hold: " + std::to_string(mSteerHold));
}

// ---------------------------------------------------------------------------
// Plugin.h 의 registerClass<BaseT, T>() 오버로드를 사용:
//   BaseT = RenderPass  (plugin base class, defines PluginInfo / PluginCreate)
//   T     = ImportancePass  (concrete class, provides kPluginType / kPluginInfo / create)
extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ImportancePass>();
}
