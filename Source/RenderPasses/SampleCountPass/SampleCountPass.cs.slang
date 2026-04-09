/***************************************************************************
 # SampleCountPass.cs.slang
 #
 # Compute shader that writes a per-pixel R8Uint sample count.
 #
 # Heuristic:
 #   Pixels whose distance from the screen centre is within
 #   (centreRadius * min(width, height)) receive highSamples.
 #   All other pixels receive lowSamples.
 **************************************************************************/

// ---------------------------------------------------------------------------
// Constant buffer
// ---------------------------------------------------------------------------
cbuffer CB
{
    uint2   frameDim;       ///< Frame width and height in pixels.
    uint    highSamples;    ///< Sample count for centre disc pixels.
    uint    lowSamples;     ///< Sample count for peripheral pixels.
    float   centreRadius;   ///< Centre disc radius as fraction of min(w,h).
};

// ---------------------------------------------------------------------------
// Output UAV  (R8Uint)
// ---------------------------------------------------------------------------
RWTexture2D<uint> gOutput;

// ---------------------------------------------------------------------------
// Main kernel – one thread per pixel
// ---------------------------------------------------------------------------
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;

    // Discard threads outside the frame.
    if (any(pixel >= frameDim))
        return;

    // Compute the distance from the pixel to the screen centre.
    float2 centre   = float2(frameDim) * 0.5f;
    float2 offset   = float2(pixel) + 0.5f - centre; // use pixel centre
    float  dist     = length(offset);

    // Disc radius in pixels.
    float  minDim   = (float)min(frameDim.x, frameDim.y);
    float  radiusPx = centreRadius * minDim;

    // Choose sample count.
    uint count = (dist <= radiusPx) ? highSamples : lowSamples;

    // Clamp to [1, 255] to satisfy R8Uint and PathTracer's expectations.
    count = clamp(count, 1u, 255u);

    gOutput[pixel] = count;
}
