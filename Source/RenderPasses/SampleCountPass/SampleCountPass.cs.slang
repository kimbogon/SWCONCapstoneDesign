/***************************************************************************
 # SampleCountPass.cs.slang
 #
 # Modified: reads importance texture and outputs 1, 2, or 4 samples.
 #
 # [디버그 시각화 추가]
 # gSampleCountVis: sample count (1/2/4) 를 정규화된 밝기값으로 RGBA8Unorm에 기록한다.
 #   1 spp → 0.25  (어두운 회색)
 #   2 spp → 0.50  (중간 회색)
 #   4 spp → 1.00  (흰색)
 # HAS_SAMPLE_COUNT_VIS 가 1일 때만 쓰기가 활성화된다.
 **************************************************************************/

// [디버그 시각화] 시각화 출력 활성화 여부 guard define.
#ifndef HAS_SAMPLE_COUNT_VIS
#define HAS_SAMPLE_COUNT_VIS 0
#endif

cbuffer CB
{
    uint2 frameDim;
    float thresholdLow;     // importance < this -> 1 spp
    float thresholdHigh;    // importance >= this -> 4 spp, else 2 spp
};

// importance 입력 (R32Float).
Texture2D<float>    gImportance;
// 실제 sample count 출력 (R8Uint) — PathTracer가 읽는 데이터.
RWTexture2D<uint>   gOutput;
// [디버그 시각화] 정규화된 시각화 출력 (RGBA8Unorm).
// HAS_SAMPLE_COUNT_VIS == 0 일 때는 바인딩만 되고 실제로 쓰지 않는다.
RWTexture2D<float4> gSampleCountVis;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= frameDim))
        return;

    float imp = gImportance[pixel];

    // importance 임계값에 따라 sample count 결정 (1 / 2 / 4 spp).
    uint count;
    if (imp < thresholdLow)
        count = 1u;
    else if (imp < thresholdHigh)
        count = 2u;
    else
        count = 4u;

    // 실제 sample count 기록.
    gOutput[pixel] = count;

    // [디버그 시각화] sample count 를 정규화된 밝기값으로 변환하여 기록한다.
    // 최대 샘플 수(4spp)로 나눠 [0,1] 범위로 정규화한다.
    //   1 spp → 0.25 (어두운 회색: 샘플이 적음)
    //   2 spp → 0.50 (중간 회색)
    //   4 spp → 1.00 (흰색: 샘플이 많음)
#if HAS_SAMPLE_COUNT_VIS
    float normalizedCount = float(count) / 4.f;
    gSampleCountVis[pixel] = float4(normalizedCount, normalizedCount, normalizedCount, 1.f);
#endif
}
