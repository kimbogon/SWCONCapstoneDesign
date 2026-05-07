/***************************************************************************
 # ImportancePass.cs.slang
 #
 # GBufferRT 의 모든 feature 를 입력으로 받아 픽셀별 중요도를 계산한다.
 #
 # importance = saturate(0.5 * lumVar + 0.3 * normalGrad + 0.2 * depthGrad)
 # (기존 계산 로직 유지 — 새 feature 는 접근 가능하나 아직 가중치에 반영하지 않음)
 #
 # [새로 접근 가능한 feature]
 # - gFaceNormalW   : geometry normal (HAS_FACE_NORMAL)
 # - gSpecRough     : specular + roughness  (HAS_SPEC_ROUGH)
 #                    glossiness = 1 - specRough.a
 # - gMotionVectorW : world-space motion vector (HAS_MOTION_VECTOR_W)
 # - gObjectID      : mesh instance index (HAS_OBJECT_ID)
 # - gLuminanceTex  : pre-computed luminance (HAS_LUMINANCE)
 # - gCenterDist    : screen-center distance (HAS_CENTER_DIST)
 **************************************************************************/

// ---- guard defines (ImportancePass.cpp 에서 주입; 컴파일 단독 실행 시 fallback) ----
#ifndef HAS_DIFFUSE_OPACITY
#define HAS_DIFFUSE_OPACITY 0
#endif
#ifndef HAS_GUIDE_NORMAL
#define HAS_GUIDE_NORMAL 0
#endif
#ifndef HAS_FACE_NORMAL
#define HAS_FACE_NORMAL 0
#endif
#ifndef HAS_SPEC_ROUGH
#define HAS_SPEC_ROUGH 0
#endif
#ifndef HAS_MOTION_VECTOR_W
#define HAS_MOTION_VECTOR_W 0
#endif
#ifndef HAS_LINEAR_Z
#define HAS_LINEAR_Z 0
#endif
#ifndef HAS_SHADOW_COUNT
#define HAS_SHADOW_COUNT 0
#endif
#ifndef HAS_OBJ_ID
#define HAS_OBJ_ID 0
#endif
#ifndef HAS_LUMINANCE
#define HAS_LUMINANCE 0
#endif
#ifndef HAS_CENTER_DIST
#define HAS_CENTER_DIST 0
#endif
#ifndef HAS_IMPORTANCE_VIS
#define HAS_IMPORTANCE_VIS 0
#endif

// ---- constant buffer ----
cbuffer CB
{
    uint2 frameDim;
};

// ---- 입력 텍스처 ----
Texture2D<float4> gDiffuseOpacity;   // diffuse albedo + opacity  (RGBA32Float)
Texture2D<float4> gGuideNormalW;     // detail(shading) normal WS (RGBA32Float)
Texture2D<float4> gFaceNormalW;      // geometry normal WS        (RGBA32Float)  [신규]
Texture2D<float4> gSpecRough;        // specular(RGB) + rough(A)  (RGBA32Float)  [신규]
Texture2D<float4> gMotionVectorW;    // world-space motion vector (RGBA16Float)  [신규]
Texture2D<float2> gLinearZ;          // linear depth + slope      (RG32Float)
Texture2D<uint>   gShadowCount;      // occluded light count      (R32Uint)
Texture2D<uint>   gObjectID;         // mesh instance index       (R32Uint)      [신규]
Texture2D<float>  gLuminanceTex;     // pre-computed luminance    (R32Float)     [신규]
Texture2D<float>  gCenterDist;       // screen-center distance    (R32Float)     [신규]

// ---- 출력 텍스처 ----
RWTexture2D<float>  gImportance;     // per-pixel importance [0,1] (R32Float)
RWTexture2D<float4> gImportanceVis;  // grayscale visualization    (RGBA8Unorm)

// ============================================================
// 샘플 헬퍼 함수
// ============================================================

