/***************************************************************************
 # SampleCountPass.cs.slang
 #
 # Modified: reads importance texture and outputs 1, 2, or 4 samples.
 **************************************************************************/

cbuffer CB
{
    uint2 frameDim;
    float thresholdLow;     // importance < this -> 1 spp
    float thresholdHigh;    // importance >= this -> 4 spp, else 2 spp
};

Texture2D<float> gImportance;
RWTexture2D<uint> gOutput;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= frameDim))
        return;

    float imp = gImportance[pixel];

    uint count;
    if (imp < thresholdLow)
        count = 1u;
    else if (imp < thresholdHigh)
        count = 2u;
    else
        count = 4u;

    gOutput[pixel] = count;
}
