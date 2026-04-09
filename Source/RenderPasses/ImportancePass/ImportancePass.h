/***************************************************************************
 # ImportancePass.h
 #
 # Computes per-pixel importance from GBufferRT outputs (albedo, normal, depth).
 # importance = saturate(0.5 * lumVar + 0.3 * normalGrad + 0.2 * depthGrad)
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
};
