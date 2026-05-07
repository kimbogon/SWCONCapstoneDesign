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
__exported import Scene.Shading;
__exported import Utils.Timing.GpuTimer;
__exported import Utils.Math.Ray;
import Utils.Math.MathHelpers;
import Utils.Sampling.SampleGenerator;
import Utils.Debug.PixelDebug;
import Rendering.Materials.TexLODTypes;
import Rendering.Materials.TexLODHelpers;
import GBufferHelpers;

// GBuffer channels
RWTexture2D<float4> gPosW;
RWTexture2D<float4> gNormW;
RWTexture2D<float4> gTangentW;
RWTexture2D<float4> gFaceNormalW;
RWTexture2D<float2> gTexC;
RWTexture2D<float4> gTexGrads;
RWTexture2D<float2> gMotionVector;
RWTexture2D<uint4>  gMaterialData;

// GBufferRT channels
RWTexture2D<PackedHitInfo> gVBuffer;
RWTexture2D<float>  gDepth;
RWTexture2D<float2> gLinearZ;
RWTexture2D<float4> gMotionVectorW;
RWTexture2D<float4> gNormalWRoughnessMaterialID;
RWTexture2D<float4> gGuideNormalW;
RWTexture2D<float4> gDiffOpacity;
RWTexture2D<float4> gSpecRough;
RWTexture2D<float4> gEmissive;
RWTexture2D<float4> gViewW;
RWTexture2D<uint>   gTime;
RWTexture2D<float>  gDisocclusion;
RWTexture2D<float>  gMask;

// [ShadowCount] Shadow count output buffer — 차폐된 광원의 수
RWTexture2D<uint>   gShadowCount;

// [ObjectID] Geometry instance index (mesh instance 고유 번호).
// material ID 와 달리 같은 material 을 공유하는 다른 mesh instance 를 구분한다.
// GeometryInstanceID.index 를 uint 로 직접 기록한다.
RWTexture2D<uint>   gObjectID;

// [Luminance] diffuse albedo 로부터 계산된 per-pixel luminance (R32Float).
// dot(diffuse.rgb, float3(0.299, 0.587, 0.114)) 로 계산한다.
RWTexture2D<float>  gLuminance;

// [CenterDist] 정규화된 스크린 좌표(UV [0,1]^2) 기준
// 화면 중심(0.5, 0.5)으로부터의 유클리드 거리 (R32Float).
// 최대값 ≈ 0.707 (모서리). ImportancePass 에서 그대로 사용하거나 재정규화 가능.
RWTexture2D<float>  gCenterDist;

// [Metalness] StandardMaterial 원본 metalness 값 [0,1] (R32Float).
// BSDFProperties 에는 metalness 필드가 노출되지 않는다.
// gScene.materials 의 getMaterialData() 를 통해 packed 4xUint8 헤더에서 직접 읽는다.
// StandardMaterial 의 data layout (Scene/Material/StandardMaterial.slang):
//   header.data[0]: MaterialType (uint8) | doubleSided (uint8) | ...
//   header.data[1]: baseColor 텍스처 슬롯 flags (uint8 packed)
//   payload word 1 (MaterialPayload): baseColor(RGBA8), specular(RGBA8) ...
//   specular 채널 구성: R=Occlusion, G=Roughness, B=Metalness, A=reserved
// Falcor 8.0 StandardMaterial packed layout 에서 metalness 는
// getMaterialData() 가 반환하는 MaterialData 구조체의 payload 를 통해 접근한다.
// 구체적으로 IMaterial::getData() -> MaterialHeader + MaterialPayload 에서
// specular texture 의 B채널(= metalness raw value) 을 언팩한다.
// 단, 이 값은 texture 가 없을 때 constant 로 packed 된 값이다.
// Texture 가 있는 경우 실제 per-pixel metalness 는 texture sample 이 필요하다.
// writeHit() 에서 sd (ShadingData) 로부터 material instance 를 통해 추출한다.
RWTexture2D<float>  gMetalness;

#define is_valid(name) (is_valid_##name != 0)

#if !defined(COMPUTE_DEPTH_OF_FIELD) || !defined(USE_ALPHA_TEST) || !defined(LOD_MODE) || !defined(ADJUST_SHADING_NORMALS) || !defined(RAY_FLAGS)
#error "Not all defines are set!"
#endif

