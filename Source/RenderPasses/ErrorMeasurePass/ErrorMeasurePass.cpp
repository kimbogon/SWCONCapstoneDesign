/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "ErrorMeasurePass.h"
#include "Core/AssetResolver.h"
#include <sstream>
#include <cmath>    // std::log10, std::isinf, std::isnan

namespace
{
const std::string kErrorComputationShaderFile = "RenderPasses/ErrorMeasurePass/ErrorMeasurer.cs.slang";
const std::string kConstantBufferName = "PerFrameCB";

// Input channels
const std::string kInputChannelWorldPosition = "WorldPosition";
const std::string kInputChannelSourceImage = "Source";
const std::string kInputChannelReferenceImage = "Reference";

// Output channel
const std::string kOutputChannelImage = "Output";

// Serialized parameters
const std::string kReferenceImagePath = "ReferenceImagePath";
const std::string kMeasurementsFilePath = "MeasurementsFilePath";
const std::string kIgnoreBackground = "IgnoreBackground";
const std::string kComputeSquaredDifference = "ComputeSquaredDifference";
const std::string kComputeAverage = "ComputeAverage";
const std::string kUseLoadedReference = "UseLoadedReference";
const std::string kReportRunningError = "ReportRunningError";
const std::string kRunningErrorSigma = "RunningErrorSigma";
const std::string kSelectedOutputId = "SelectedOutputId";

// [요구사항 5-3] MSE=0 시 PSNR 상한 클램프 값 (inf 방지)
constexpr float kPSNRMax = 999.0f;
// MSE가 이 값 이하면 최대 PSNR을 반환 (division-by-zero 방지)
constexpr float kMSEMinThreshold = 1e-10f;
} // namespace

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ErrorMeasurePass>();
}

const Gui::RadioButtonGroup ErrorMeasurePass::sOutputSelectionButtons = {
    {(uint32_t)OutputId::Source, "Source", true},
    {(uint32_t)OutputId::Reference, "Reference", true},
    {(uint32_t)OutputId::Difference, "Difference", true}};

const Gui::RadioButtonGroup ErrorMeasurePass::sOutputSelectionButtonsSourceOnly = {{(uint32_t)OutputId::Source, "Source", true}};

ErrorMeasurePass::ErrorMeasurePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    for (const auto& [key, value] : props)
    {
        if (key == kReferenceImagePath)
            mReferenceImagePath = value.operator std::filesystem::path();
        else if (key == kMeasurementsFilePath)
            mMeasurementsFilePath = value.operator std::filesystem::path();
        else if (key == kIgnoreBackground)
            mIgnoreBackground = value;
        else if (key == kComputeSquaredDifference)
            mComputeSquaredDifference = value;
        else if (key == kComputeAverage)
            mComputeAverage = value;
        else if (key == kUseLoadedReference)
            mUseLoadedReference = value;
        else if (key == kReportRunningError)
            mReportRunningError = value;
        else if (key == kRunningErrorSigma)
            mRunningErrorSigma = value;
        else if (key == kSelectedOutputId)
            mSelectedOutputId = value;
        else
        {
            logWarning("Unknown property '{}' in ErrorMeasurePass properties.", key);
        }
    }

    // Load/create files (if specified in config).
    loadReference();
    loadMeasurementsFile();

    mpParallelReduction = std::make_unique<ParallelReduction>(mpDevice);
    mpErrorMeasurerPass = ComputePass::create(mpDevice, kErrorComputationShaderFile);
}

Properties ErrorMeasurePass::getProperties() const
{
    Properties props;
    props[kReferenceImagePath] = mReferenceImagePath;
    props[kMeasurementsFilePath] = mMeasurementsFilePath;
    props[kIgnoreBackground] = mIgnoreBackground;
    props[kComputeSquaredDifference] = mComputeSquaredDifference;
    props[kComputeAverage] = mComputeAverage;
    props[kUseLoadedReference] = mUseLoadedReference;
    props[kReportRunningError] = mReportRunningError;
    props[kRunningErrorSigma] = mRunningErrorSigma;
    props[kSelectedOutputId] = mSelectedOutputId;
    return props;
}

