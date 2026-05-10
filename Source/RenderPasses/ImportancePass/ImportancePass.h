#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Core/API/Buffer.h"
#include <chrono>
#include <cmath>
#include <cstring>

using namespace Falcor;

class ImportancePass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(ImportancePass, "ImportancePass",
        {"Computes per-pixel importance from G-buffer + user task state."});

    static ref<ImportancePass> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<ImportancePass>(pDevice, props);
    }

    ImportancePass(ref<Device> pDevice, const Properties& props);

    // ---- Task definition ----
    enum class TaskType : uint32_t
    {
        None     = 0,
        Aiming   = 1,
        Pointing = 2,
        Steering = 3,
    };

    enum class DebugView : uint32_t
    {
        Importance     = 0,
        TaskOverlay    = 1,
        ImageSpaceOnly = 2,
        TaskObjectOnly = 3,
    };

    // GPU constant buffer layout — must match cbuffer CB in the slang shader exactly.
    // All vec2 fields are flattened to float[2] to avoid Falcor/GLM type dependencies.
    struct alignas(16) ImportanceCB
    {
        uint32_t frameDim[2];        //  0
        float    invFrameDim[2];     //  8
        uint32_t activeTask;         // 16
        uint32_t debugView;          // 20
        uint32_t hasImportanceVis;   // 24
        uint32_t _pad0;              // 28
        float    centerUV[2];        // 32
        float    centerRadiusUV;     // 40
        float    taskImportance;     // 44
        float    enemyRedThreshold;  // 48
        float    itemGBThreshold;    // 52
        float    lumLow;             // 56
        float    lumHigh;            // 60
        float    motionA;            // 64
        float    motionB;            // 68
        float    motionC;            // 72
        float    centerWeightMin;    // 76
        float    steerFloorY;        // 80
        float    _pad1;              // 84
        float    _pad2;              // 88
        float    _pad3;              // 92
    };  // total = 96 bytes, multiple of 16

    // RenderPass interface
    virtual Properties           getProperties()   const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void                 execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void                 renderUI(Gui::Widgets& widget) override;
    virtual bool                 onKeyEvent(const KeyboardEvent& keyEvent) override;
    virtual bool                 onMouseEvent(const MouseEvent& mouseEvent) override;

    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override
    {
        mpScene = pScene;
    }

private:
    void updateTaskState(float dt);
    void readbackCenterCategory();

    ref<ComputePass> mpComputePass;
    ref<Scene>       mpScene;

    // Input state
    bool    mKeyW = false, mKeyA = false, mKeyS = false, mKeyD = false;
    float   mMouseDeltaAccum     = 0.f;
    float   mMouseUV[2]          = { 0.5f, 0.5f };
    bool    mMouseMovedThisFrame = false;

    // Hold timers (seconds)
    float mAimHold       = 0.f;
    float mPointHold     = 0.f;
    float mSteerHold     = 0.f;
    float mHoldThreshold = 0.35f;

    // Color readback
    uint32_t mCenterCategory = 0;   // 0=none, 1=enemy(red), 2=item(green/blue)
    float    mEnemyHold = 0.f;
    float    mItemHold  = 0.f;

    // Camera change detection
    float4x4 mPrevViewProj = float4x4::identity();
    bool     mCameraMoved  = false;

    // Timing
    std::chrono::steady_clock::time_point mLastTick;
    bool mFirstTick = true;

    // GPU resources
    ImportanceCB mCB           = {};
    ref<Buffer>  mpCenterCatBuf;      // 1 x uint32, UAV (GPU writes category)
    ref<Buffer>  mpCenterCatStaging;  // ReadBack copy for CPU

    // UI-tweakable parameters
    DebugView mDebugView     = DebugView::Importance;
    float mEnemyRedThreshold = 0.55f;
    float mItemGBThreshold   = 0.45f;
    float mTaskImportance    = 0.90f;
    float mCenterRadiusUV    = 0.20f;
    float mLumLow            = 0.15f;
    float mLumHigh           = 0.90f;
    float mMotionA           = 1.05f;
    float mMotionB           = 3.10f;
    float mMotionC           = 0.35f;
    float mCenterWeightMin   = 0.50f;
    float mSteerFloorY       = 0.70f;

    static constexpr uint32_t kHistBins = 8;
    float mHistogram[kHistBins] = {};
};

static_assert(sizeof(ImportancePass::ImportanceCB) == 96,
    "ImportanceCB size mismatch -- check padding");
static_assert(sizeof(ImportancePass::ImportanceCB) % 16 == 0,
    "ImportanceCB must be 16-byte aligned");