// BT.601 luminance (diffuse albedo 에서 계산; gLuminanceTex 미연결 시 fallback)
float luminance(float3 c)
{
    return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float sampleLuminance(int2 p)
{
    p = clamp(p, int2(0), int2(frameDim) - 1);
#if HAS_LUMINANCE
    // GBufferRT 에서 미리 계산된 luminance 를 우선 사용한다.
    return gLuminanceTex[p];
#elif HAS_DIFFUSE_OPACITY
    return luminance(gDiffuseOpacity[p].rgb);
#else
    return 0.f;
#endif
}

float3 sampleGuideNormal(int2 p)
{
#if HAS_GUIDE_NORMAL
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gGuideNormalW[p].xyz;
#else
    return float3(0.f, 0.f, 1.f);
#endif
}

// [FaceNormal] geometry normal 샘플 헬퍼.
float3 sampleFaceNormal(int2 p)
{
#if HAS_FACE_NORMAL
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gFaceNormalW[p].xyz;
#else
    return float3(0.f, 0.f, 1.f);
#endif
}

float sampleDepth(int2 p)
{
#if HAS_LINEAR_Z
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gLinearZ[p].x;
#else
    return 0.f;
#endif
}

// [ShadowCount] shadow count 샘플 헬퍼.
uint sampleShadowCount(int2 p)
{
#if HAS_SHADOW_COUNT
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gShadowCount[p];
#else
    return 0u;
#endif
}

// [SpecRough] roughness 샘플 헬퍼. glossiness = 1 - roughness.
float sampleRoughness(int2 p)
{
#if HAS_SPEC_ROUGH
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gSpecRough[p].a;
#else
    return 0.f;
#endif
}

// [MotionVectorW] world-space motion vector 의 크기를 반환한다.
float sampleMotionMagnitude(int2 p)
{
#if HAS_MOTION_VECTOR_W
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return length(gMotionVectorW[p].xyz);
#else
    return 0.f;
#endif
}

// [ObjectID] object ID 샘플 헬퍼. 배경(miss)은 0xFFFFFFFF.
uint sampleObjectID(int2 p)
{
#if HAS_OBJ_ID
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gObjectID[p];
#else
    return 0u;
#endif
}

// [CenterDist] 화면 중심 거리 샘플 헬퍼. [0, ~0.707].
float sampleCenterDist(int2 p)
{
#if HAS_CENTER_DIST
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gCenterDist[p];
#else
    return 0.f;
#endif
}

// ============================================================
// 메인 커널
// ============================================================
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= frameDim))
        return;

    int2 p = int2(pixel);

    // ---- luminance variance (3×3 neighborhood) ----
    // HAS_LUMINANCE 또는 HAS_DIFFUSE_OPACITY 중 하나라도 있으면 계산한다.
    float lumVar = 0.f;
#if HAS_LUMINANCE || HAS_DIFFUSE_OPACITY
    {
        float sum   = 0.f;
        float sumSq = 0.f;
        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                float l = sampleLuminance(p + int2(dx, dy));
                sum   += l;
                sumSq += l * l;
            }
        }
        float mean     = sum / 9.f;
        float variance = max(0.f, sumSq / 9.f - mean * mean);
        lumVar = saturate(sqrt(variance) * 4.f);
    }
#endif

    // ---- normal gradient (4-neighbor, guide normal 우선) ----
    float normalGrad = 0.f;
#if HAS_GUIDE_NORMAL
    {
        float3 nc = sampleGuideNormal(p);
        float d   = 0.f;
        d += length(nc - sampleGuideNormal(p + int2(-1,  0)));
        d += length(nc - sampleGuideNormal(p + int2( 1,  0)));
        d += length(nc - sampleGuideNormal(p + int2( 0, -1)));
        d += length(nc - sampleGuideNormal(p + int2( 0,  1)));
        normalGrad = saturate(d / 4.f);
    }
#endif

    // ---- depth gradient (4-neighbor, relative) ----
    float depthGrad = 0.f;
#if HAS_LINEAR_Z
    {
        float zc = sampleDepth(p);
        if (zc > 0.f)
        {
            float d = 0.f;
            d += abs(zc - sampleDepth(p + int2(-1,  0)));
            d += abs(zc - sampleDepth(p + int2( 1,  0)));
            d += abs(zc - sampleDepth(p + int2( 0, -1)));
            d += abs(zc - sampleDepth(p + int2( 0,  1)));
            depthGrad = saturate((d / 4.f) / zc * 10.f);
        }
    }
#endif

    // ============================================================
    // 아래 변수들은 현재 importance 계산 식에는 포함되지 않지만
    // 모두 접근 가능하다. 가중치 조정 시 바로 활용할 수 있다.
    // ============================================================

    // [FaceNormal] geometry normal — detail normal 과의 차이로 범프 강도 파악 가능
    // float3 faceN = sampleFaceNormal(p);

    // [SpecRough] roughness / glossiness — 반사성 소재 강조 등에 사용 가능
    // float roughness  = sampleRoughness(p);
    // float glossiness = 1.f - roughness;

    // [MotionVectorW] motion magnitude — 빠르게 움직이는 영역에 샘플 집중 가능
    // float motion = sampleMotionMagnitude(p);

    // [ShadowCount] 그림자 영역 강조 가능
    // uint sc = sampleShadowCount(p);

    // [ObjectID] 오브젝트 경계 검출 (인접 픽셀과 ID 비교)
    // uint oid = sampleObjectID(p);

    // [CenterDist] 화면 주변부/중심부 차별화 샘플링 가능
    // float cd = sampleCenterDist(p);

    // ---- importance 최종 계산 (기존 수식 유지) ----
    float importance = saturate(0.5f * lumVar + 0.3f * normalGrad + 0.2f * depthGrad);

    // ---- 출력 ----
    gImportance[pixel] = importance;

#if HAS_IMPORTANCE_VIS
    // grayscale 시각화: importance 가 클수록 밝게(흰색)
    gImportanceVis[pixel] = float4(importance, importance, importance, 1.f);
#endif
}
