/***************************************************************************
 # ImportancePass.h
 #
 # Computes per-pixel importance from GBufferRT outputs (albedo, normal, depth).
 # importance = saturate(0.5 * lumVar + 0.3 * normalGrad + 0.2 * depthGrad)
 #
 # [디버그 출력 추가]
 # importanceVis 채널: importance 값을 grayscale RGBA8 텍스처로 시각화하여
 # RenderGraph output으로 노출한다.
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

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;

private:
    ref<ComputePass> mpComputePass;

    // [디버그 시각화] importance 값을 grayscale로 표현하는 RGBA8Unorm 텍스처.
    // RenderGraph가 직접 관리하지 않으므로 Pass가 직접 생성·소유한다.
    // execute() 안에서 프레임 해상도가 바뀌면 재생성된다.
    ref<Texture> mpImportanceVisTex;
};