RenderPassReflection ErrorMeasurePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    reflector.addInput(kInputChannelSourceImage, "Source image");
    reflector.addInput(kInputChannelReferenceImage, "Reference image (optional)").flags(RenderPassReflection::Field::Flags::Optional);
    // [요구사항 5-2] WorldPosition은 Optional: 없어도 크래시 없이 동작
    reflector.addInput(kInputChannelWorldPosition, "World-space position").flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addOutput(kOutputChannelImage, "Output image").format(ResourceFormat::RGBA32Float);
    return reflector;
}

void ErrorMeasurePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    ref<Texture> pSourceImageTexture = renderData.getTexture(kInputChannelSourceImage);
    ref<Texture> pOutputImageTexture = renderData.getTexture(kOutputChannelImage);

    // Create the texture for the difference image if this is our first
    // time through or if the source image resolution has changed.
    const uint32_t width = pSourceImageTexture->getWidth(), height = pSourceImageTexture->getHeight();
    if (!mpDifferenceTexture || mpDifferenceTexture->getWidth() != width || mpDifferenceTexture->getHeight() != height)
    {
        mpDifferenceTexture = mpDevice->createTexture2D(
            width,
            height,
            ResourceFormat::RGBA32Float,
            1,
            1,
            nullptr,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
        );
        FALCOR_ASSERT(mpDifferenceTexture);
    }

    mMeasurements.valid = false;

    ref<Texture> pReference = getReference(renderData);
    if (!pReference)
    {
        // We don't have a reference image, so just copy the source image to the output.
        pRenderContext->blit(pSourceImageTexture->getSRV(), pOutputImageTexture->getRTV());
        return;
    }

    // [요구사항 5-1] Reference 와 Source 해상도 불일치 시 처리
    // Reference 크기가 Source 와 다른 경우, 경고 로그를 남기고 Source 를 그대로 출력한다.
    // (blit 으로 스케일 복사하는 방식도 가능하나, 오차 지표가 왜곡되므로 스킵이 더 안전)
    {
        const uint32_t refW = pReference->getWidth(), refH = pReference->getHeight();
        if (refW != width || refH != height)
        {
            logWarning(
                "ErrorMeasurePass: Source resolution ({}x{}) differs from Reference resolution ({}x{}). "
                "Skipping error measurement for this frame.",
                width, height, refW, refH
            );
            pRenderContext->blit(pSourceImageTexture->getSRV(), pOutputImageTexture->getRTV());
            return;
        }
    }

    runDifferencePass(pRenderContext, renderData);
    runReductionPasses(pRenderContext, renderData);

    switch (mSelectedOutputId)
    {
    case OutputId::Source:
        pRenderContext->blit(pSourceImageTexture->getSRV(), pOutputImageTexture->getRTV());
        break;
    case OutputId::Reference:
        pRenderContext->blit(pReference->getSRV(), pOutputImageTexture->getRTV());
        break;
    case OutputId::Difference:
        pRenderContext->blit(mpDifferenceTexture->getSRV(), pOutputImageTexture->getRTV());
        break;
    default:
        FALCOR_THROW("ErrorMeasurePass: Unhandled OutputId case");
    }

    saveMeasurementsToFile();
}

