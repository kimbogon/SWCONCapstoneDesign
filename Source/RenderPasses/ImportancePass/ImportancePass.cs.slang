/***************************************************************************
 # ImportancePass.cs.slang
 #
 # Computes per-pixel importance from GBufferRT channels.
 # importance = saturate(0.5 * lumVar + 0.3 * normalGrad + 0.2 * depthGrad)
 #
 # [디버그 시각화 추가]
 # gImportanceVis: importance float 값을 grayscale RGBA8Unorm 텍스처로 기록한다.
 # HAS_IMPORTANCE_VIS 가 1일 때만 쓰기가 활성화된다.
 **************************************************************************/
#ifndef HAS_DIFFUSE_OPACITY
#define HAS_DIFFUSE_OPACITY 0
#endif
#ifndef HAS_GUIDE_NORMAL
#define HAS_GUIDE_NORMAL 0
#endif
#ifndef HAS_LINEAR_Z
#define HAS_LINEAR_Z 0
#endif
// [ShadowCount] Guard define for shadow count input availability.
#ifndef HAS_SHADOW_COUNT
#define HAS_SHADOW_COUNT 0
#endif
// [디버그 시각화] 시각화 출력 활성화 여부 guard define.
#ifndef HAS_IMPORTANCE_VIS
#define HAS_IMPORTANCE_VIS 0
#endif

cbuffer CB
{
    uint2 frameDim;
};

// Inputs from GBufferRT (bound even when nullptr; guarded by defines).
Texture2D<float4> gDiffuseOpacity;  // diffuseOpacity channel (RGBA32Float)
Texture2D<float4> gGuideNormalW;    // guideNormalW channel   (RGBA32Float)
Texture2D<float2> gLinearZ;         // linearZ channel        (RG32Float, .x = linearZ)
// [ShadowCount] Shadow count input from GBufferRT (R32Uint).
Texture2D<uint>   gShadowCount;

// 실제 importance 데이터 출력 (R32Float).
RWTexture2D<float>  gImportance;
// [디버그 시각화] grayscale 시각화 출력 (RGBA8Unorm).
// HAS_IMPORTANCE_VIS == 0 일 때는 바인딩만 되고 실제로 쓰지 않는다.
RWTexture2D<float4> gImportanceVis;

// --- helpers ---
float luminance(float3 c)
{
    return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float sampleLuminance(int2 p)
{
#if HAS_DIFFUSE_OPACITY
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return luminance(gDiffuseOpacity[p].rgb);
#else
    return 0.f;
#endif
}

float3 sampleNormal(int2 p)
{
#if HAS_GUIDE_NORMAL
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gGuideNormalW[p].xyz;
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

// [ShadowCount] Helper to sample shadow count at a given pixel.
uint sampleShadowCount(int2 p)
{
#if HAS_SHADOW_COUNT
    p = clamp(p, int2(0), int2(frameDim) - 1);
    return gShadowCount[p];
#else
    return 0u;
#endif
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= frameDim))
        return;

    int2 p = int2(pixel);

    // ---- luminance variance (3x3 neighbourhood) ----
    float lumVar = 0.f;
#if HAS_DIFFUSE_OPACITY
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
        // Normalize: sqrt(variance) mapped through a soft knee; typical albedo variance is small.
        lumVar = saturate(sqrt(variance) * 4.f);
    }
#endif

    // ---- normal gradient (4-neighbour) ----
    float normalGrad = 0.f;
#if HAS_GUIDE_NORMAL
    {
        float3 nc = sampleNormal(p);
        float d = 0.f;
        d += length(nc - sampleNormal(p + int2(-1, 0)));
        d += length(nc - sampleNormal(p + int2( 1, 0)));
        d += length(nc - sampleNormal(p + int2( 0,-1)));
        d += length(nc - sampleNormal(p + int2( 0, 1)));
        // Max possible per-neighbour diff is 2.0 (opposite normals), average max = 2.0.
        normalGrad = saturate(d / 4.f);
    }
#endif

    // ---- depth gradient (4-neighbour, relative) ----
    float depthGrad = 0.f;
#if HAS_LINEAR_Z
    {
        float zc = sampleDepth(p);
        if (zc > 0.f)
        {
            float d = 0.f;
            d += abs(zc - sampleDepth(p + int2(-1, 0)));
            d += abs(zc - sampleDepth(p + int2( 1, 0)));
            d += abs(zc - sampleDepth(p + int2( 0,-1)));
            d += abs(zc - sampleDepth(p + int2( 0, 1)));
            // Normalize by centre depth so we get relative depth discontinuity.
            depthGrad = saturate((d / 4.f) / zc * 10.f);
        }
    }
#endif

    // [ShadowCount] Read shadow count for this pixel (available for future use in importance computation).
    // uint sc = sampleShadowCount(p);

    // importance 최종 계산.
    float importance = saturate(0.5f * lumVar + 0.3f * normalGrad + 0.2f * depthGrad);

    // 실제 importance 값 기록 (R32Float).
    gImportance[pixel] = importance;

    // [디버그 시각화] importance 값을 grayscale RGBA8Unorm 텍스처에 기록한다.
    // importance가 클수록 밝게(흰색), 작을수록 어둡게(검정) 표시된다.
#if HAS_IMPORTANCE_VIS
    gImportanceVis[pixel] = float4(importance, importance, importance, 1.f);
#endif
}
