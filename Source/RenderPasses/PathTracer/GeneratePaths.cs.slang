/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
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
import Utils.Attributes;
import Utils.Color.ColorHelpers;
import Rendering.RTXDI.RTXDI;
import RenderPasses.Shared.Denoising.NRDBuffers;
import RenderPasses.Shared.Denoising.NRDConstants;
import LoadShadingData;
import NRDHelpers;
import PathTracer;
import PathState;
import ColorType;
import Params;

// Shared memory variables.
// TODO: Can we declare these inside PathGenerator?
static const uint kWarpCount = (kScreenTileDim.x * kScreenTileDim.y) / 32;

// TODO: Replace explicitly declared size by compile-time constant when it works. For now assume tile is always 16x16!
groupshared uint gSamplesOffset[8 /* kWarpCount */];

#if USE_IDEA_AB
// [아이디어 B] 타일 내 모든 픽셀의 spp 합이 0인지 나타내는 플래그.
// execute() 안에서 첫 번째 GroupMemoryBarrierWithGroupSync() 직후,
// prefix sum 연산을 수행하기 전에 threadIdx == 0이 설정한다.
groupshared bool gTileAllZeroSpp;
#endif

/** Helper struct for generating paths in screen space.

    The dispatch size is one thread group per screen tile. A warp is assumed to be 32 threads.
    Within a thread group, the threads are linearly indexed and mapped to pixels in Morton order.

    Output sample buffer
    --------------------

    For each pixel that belongs to the background, and hence does not need to be path traced,
    we directly evaluate the background color and write all samples to the output sample buffers.

    The output sample buffers are organized by tiles in scanline order. Within tiles,
    the pixels are enumerated in Morton order with all samples for a pixel stored consecutively.

    When the number of samples/pixel is not fixed, we additionally write a 2D lookup table,
    for each pixel storing the tile-local offset to where the first sample is stored.
    Based on this information, subsequent passes can easily find the location of a given sample.
*/
struct PathGenerator
{
    // Resources
    PathTracerParams params;                        ///< Runtime parameters.

    Texture2D<PackedHitInfo> vbuffer;               ///< Fullscreen V-buffer for the primary hits.
    Texture2D<float3> viewDir;                      ///< Optional view direction. Only valid when kUseViewDir == true.
    Texture2D<uint> sampleCount;                    ///< Optional input sample count buffer. Only valid when kSamplesPerPixel == 0.
    RWTexture2D<uint> sampleOffset;                 ///< Output offset into per-sample buffers. Only valid when kSamplesPerPixel == 0.

    RWStructuredBuffer<ColorType> sampleColor;      ///< Output per-sample color if kSamplesPerPixel != 1.
    RWStructuredBuffer<GuideData> sampleGuideData;  ///< Output per-sample guide data.
    NRDBuffers outputNRD;                           ///< Output NRD data.

    RWTexture2D<float4> outputColor;                ///< Output color buffer if kSamplesPerPixel == 1.

    // Render settings that depend on the scene.
    // TODO: Move into scene defines.
    static const bool kUseEnvLight = USE_ENV_LIGHT;
    static const bool kUseCurves = USE_CURVES;

    // Additional specialization.
    static const bool kOutputGuideData = OUTPUT_GUIDE_DATA;

