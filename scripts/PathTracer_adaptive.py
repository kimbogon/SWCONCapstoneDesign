from falcor import *

def render_graph_PathTracer():
    g = RenderGraph("PathTracer")

    # --- Passes -----------------------------------------------------------

    # VBufferRT: primary visibility buffer.
    VBufferRT = createPass("VBufferRT", {
        'samplePattern': 'Stratified',
        'sampleCount': 16,
        'useAlphaTest': True
    })
    g.addPass(VBufferRT, "VBufferRT")

    # SampleCountPass: outputs a per-pixel R8Uint sampleCount texture.
    #   highSamples  – samples for pixels inside the centre disc (default 4)
    #   lowSamples   – samples for all other pixels              (default 1)
    #   centreRadius – disc radius as a fraction of min(w,h)     (default 0.25)
    SampleCountPass = createPass("SampleCountPass", {
        'highSamples':  4,
        'lowSamples':   1,
        'centreRadius': 0.25
    })
    g.addPass(SampleCountPass, "SampleCountPass")

    # PathTracer: set samplesPerPixel to 1 (overridden per-pixel by sampleCount
    # input when it is connected, so this value acts as a fallback / max hint).
    PathTracer = createPass("PathTracer", {'samplesPerPixel': 1})
    g.addPass(PathTracer, "PathTracer")

    # Accumulate + tone-map as before.
    AccumulatePass = createPass("AccumulatePass", {
        'enabled': True,
        'precisionMode': 'Single'
    })
    g.addPass(AccumulatePass, "AccumulatePass")

    ToneMapper = createPass("ToneMapper", {
        'autoExposure': False,
        'exposureCompensation': 0.0
    })
    g.addPass(ToneMapper, "ToneMapper")

    # --- Edges ------------------------------------------------------------

    # VBufferRT → SampleCountPass (optional vbuffer input)
    g.addEdge("VBufferRT.vbuffer", "SampleCountPass.vbuffer")

    # VBufferRT → PathTracer (existing connections)
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW",   "PathTracer.viewW")
    g.addEdge("VBufferRT.mvec",    "PathTracer.mvec")

    # *** Key edge: SampleCountPass → PathTracer.sampleCount ***
    # Connecting this edge causes PathTracer to switch from fixed to
    # variable (adaptive) sample count mode automatically (mFixedSampleCount
    # becomes false in PathTracer::beginFrame).
    g.addEdge("SampleCountPass.sampleCount", "PathTracer.sampleCount")

    # PathTracer → AccumulatePass → ToneMapper
    g.addEdge("PathTracer.color",      "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")

    # --- Output -----------------------------------------------------------
    g.markOutput("ToneMapper.dst")

    return g

PathTracer = render_graph_PathTracer()
try: m.addGraph(PathTracer)
except NameError: None