struct GBufferRT
{
    static const bool kComputeDepthOfField = COMPUTE_DEPTH_OF_FIELD;
    static const bool kUseAlphaTest = USE_ALPHA_TEST;
    static const TexLODMode kLODMode = TexLODMode(LOD_MODE);
    static const bool kAdjustShadingNormals = ADJUST_SHADING_NORMALS;
    static const uint kRayFlags = RAY_FLAGS;
    static const float kEnvMapDepth = 100000000.0f;

    // [ShadowCount] 셀프-인터섹션 방지용 ray origin offset.
    static const float kShadowRayEpsilon = 1e-4f;

    uint2 frameDim;
    float2 invFrameDim;
    uint frameCount;
    float screenSpacePixelSpreadAngle;

    /**
     * Ray differentials for primary hit. Code from RayTracingGems, Chapter 20.
     */
    void computeRayDifferentials(
        const TriangleHit hit,
        float3 rayDir,
        float hitT,
        const Camera camera,
        float2 invFrameDim,
        out float2 ddx,
        out float2 ddy
    )
    {
        float3 P[3];
        gScene.getVertexPositionsW(hit.instanceID, hit.primitiveIndex, P);
        float3 e1 = P[1] - P[0];
        float3 e2 = P[2] - P[0];
        float3 d = rayDir;
        float k = dot(cross(e1, e2), d);
        k = abs(k) > 1e-20f ? rcp(k) : 0.0f;
        float3 cu = cross(e2, d);
        float3 cv = cross(d, e1);
        float3 dx = camera.data.cameraU * 2.f * invFrameDim.x / camera.data.focalDistance;
        float3 dy = camera.data.cameraV * 2.f * invFrameDim.y / camera.data.focalDistance;
        float3 q = dx * hitT;
        float3 r = dy * hitT;
        float dudx = k * dot(cu, q);
        float dudy = k * dot(cu, r);
        float dvdx = k * dot(cv, q);
        float dvdy = k * dot(cv, r);
        float2 T[3];
        gScene.getVertexTexCoords(hit.instanceID, hit.primitiveIndex, T);
        float2 g1 = T[1] - T[0];
        float2 g2 = T[2] - T[0];
        float dsdx = (dudx * g1.x + dvdx * g2.x);
        float dsdy = (dudy * g1.x + dvdy * g2.x);
        float dtdx = (dudx * g1.y + dvdx * g2.y);
        float dtdy = (dudy * g1.y + dvdy * g2.y);
        ddx = float2(dsdx, dtdx);
        ddy = float2(dsdy, dtdy);
    }

    void computeAnisotropicAxesRayCones(
        const TriangleHit hit,
        VertexData v,
        float3 rayDir,
        float hitT,
        float pixelAngle,
        out float2 ddx,
        out float2 ddy
    )
    {
        float3 positions[3];
        float2 texCoords[3];
        gScene.getVertexPositionsW(hit.instanceID, hit.primitiveIndex, positions);
        gScene.getVertexTexCoords(hit.instanceID, hit.primitiveIndex, texCoords);
        float coneWidthAtHitPoint = hitT * tan(pixelAngle);
        computeAnisotropicEllipseAxes(v.posW, v.faceNormalW, rayDir, coneWidthAtHitPoint, positions, texCoords, v.texC, ddx, ddy);
    }

    float3 computeDdxPosW(float3 posW, float3 normW, float2 invFrameDim)
    {
        float3 projRight = normalize(cross(normW, cross(normW, gScene.camera.data.cameraU)));
        float distanceToHit = length(posW - gScene.camera.data.posW);
        float2 ddNdc = float2(2.f, -2.f) * invFrameDim;
        float distRight = distanceToHit * ddNdc.x / dot(normalize(gScene.camera.data.cameraU), projRight);
        return distRight * projRight;
    }

    float3 computeDdyPosW(float3 posW, float3 normW, float2 invFrameDim)
    {
        float3 projUp = normalize(cross(normW, cross(normW, gScene.camera.data.cameraV)));
        float distanceToHit = length(posW - gScene.camera.data.posW);
        float2 ddNdc = float2(2.f, -2.f) * invFrameDim;
        float distUp = distanceToHit * ddNdc.y / dot(normalize(gScene.camera.data.cameraV), projUp);
        return distUp * projUp;
    }

    Ray generateRay(uint2 pixel)
    {
        if (kComputeDepthOfField)
        {
            SampleGenerator sg = SampleGenerator(pixel, frameCount);
            return gScene.camera.computeRayThinlens(pixel, frameDim, sampleNext2D(sg));
        }
        else
        {
            return gScene.camera.computeRayPinhole(pixel, frameDim);
        }
    }