    /** Entry point for path generator.
        \param[in] tileID Tile ID in x and y on screen.
        \param[in] threadIdx Thread index within the tile.
    */
    void execute(const uint2 tileID, const uint threadIdx)
    {
        // Map thread to pixel based on Morton order within tile.
        // The tiles themselves are enumerated in scanline order on screen.
        const uint2 tileOffset = tileID << kScreenTileBits; // Tile offset in pixels.
        const uint2 pixel = deinterleave_8bit(threadIdx) + tileOffset; // Assumes 16x16 tile or smaller. A host-side assert checks this assumption.

        // Process each pixel.
        // If we don't hit any surface then the background will be evaluated and written out directly.
        Ray cameraRay;
        bool hitSurface = false;
        uint spp = 0;

        // Note: Do not terminate threads for out-of-bounds pixels because we need all threads active for the prefix sum pass below.
        if (all(pixel < params.frameDim))
        {
            // Determine number of samples at the current pixel.
            // This is either a fixed number or loaded from the sample count texture.
            // TODO: We may want to use a nearest sampler to allow the texture to be of arbitrary dimension.
            spp = kSamplesPerPixel > 0 ? kSamplesPerPixel : min(sampleCount[pixel], kMaxSamplesPerPixel);

            // Compute the primary ray.
            cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);
            if (kUseViewDir) cameraRay.dir = -viewDir[pixel];

            // Load the primary hit from the V-buffer.
            const HitInfo hit = unpackHitInfo(vbuffer[pixel]);
            hitSurface = hit.isValid();

#if USE_IDEA_AB
            // [아이디어 A] 낮은 중요도 픽셀의 조기 종료.
            // SampleCountPass가 spp == 0을 배정한 표면 픽셀은 TracePass를 거칠 필요가 없다.
            // hitSurface = false로 override하면:
            //   (1) 아래 RTXDI 블록에서 setSurfaceData 비용이 스킵된다.
            //   (2) prefix sum 이후 writeBackground()가 호출되어
            //       outputColor / NRD / guide 버퍼에 0(또는 배경색)이 올바르게 기록된다.
            // 이 분기는 kSamplesPerPixel == 0 (가변 spp 모드)에서만 의미가 있다.
            if (hitSurface && spp == 0)
            {
                hitSurface = false;
            }
#endif

            // Prepare per-pixel surface data for RTXDI.
            if (kUseRTXDI)
            {
                bool validSurface = false;
                if (hitSurface)
                {
                    ShadingData sd = loadShadingData(hit, cameraRay.origin, cameraRay.dir);

                    // Create material instance and query its properties.
                    let lod = ExplicitLodTextureSampler(0.f);
                    let hints = getMaterialInstanceHints(hit, true /* primary hit */);
                    let mi = gScene.materials.getMaterialInstance(sd, lod, hints);
                    let bsdfProperties = mi.getProperties(sd);

                    // Check for BSDF lobes that RTXDI can sample.
                    uint lobeTypes = mi.getLobeTypes(sd);
                    validSurface = (lobeTypes & (uint)LobeType::NonDeltaReflection) != 0;

                    if (validSurface)
                    {
                        // RTXDI uses a simple material model with only diffuse and specular reflection lobes.
                        // We query the BSDF for the diffuse albedo and specular reflectance.
                        gRTXDI.setSurfaceData(pixel, sd.computeRayOrigin(), bsdfProperties.guideNormal, bsdfProperties.diffuseReflectionAlbedo, bsdfProperties.specularReflectance, bsdfProperties.roughness);
                    }
                }
                if (!validSurface) gRTXDI.setInvalidSurfaceData(pixel);
            }
        }

        // Perform a reduction over the tile to determine the number of samples required.
        // This is done via wave ops and shared memory.
        // The write offsets are given by prefix sums over the threads.
        const uint warpIdx = threadIdx >> 5;

        // Calculate the sample counts over the warp.
        // The first thread in each warp writes the results to shared memory.
        {
            uint samples = WaveActiveSum(spp);

            if (WaveIsFirstLane())
            {
                gSamplesOffset[warpIdx] = samples;
            }
        }
        GroupMemoryBarrierWithGroupSync();

#if USE_IDEA_AB
        // [아이디어 B] 타일 단위 중요도 조기 감지.
        // 첫 번째 동기화 이후 시점에서 gSamplesOffset[i]에는
        // 각 warp의 spp 합계(WaveActiveSum 결과)가 저장되어 있다.
        // threadIdx == 0이 이를 모두 합산하여 타일 총합을 계산한다.
        // 총합 == 0이면 타일 내 모든 픽셀이 low-importance(spp == 0)이므로
        // 이후 prefix sum의 WavePrefixSum() 호출과 sampleOffset 쓰기를 생략할 수 있다.
        if (threadIdx == 0)
        {
            uint tileSum = 0;
            for (uint i = 0; i < kWarpCount; i++)
                tileSum += gSamplesOffset[i];   // prefix sum 전 → 각 warp의 spp 합
            gTileAllZeroSpp = (tileSum == 0);
        }
        GroupMemoryBarrierWithGroupSync();
#endif