void ErrorMeasurePass::runDifferencePass(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Bind textures.
    ref<Texture> pSourceTexture = renderData.getTexture(kInputChannelSourceImage);

    // [요구사항 5-2] WorldPosition 이 없으면 nullptr 이 되고, 셰이더 측 gIgnoreBackground=0 으로 처리됨
    ref<Texture> pWorldPositionTexture = renderData.getTexture(kInputChannelWorldPosition);

    auto var = mpErrorMeasurerPass->getRootVar();
    var["gReference"] = getReference(renderData);
    var["gSource"] = pSourceTexture;
    var["gWorldPosition"] = pWorldPositionTexture; // nullptr 이어도 Optional 이므로 안전
    var["gResult"] = mpDifferenceTexture;

    // Set constant buffer parameters.
    const uint2 resolution = uint2(pSourceTexture->getWidth(), pSourceTexture->getHeight());
    var[kConstantBufferName]["gResolution"] = resolution;
    // WorldPosition 이 nullptr 이면 배경 무시 기능 자동 비활성화
    var[kConstantBufferName]["gIgnoreBackground"] = (uint32_t)(mIgnoreBackground && pWorldPositionTexture);
    var[kConstantBufferName]["gComputeDiffSqr"] = (uint32_t)mComputeSquaredDifference;
    var[kConstantBufferName]["gComputeAverage"] = (uint32_t)mComputeAverage;

    // Run the compute shader.
    mpErrorMeasurerPass->execute(pRenderContext, resolution.x, resolution.y);
}

void ErrorMeasurePass::runReductionPasses(RenderContext* pRenderContext, const RenderData& renderData)
{
    float4 error;
    mpParallelReduction->execute(pRenderContext, mpDifferenceTexture, ParallelReduction::Type::Sum, &error);

    const float pixelCountf = static_cast<float>(mpDifferenceTexture->getWidth() * mpDifferenceTexture->getHeight());
    mMeasurements.error = error.xyz() / pixelCountf;
    mMeasurements.avgError = (mMeasurements.error.x + mMeasurements.error.y + mMeasurements.error.z) / 3.f;
    mMeasurements.valid = true;

    // ------------------------------------------------------------------
    // [요구사항 2] PSNR 계산
    // PSNR = 10 * log10(1.0 / MSE)  (max signal value = 1.0 기준)
    // MSE 가 0 이거나 극히 작을 때 inf/nan 방지를 위해 임계값 아래면 kPSNRMax 클램프
    // ------------------------------------------------------------------
    if (mComputeSquaredDifference)
    {
        // [요구사항 5-3] MSE <= kMSEMinThreshold 이면 최대값으로 클램프 (division-by-zero 방지)
        if (mMeasurements.avgError <= kMSEMinThreshold)
        {
            mMeasurements.psnr = kPSNRMax;
        }
        else
        {
            float psnr = 10.0f * std::log10(1.0f / mMeasurements.avgError);
            // 혹시라도 inf/nan 이 되었을 경우 추가 방어
            if (std::isinf(psnr) || std::isnan(psnr))
                psnr = kPSNRMax;
            mMeasurements.psnr = psnr;
        }
    }
    else
    {
        // L1 모드에서는 PSNR 정의가 모호하므로 무효값으로 설정
        mMeasurements.psnr = -1.0f;
    }

    if (mRunningAvgError < 0)
    {
        // The running error values are invalid. Start them off with the current frame's error.
        mRunningError = mMeasurements.error;
        mRunningAvgError = mMeasurements.avgError;
    }
    else
    {
        mRunningError = mRunningErrorSigma * mRunningError + (1 - mRunningErrorSigma) * mMeasurements.error;
        mRunningAvgError = mRunningErrorSigma * mRunningAvgError + (1 - mRunningErrorSigma) * mMeasurements.avgError;
    }
}

