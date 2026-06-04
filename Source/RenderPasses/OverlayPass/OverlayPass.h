#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"

using namespace Falcor;

class OverlayPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(
        OverlayPass,
        "OverlayPass",
        "화면 중앙에 크로스헤어를 그리는 오버레이 패스."
    );

    static ref<OverlayPass> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<OverlayPass>(pDevice, props);
    }

    OverlayPass(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderOverlayUI(RenderContext* pRenderContext) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    static void registerBindings(pybind11::module& m);

private:
    uint2   mFrameDim   = {};
    float   mHalfLen    = 15.f;  // 팔 길이 (픽셀)
    float   mThickness  = 1.5f;  // 선 두께 (픽셀)
    float   mGap        = 3.f;   // 중앙 공백 반경 (픽셀)
    float4  mColor      = float4(1.f, 1.f, 1.f, 1.f); // 크로스헤어 색상
};