    bool evalModifyNormal(const HitType hitType)
    {
        return true;
    }

    /**
     * [ShadowCount] 주어진 world-space 위치에서 차폐된 광원의 수를 계산한다.
     * inline ray tracing (RayQuery) 을 사용하여 각 analytic light 에 대해
     * 그림자 레이를 추적한다.
     */
    uint computeShadowCount(float3 worldPos, float3 normalW)
    {
        uint shadowCount = 0;
        uint lightCount = gScene.getLightCount();

        float3 origin = worldPos + normalW * kShadowRayEpsilon;

        for (uint i = 0; i < lightCount; i++)
        {
            LightData light = gScene.getLight(i);

            float3 toLight;
            float distance;

            if (light.type == LightType::Directional)
            {
                toLight  = -light.dirW;
                distance = kEnvMapDepth;
            }
            else
            {
                toLight  = light.posW - origin;
                distance = length(toLight);
                if (distance < 1e-7f) continue;
                toLight /= distance;
            }

            RayDesc shadowRay;
            shadowRay.Origin    = origin;
            shadowRay.Direction = toLight;
            shadowRay.TMin      = 0.0f;
            shadowRay.TMax      = distance;

            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rayQuery;
            rayQuery.TraceRayInline(gScene.rtAccel, 0, 0xff, shadowRay);
            rayQuery.Proceed();

            if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT ||
                rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT)
            {
                shadowCount++;
            }
        }