void ErrorMeasurePass::renderUI(Gui::Widgets& widget)
{
    const auto getFilename = [](const std::filesystem::path& path) { return path.empty() ? "N/A" : path.filename().string(); };

    // Create a button for loading the reference image.
    if (widget.button("Load reference"))
    {
        FileDialogFilterVec filters;
        filters.push_back({"exr", "High Dynamic Range"});
        filters.push_back({"pfm", "Portable Float Map"});
        std::filesystem::path path;
        if (openFileDialog(filters, path))
        {
            mReferenceImagePath = path;
            if (!loadReference())
                msgBox("Error", fmt::format("Failed to load reference image from '{}'.", path), MsgBoxType::Ok, MsgBoxIcon::Error);
        }
    }

    // Create a button for defining the measurements output file.
    if (widget.button("Set output data file", true))
    {
        FileDialogFilterVec filters;
        filters.push_back({"csv", "CSV Files"});
        std::filesystem::path path;
        if (saveFileDialog(filters, path))
        {
            mMeasurementsFilePath = path;
            if (!loadMeasurementsFile())
                msgBox("Error", fmt::format("Failed to save measurements to '{}'.", path), MsgBoxType::Ok, MsgBoxIcon::Error);
        }
    }

    // Radio buttons to select the output.
    widget.text("Show:");
    if (mMeasurements.valid)
    {
        widget.radioButtons(sOutputSelectionButtons, reinterpret_cast<uint32_t&>(mSelectedOutputId));
        widget.tooltip(
            "Press 'O' to change output mode; hold 'Shift' to reverse the cycling.\n\n"
            "Note: Difference is computed based on current - reference value.",
            true
        );
    }
    else
    {
        uint32_t dummyId = 0;
        widget.radioButtons(sOutputSelectionButtonsSourceOnly, dummyId);
    }

    widget.checkbox("Ignore background", mIgnoreBackground);
    widget.tooltip(
        "Do not include background pixels in the error measurements.\n"
        "This option requires the optional input '" +
            std::string(kInputChannelWorldPosition) + "' to be bound",
        true
    );
    widget.checkbox("Compute L2 error (rather than L1)", mComputeSquaredDifference);
    widget.checkbox("Compute RGB average", mComputeAverage);
    widget.tooltip(
        "When enabled, the average error over the RGB components is computed when creating the difference image.\n"
        "The average is computed after squaring the differences when L2 error is selected."
    );

    widget.checkbox("Use loaded reference image", mUseLoadedReference);
    widget.tooltip(
        "Take the reference from the loaded image instead or the input channel.\n\n"
        "If the chosen reference doesn't exist, the error measurements are disabled.",
        true
    );
    // Display the filename of the reference file.
    const std::string referenceText = "Reference: " + getFilename(mReferenceImagePath);
    widget.text(referenceText);
    if (!mReferenceImagePath.empty())
    {
        widget.tooltip(mReferenceImagePath.string());
    }

    // Display the filename of the measurement file.
    const std::string outputText = "Output: " + getFilename(mMeasurementsFilePath);
    widget.text(outputText);
    if (!mMeasurementsFilePath.empty())
    {
        widget.tooltip(mMeasurementsFilePath.string());
    }

    // Print numerical error (scalar and RGB).
    if (widget.checkbox("Report running error", mReportRunningError) && mReportRunningError)
    {
        // The checkbox was enabled; mark the running error values invalid so that they start fresh.
        mRunningAvgError = -1.f;
    }
    widget.tooltip("Exponential moving average, sigma = " + std::to_string(mRunningErrorSigma));
    if (mMeasurements.valid)
    {
        // Use stream so we can control formatting.
        std::ostringstream oss;
        oss << std::scientific;
        oss << (mComputeSquaredDifference ? "MSE (avg): " : "L1 error (avg): ")
            << (mReportRunningError ? mRunningAvgError : mMeasurements.avgError) << std::endl;
        oss << (mComputeSquaredDifference ? "MSE (rgb): " : "L1 error (rgb): ")
            << (mReportRunningError ? mRunningError.r : mMeasurements.error.r) << ", "
            << (mReportRunningError ? mRunningError.g : mMeasurements.error.g) << ", "
            << (mReportRunningError ? mRunningError.b : mMeasurements.error.b);
        widget.text(oss.str());

        // [요구사항 3] PSNR 출력 (MSE 모드일 때만 의미 있는 값)
        if (mComputeSquaredDifference)
        {
            // MSE 값을 고정소수점으로 별도 표시
            widget.text(fmt::format("MSE: {:.6f}", mMeasurements.avgError));
            // PSNR 값 표시 (dB 단위); kPSNRMax 이면 "inf (clamped)" 로 표현
            if (mMeasurements.psnr >= kPSNRMax)
                widget.text("PSNR: inf (clamped) dB");
            else
                widget.text(fmt::format("PSNR: {:.2f} dB", mMeasurements.psnr));
        }
    }
    else
    {
        widget.text("Error: N/A");
    }
}

