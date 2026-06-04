#include "OverlayPass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

#include "imgui.h"

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, OverlayPass>();
    ScriptBindings::registerBinding(OverlayPass::registerBindings);
}

namespace
{
const ChannelList kInputChannels = {
    {"input", "", "Input buffer", true, ResourceFormat::RGBA32Float},
};

const ChannelList kOutputChannels = {
    {"output", "", "Output buffer", false, ResourceFormat::RGBA32Float},
};

// Properties 
const char kHalfLen[] = "halfLen";
const char kThickness[] = "thickness";
const char kGap[] = "gap";
const char kColor[] = "color";
} // namespace

OverlayPass::OverlayPass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    // 렌더 그래프 스크립트에서 넘겨받은 파라미터 로드
    for (const auto& [key, val] : props)
    {
        if      (key == kHalfLen)   mHalfLen   = val;
        else if (key == kThickness) mThickness = val;
        else if (key == kGap)       mGap       = val;
        else if (key == kColor)     mColor     = val;
    }
}

Properties OverlayPass::getProperties() const
{
    Properties props;
    props[kHalfLen]   = mHalfLen;
    props[kThickness] = mThickness;
    props[kGap]       = mGap;
    props[kColor]     = mColor;
    return props;
}

RenderPassReflection OverlayPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);

    // RenderTarget 플래그 명시
    reflector.addOutput("output", "Output buffer")
        .format(ResourceFormat::RGBA32Float)
        .bindFlags(ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource);

    return reflector;
}



void OverlayPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 입력을 출력으로 그대로 복사 (크로스헤어는 renderOverlayUI에서 ImGui로 그림)
    auto src = renderData.getTexture(kInputChannels[0].name);
    auto dst = renderData.getTexture(kOutputChannels[0].name);

    mFrameDim.x = dst->getWidth();
    mFrameDim.y = dst->getHeight();

    if (src)
    {
        pRenderContext->blit(src->getSRV(), dst->getRTV());
    }
}

void OverlayPass::renderOverlayUI(RenderContext* pRenderContext)
{
    // ImGui 백그라운드 드로우리스트에 직접 그림 (렌더 결과 위에 올라옴)
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // 화면 중앙 좌표
    float cx = mFrameDim.x * 0.5f;
    float cy = mFrameDim.y * 0.5f;

    ImColor color(mColor.x, mColor.y, mColor.z, mColor.w);

    // 가로선: 중앙 gap을 비워두고 양쪽으로 그림
    drawList->AddLine(
        ImVec2(cx - mHalfLen, cy),
        ImVec2(cx - mGap,     cy),
        color, mThickness
    );
    drawList->AddLine(
        ImVec2(cx + mGap,     cy),
        ImVec2(cx + mHalfLen, cy),
        color, mThickness
    );

    // 세로선: 중앙 gap을 비워두고 위아래로 그림
    drawList->AddLine(
        ImVec2(cx, cy - mHalfLen),
        ImVec2(cx, cy - mGap    ),
        color, mThickness
    );
    drawList->AddLine(
        ImVec2(cx, cy + mGap    ),
        ImVec2(cx, cy + mHalfLen),
        color, mThickness
    );
}

void OverlayPass::renderUI(Gui::Widgets& widget)
{
    // Mogwai UI 패널에서 실시간으로 크로스헤어 모양 조정 가능
    widget.slider("팔 길이 (px)",   mHalfLen,   1.f, 50.f);
    widget.slider("선 두께 (px)",   mThickness, 0.5f, 5.f);
    widget.slider("중앙 공백 (px)", mGap,       0.f, 20.f);
    widget.var("색상",              mColor,     0.f, 1.f);
}

void OverlayPass::registerBindings(pybind11::module& m)
{
    // 필요 시 Python 바인딩 추가
}