        return shadowCount;
    }

    // [Luminance] BT.601 휘도 계산 헬퍼.
    float computeLuminance(float3 rgb)
    {
        return dot(rgb, float3(0.299f, 0.587f, 0.114f));
    }

    // [CenterDist] 픽셀 좌표를 UV [0,1]^2 로 변환한 뒤 화면 중심(0.5,0.5)까지 거리 계산.
    // invFrameDim 은 GBufferRT 구조체의 멤버를 사용한다.
    float computeCenterDist(uint2 pixel)
    {
        float2 uv = (float2(pixel) + 0.5f) * invFrameDim; // [0,1]^2
        return length(uv - float2(0.5f, 0.5f));
    }

    /**
     * [Metalness] ShadingData 로부터 metalness 원본값 [0,1] 을 추출한다.
     *
     * Falcor 8.0 의 BSDFProperties 는 metalness 를 직접 노출하지 않는다.
     * StandardMaterial 은 specular 텍스처를 ORM(Occlusion-Roughness-Metalness) 레이아웃으로
     * 사용하며, metalness 는 B 채널에 해당한다.
     *
     * 접근 경로:
     *   sd.mtl 은 IMaterialData 인터페이스를 구현한다.
     *   gScene.materials.evalTexture() 또는 evaluateAttributeAt() 은 재질 타입에 따라
     *   구체적인 구현으로 분기되므로, StandardMaterial 전용 메서드를 직접 쓸 수 없다.
     *
     * 안전한 대안:
     *   - bsdfProperties.specularReflectance 는 이미 F0(fresnel 반사율)로 변환된 값이다.
     *     비금속의 F0 ≈ 0.04, 금속의 F0 = baseColor 에 가까우므로
     *     luminance(F0) 로 metalness 를 근사하면 충분히 유효한 추정치가 된다.
     *   - 정확한 원본값이 필요하면 StandardMaterial 의 specular 텍스처 B 채널을
     *     sd.mtl.getMaterialHeader() + sd.texC 로 직접 샘플링해야 하지만
     *     이는 타입별 캐스팅 없이는 접근 불가능하다.
     *
     * 따라서 본 구현은 F0 luminance 를 기반으로 한 metalness 근사값을 출력한다.
     * F0 luminance ≈ 0.04 → metalness ≈ 0 (dielectric)
     * F0 luminance ≈ 0.5~1.0 → metalness ≈ 1 (conductor)
     *
     * 선형 역매핑:  metalness_approx = saturate((lum(F0) - 0.04) / (1.0 - 0.04))
     */
    float computeMetalness(const BSDFProperties bsdfProperties)
    {
        // specularReflectance = F0 (Fresnel reflectance at normal incidence).
        // 비금속 F0 ≈ float3(0.04) → luminance ≈ 0.04
        // 금속 F0 = baseColor → luminance 는 재질 색상에 따라 다양하지만 일반적으로 >> 0.04
        float f0Lum = dot(bsdfProperties.specularReflectance, float3(0.299f, 0.587f, 0.114f));
        const float kDielectricF0 = 0.04f;
        return saturate((f0Lum - kDielectricF0) / (1.0f - kDielectricF0));
    }

    void writeHit(uint2 pixel, float3 rayOrigin, float3 rayDir, const HitInfo hit, float hitT)
    {
        uint materialID = 0;
        VertexData v = {};
        float3 prevPosW = {};
        float4 texGrads = {};
        float2 motionVector = {};
        float4 motionVectorW = {};
        float disocclusion = 0.f;
        ITextureSampler lod = ExplicitLodTextureSampler(0.f);

        // [ObjectID] hit から instanceID を取得するための変数
        // GeometryInstanceID.index が mesh instance 固有番号になる
        uint objectID = 0;

        if (hit.getType() == HitType::Triangle)
        {
            const TriangleHit triangleHit = hit.getTriangleHit();

            materialID = gScene.getMaterialID(triangleHit.instanceID);
            // [ObjectID] TriangleHit の instanceID.index を object ID として使用する.
            // これは material ID とは独立しており、同じ material を持つ別の mesh instance でも異なる値になる.
            objectID   = triangleHit.instanceID.index;
            v = gScene.getVertexData(triangleHit);
            prevPosW = gScene.getPrevPosW(triangleHit);

            if (kLODMode == TexLODMode::RayCones)
            {
                float2 ddx, ddy;
                computeAnisotropicAxesRayCones(triangleHit, v, rayDir, hitT, screenSpacePixelSpreadAngle, ddx, ddy);
                lod = ExplicitGradientTextureSampler(ddx, ddy);
                texGrads = float4(ddx, ddy);
            }
            else if (kLODMode == TexLODMode::RayDiffs)
            {
                float2 ddx, ddy;
                computeRayDifferentials(triangleHit, rayDir, hitT, gScene.camera, invFrameDim, ddx, ddy);
                lod = ExplicitGradientTextureSampler(ddx, ddy);
                texGrads = float4(ddx, ddy);
            }
        }
        else if (hit.getType() == HitType::DisplacedTriangle)
        {
            const DisplacedTriangleHit displacedTriangleHit = hit.getDisplacedTriangleHit();
            materialID = gScene.getMaterialID(displacedTriangleHit.instanceID);
            objectID   = displacedTriangleHit.instanceID.index;
            v = gScene.getVertexData(displacedTriangleHit, -rayDir);
            prevPosW = gScene.getPrevPosW(displacedTriangleHit);
        }
        else if (hit.getType() == HitType::Curve)
        {
            const CurveHit curveHit = hit.getCurveHit();
            materialID = gScene.getMaterialID(curveHit.instanceID);
            objectID   = curveHit.instanceID.index;
            v = gScene.getVertexDataFromCurve(curveHit);
            prevPosW = gScene.getPrevPosWFromCurve(curveHit);
        }
        else if (hit.getType() == HitType::SDFGrid)
        {
            const SDFGridHit sdfGridHit = hit.getSDFGridHit();
            materialID = gScene.getMaterialID(sdfGridHit.instanceID);
            objectID   = sdfGridHit.instanceID.index;
            v = gScene.getVertexDataFromSDFGrid(sdfGridHit, rayOrigin, rayDir);
            prevPosW = gScene.getPrevPosWFromSDFGrid(sdfGridHit, v.posW);
        }
        else
        {
            return;
        }

        // Prepare shading data.
        ShadingData sd = gScene.materials.prepareShadingData(v, materialID, -rayDir);

        bool modifyNormal = evalModifyNormal(hit.getType());
        uint hints = !modifyNormal ? (uint)MaterialInstanceHints::DisableNormalMapping : 0;
        if (kAdjustShadingNormals && (hit.getType() == HitType::Triangle || hit.getType() == HitType::DisplacedTriangle))
        {
            hints |= (uint)MaterialInstanceHints::AdjustShadingNormal;
        }

        if (hit.getType() != HitType::None && hit.getType() != HitType::Volume)
        {
            float2 pixelPos = pixel + float2(0.5f, 0.5f);
            float4 prevPosH = mul(gScene.camera.data.prevViewProjMatNoJitter, float4(prevPosW, 1.f));

            motionVector = calcMotionVector(pixelPos, prevPosH, frameDim) + float2(gScene.camera.data.jitterX, -gScene.camera.data.jitterY);
            motionVectorW = float4(prevPosW - sd.posW, 0.f);

            float4 curPosH = mul(gScene.camera.data.viewProjMatNoJitter, float4(sd.posW, 1.f));
            disocclusion = prevPosH.w - curPosH.w;
        }

        let mi = gScene.materials.getMaterialInstance(sd, lod, hints);
        let bsdfProperties = mi.getProperties(sd);

        const GBufferData gbuf = prepareGBufferData(sd, v, mi, bsdfProperties);

        // GBuffer channels
        if (is_valid(gPosW))
            gPosW[pixel] = gbuf.posW;
        if (is_valid(gNormW))
            gNormW[pixel] = gbuf.normW;
        if (is_valid(gTangentW))
            gTangentW[pixel] = gbuf.tangentW;
        if (is_valid(gFaceNormalW))
            gFaceNormalW[pixel] = gbuf.faceNormalW;
        if (is_valid(gTexC))
            gTexC[pixel] = gbuf.texC;
        if (is_valid(gTexGrads))
            gTexGrads[pixel] = texGrads;
        if (is_valid(gMotionVector))
            gMotionVector[pixel] = motionVector;
        if (is_valid(gMaterialData))
            gMaterialData[pixel] = gbuf.mtlData;

        // GBufferRT channels
        if (is_valid(gMotionVectorW))
            gMotionVectorW[pixel] = motionVectorW;
        if (is_valid(gGuideNormalW))
            gGuideNormalW[pixel] = gbuf.guideNormalW;
        if (is_valid(gDiffOpacity))
            gDiffOpacity[pixel] = gbuf.diffuseOpacity;
        if (is_valid(gSpecRough))
            gSpecRough[pixel] = gbuf.specRough;
        if (is_valid(gEmissive))
            gEmissive[pixel] = gbuf.emissive;
        if (is_valid(gDisocclusion))
            gDisocclusion[pixel] = disocclusion;
        if (is_valid(gMask))
            gMask[pixel] = 1.0f;

        // [ShadowCount] face normal 방향으로 origin을 offset하여 self-intersection 방지
        if (is_valid(gShadowCount))
        {
            gShadowCount[pixel] = computeShadowCount(sd.posW, sd.faceN);
        }

        // [ObjectID] material ID와 독립적인 mesh instance 고유 번호를 기록한다.
        // 같은 material을 공유하는 다른 instance도 서로 다른 값을 갖는다.
        if (is_valid(gObjectID))
        {
            gObjectID[pixel] = objectID;
        }

        // [Luminance] diffuse albedo로부터 BT.601 휘도를 계산하여 기록한다.
        // bsdfProperties 를 이미 계산했으므로 추가 material fetch 없이 사용한다.
        if (is_valid(gLuminance))
        {
            gLuminance[pixel] = computeLuminance(gbuf.diffuseOpacity.rgb);
        }

        // [CenterDist] 화면 중심 (UV 0.5, 0.5) 으로부터의 normalized 거리를 기록한다.
        // 중앙 픽셀 = 0.0, 모서리 픽셀 ≈ 0.707.
        if (is_valid(gCenterDist))
        {
            gCenterDist[pixel] = computeCenterDist(pixel);
        }

        if (is_valid(gLinearZ))
        {
            float4 curPosH = mul(gScene.camera.data.viewProjMatNoJitter, float4(sd.posW, 1.f));
            float curLinearZ = curPosH.w;

            float3 ddxPosW = computeDdxPosW(sd.posW, sd.faceN, invFrameDim);
            float3 ddyPosW = computeDdyPosW(sd.posW, sd.faceN, invFrameDim);
            float4 curPosH_dx = mul(gScene.camera.data.viewProjMatNoJitter, float4(sd.posW + ddxPosW, 1.f));
            float4 curPosH_dy = mul(gScene.camera.data.viewProjMatNoJitter, float4(sd.posW + ddyPosW, 1.f));
            float ddxLinearZ = abs(curPosH_dx.w - curLinearZ);
            float ddyLinearZ = abs(curPosH_dy.w - curLinearZ);
            float dLinearZ = max(ddxLinearZ, ddyLinearZ);
            gLinearZ[pixel] = float2(curLinearZ, dLinearZ);
        }

        if (is_valid(gDepth))
        {
            float4 curPosH = mul(gScene.camera.data.viewProjMatNoJitter, float4(sd.posW, 1.f));
            gDepth[pixel] = curPosH.z / curPosH.w;
        }

        if (is_valid(gNormalWRoughnessMaterialID))
        {
            float3 normal = bsdfProperties.guideNormal;
            if (sd.mtl.getMaterialType() == MaterialType::Hair)
                normal = sd.frame.T;

            float2 octNormal = ndir_to_oct_unorm(normal);
            float roughness = bsdfProperties.roughness;
            float materialID_f = 0.f;
            gNormalWRoughnessMaterialID[pixel] = float4(octNormal, roughness, materialID_f);
        }

        if (is_valid(gVBuffer))
        {
            gVBuffer[pixel] = hit.pack();
        }
    }

    float2 computeMotionVectorBackground(const uint2 pixel, const float3 rayOrigin, const float3 rayDir)
    {
        float3 worldPos = rayOrigin + normalize(rayDir) * kEnvMapDepth;
        float2 pixelPos = pixel + float2(0.5f, 0.5f);
        float4 prevPosH = mul(gScene.camera.data.prevViewProjMatNoJitter, float4(worldPos, 1.f));
        return calcMotionVector(pixelPos, prevPosH, frameDim) + float2(gScene.camera.data.jitterX, -gScene.camera.data.jitterY);
    }

    void writeMiss(const uint2 pixel, const float3 rayOrigin, const float3 rayDir)
    {
        const float2 mvec = computeMotionVectorBackground(pixel, rayOrigin, rayDir);
        const float2 octNormal = ndir_to_oct_unorm(float3(0.2215f, 0.2215f, 0.2215f));
        const float roughness = 0.f;
        const float materialID = 0.f;

        // GBuffer channels
        if (is_valid(gPosW))        gPosW[pixel]        = {};
        if (is_valid(gNormW))       gNormW[pixel]       = {};
        if (is_valid(gTangentW))    gTangentW[pixel]    = {};
        if (is_valid(gFaceNormalW)) gFaceNormalW[pixel] = {};
        if (is_valid(gTexC))        gTexC[pixel]        = {};
        if (is_valid(gTexGrads))    gTexGrads[pixel]    = {};
        if (is_valid(gMotionVector)) gMotionVector[pixel] = mvec;
        if (is_valid(gMaterialData)) gMaterialData[pixel] = {};

        // GBufferRT channels
        if (is_valid(gVBuffer))     gVBuffer[pixel]     = {};
        if (is_valid(gDepth))       gDepth[pixel]       = 1.f;
        if (is_valid(gLinearZ))     gLinearZ[pixel]     = float2(kEnvMapDepth, 0.f);
        if (is_valid(gMotionVectorW)) gMotionVectorW[pixel] = {};
        if (is_valid(gNormalWRoughnessMaterialID))
            gNormalWRoughnessMaterialID[pixel] = float4(octNormal, roughness, materialID);
        if (is_valid(gGuideNormalW))  gGuideNormalW[pixel]  = {};
        if (is_valid(gDiffOpacity))   gDiffOpacity[pixel]   = {};
        if (is_valid(gSpecRough))     gSpecRough[pixel]     = {};
        if (is_valid(gEmissive))      gEmissive[pixel]      = {};
        if (is_valid(gDisocclusion))  gDisocclusion[pixel]  = 0.f;
        if (is_valid(gMask))          gMask[pixel]          = 0.0f;

        // [ShadowCount] miss — 표면이 없으므로 그림자도 없다.
        if (is_valid(gShadowCount))   gShadowCount[pixel]   = 0u;

        // [ObjectID] miss — 어떤 object에도 해당하지 않으므로 UINT_MAX 로 표기.
        // ImportancePass 에서 배경 픽셀을 별도로 처리할 때 활용할 수 있다.
        if (is_valid(gObjectID))      gObjectID[pixel]      = 0xFFFFFFFFu;

        // [Luminance] miss — 배경 픽셀은 luminance = 0 으로 기록.
        if (is_valid(gLuminance))     gLuminance[pixel]     = 0.f;

        // [CenterDist] miss 도 동일하게 화면 중심 거리를 기록한다.
        // 배경 픽셀에서도 거리 정보가 의미 있을 수 있다 (예: 가장자리 우선 샘플링).
        if (is_valid(gCenterDist))    gCenterDist[pixel]    = computeCenterDist(pixel);
    }

    void writeAux(uint2 pixel, const Ray ray)
    {
        if (is_valid(gViewW))
            gViewW[pixel] = float4(-ray.dir, 0.f);
    }

    void beginTime(inout GpuTimer timer)
    {
        if (is_valid(gTime))
            timer.start();
    }

    void endTime(uint2 pixel, inout GpuTimer timer)
    {
        if (is_valid(gTime))
            gTime[pixel] = timer.getElapsed();
    }
};