bool ErrorMeasurePass::onKeyEvent(const KeyboardEvent& keyEvent)
{
    if (keyEvent.type == KeyboardEvent::Type::KeyPressed && keyEvent.key == Input::Key::O)
    {
        int32_t ofs = keyEvent.hasModifier(Input::Modifier::Shift) ? -1 : 1;
        int32_t index = (int32_t)mSelectedOutputId;
        index = (index + ofs + (int32_t)OutputId::Count) % (int32_t)OutputId::Count;
        mSelectedOutputId = (OutputId)index;
        return true;
    }

    return false;
}

bool ErrorMeasurePass::loadReference()
{
    if (mReferenceImagePath.empty())
        return false;

    std::filesystem::path resolvedPath = AssetResolver::getDefaultResolver().resolvePath(mReferenceImagePath);
    mpReferenceTexture = Texture::createFromFile(mpDevice, resolvedPath, false /* no MIPs */, false /* linear color */);
    if (!mpReferenceTexture)
    {
        logWarning("Failed to load texture from '{}'", mReferenceImagePath);
        mReferenceImagePath.clear();
        return false;
    }

    mUseLoadedReference = mpReferenceTexture != nullptr;
    mRunningAvgError = -1.f; // Mark running error values as invalid.
    return true;
}

ref<Texture> ErrorMeasurePass::getReference(const RenderData& renderData) const
{
    return mUseLoadedReference ? mpReferenceTexture : renderData.getTexture(kInputChannelReferenceImage);
}

bool ErrorMeasurePass::loadMeasurementsFile()
{
    if (mMeasurementsFilePath.empty())
        return false;

    mMeasurementsFile = std::ofstream(mMeasurementsFilePath, std::ios::trunc);
    if (!mMeasurementsFile)
    {
        logWarning(fmt::format("Failed to open file '{}'.", mMeasurementsFilePath));
        mMeasurementsFilePath.clear();
        return false;
    }
    else
    {
        // [요구사항 4] CSV 헤더에 psnr 컬럼 추가 (frame, mse, psnr 순서)
        if (mComputeSquaredDifference)
        {
            mMeasurementsFile << "frame,avg_L2_error,red_L2_error,green_L2_error,blue_L2_error,psnr" << std::endl;
        }
        else
        {
            mMeasurementsFile << "frame,avg_L1_error,red_L1_error,green_L1_error,blue_L1_error,psnr" << std::endl;
        }
        mMeasurementsFile << std::scientific;
    }

    return true;
}

void ErrorMeasurePass::saveMeasurementsToFile()
{
    if (!mMeasurementsFile)
        return;

    FALCOR_ASSERT(mMeasurements.valid);

    // [요구사항 4] frame 카운터, MSE, RGB 오차, PSNR 순서로 기록
    mMeasurementsFile << mFrameIndex << ",";
    mMeasurementsFile << mMeasurements.avgError << ",";
    mMeasurementsFile << mMeasurements.error.r << ","
                      << mMeasurements.error.g << ","
                      << mMeasurements.error.b << ",";

    // PSNR: L1 모드이거나 무효(-1)이면 "N/A", 클램프 상한이면 "inf" 로 기록
    if (mMeasurements.psnr < 0.0f)
        mMeasurementsFile << "N/A";
    else if (mMeasurements.psnr >= kPSNRMax)
        mMeasurementsFile << "inf";
    else
        mMeasurementsFile << mMeasurements.psnr;

    mMeasurementsFile << std::endl;

    // 프레임 카운터 증가
    ++mFrameIndex;
}