        // Compute the prefix sum over the warp totals in shared memory.
        // The first N threads in the thread group perform this computation.
        if (threadIdx < kWarpCount)
        {
#if USE_IDEA_AB
            // [아이디어 B] 타일 전체 spp가 0이면 prefix sum 연산 자체를 생략한다.
            // gSamplesOffset[i] == 0이 이미 확정되어 있으므로 그대로 두면 된다.
            if (!gTileAllZeroSpp)
#endif
            {
                // Compute the prefix sum over the sample counts.
                uint samples = gSamplesOffset[threadIdx];
                gSamplesOffset[threadIdx] = WavePrefixSum(samples);
            }
        }
        GroupMemoryBarrierWithGroupSync();

        if (all(pixel < params.frameDim))
        {
            // Compute the output sample index.
            // For a fixed sample count, the output index is computed directly from the thread index.
            // For a variable sample count, the output index is given by the prefix sum over sample counts.
            const uint outTileOffset = params.getTileOffset(tileID);
            uint outIdx = 0;

            if (kSamplesPerPixel > 0)
            {
                outIdx = outTileOffset + threadIdx * kSamplesPerPixel;
            }
            else
            {
#if USE_IDEA_AB
                // [아이디어 B] 타일 전체 spp == 0이면 WavePrefixSum() 호출을 생략한다.
                // gSamplesOffset[warpIdx] == 0, WavePrefixSum(0) == 0 이 자명하므로
                // outSampleOffset = 0으로 직접 설정한다.
                uint outSampleOffset;
                if (gTileAllZeroSpp)
                {
                    outSampleOffset = 0;
                }
                else
                {
                    outSampleOffset = gSamplesOffset[warpIdx] + WavePrefixSum(spp);
                }
#else
                uint outSampleOffset = gSamplesOffset[warpIdx] + WavePrefixSum(spp);
#endif
                outIdx = outTileOffset + outSampleOffset;

                // Write sample offset lookup table. This will be used by later passes.
                sampleOffset[pixel] = outSampleOffset;
            }

            if (!hitSurface)
            {
                // Write background pixels.
                writeBackground(pixel, spp, outIdx, cameraRay.dir);
            }
        }
    }

    void writeBackground(const uint2 pixel, const uint spp, const uint outIdx, const float3 dir)
    {
        // Evaluate background color for the current pixel.
        float3 color = float3(0.f);
        if (kUseEnvLight)
        {
            color = gScene.envMap.eval(dir);
        }

        // Write color and denoising guide data for all samples in pixel.
        // For the special case of fixed 1 spp we write the color directly to the output texture.
        if (kSamplesPerPixel == 1)
        {
            outputColor[pixel] = float4(color, 1.f);
        }

        for (uint i = 0; i < spp; i++)
        {
            if (kSamplesPerPixel != 1)
            {
                sampleColor[outIdx + i].set(color);
            }

            if (kOutputGuideData)
            {
                PathTracer::setBackgroundGuideData(sampleGuideData[outIdx + i], dir, color);
            }

            if (kOutputNRDData)
            {
                outputNRD.sampleRadiance[outIdx + i] = {};
                outputNRD.sampleHitDist[outIdx + i] = kNRDInvalidPathLength;
                outputNRD.sampleEmission[outIdx + i] = 0.f;
                outputNRD.sampleReflectance[outIdx + i] = 1.f;
            }
        }

        if (kOutputNRDData)
        {
            outputNRD.primaryHitEmission[pixel] = float4(color, 1.f);
            outputNRD.primaryHitDiffuseReflectance[pixel] = 0.f;
            outputNRD.primaryHitSpecularReflectance[pixel] = 0.f;
        }

        if (kOutputNRDAdditionalData)
        {
            writeNRDDeltaReflectionGuideBuffers(outputNRD, kUseNRDDemodulation, pixel, 0.f, 0.f, -dir, 0.f, kNRDInvalidPathLength, kNRDInvalidPathLength);
            writeNRDDeltaTransmissionGuideBuffers(outputNRD, kUseNRDDemodulation, pixel, 0.f, 0.f, -dir, 0.f, kNRDInvalidPathLength, 0.f);
        }
    }
};

cbuffer CB
{
    PathGenerator gPathGenerator;
}

// TODO: Replace by compile-time uint2 constant when it works in Slang.
[numthreads(256 /* kScreenTileDim.x * kScreenTileDim.y, 16x16 */, 1, 1)]
void main(
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID)
{
    gPathGenerator.execute(groupID.xy, groupThreadID.x);
}
