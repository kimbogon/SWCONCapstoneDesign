/***************************************************************************
 # ImportancePass.h
 #
 # GBufferRT 출력으로부터 픽셀별 중요도(importance)를 계산한다.
 #
 # [입력 feature 전체 목록]
 # - diffuseOpacity  : diffuse albedo + opacity       (RGBA32Float)
 # - guideNormalW    : detail(shading) normal WS      (RGBA32Float)
 # - faceNormalW     : geometry normal WS             (RGBA32Float)  ← kGBufferChannels
 # - specRough       : specular + roughness           (RGBA32Float)  → glossiness = 1 - roughness
 # - mvecW           : world-space motion vector      (RGBA16Float)
 # - linearZ         : linear depth + slope           (RG32Float)
 # - objectID        : mesh instance 고유 번호        (R32Uint)      ← 신규
 # - luminance       : BT.601 luminance               (R32Float)     ← 신규
 # - centerDist      : 화면 중심으로부터의 거리       (R32Float)     ← 신규
 #
 # [출력]
 # - importance      : per-pixel importance [0,1]    (R32Float)
 # - importanceVis   : grayscale 시각화              (RGBA8Unorm)
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"

using namespace Falcor;

class ImportancePass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(ImportancePass, "ImportancePass", "Computes per-pixel importance from G-buffer data.");

    static ref<ImportancePass> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<ImportancePass>(pDevice, props);
    }

    ImportancePass(ref<Device> pDevice, const Properties& props);

    virtual Properties          getProperties()  const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void                execute(RenderContext* pRenderContext, const RenderData& renderData) override;

private:
    ref<ComputePass> mpComputePass;
};
