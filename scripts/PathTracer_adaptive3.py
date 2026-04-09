from falcor import *

def render_graph_AdaptivePathTracer():
    g = RenderGraph("AdaptivePathTracer")

    # --- Passes -----------------------------------------------------------
    GBufferRT = createPass("GBufferRT")
    g.addPass(GBufferRT, "GBufferRT")

    ImportancePass = createPass("ImportancePass")
    g.addPass(ImportancePass, "ImportancePass")

    SampleCountPass = createPass("SampleCountPass")
    g.addPass(SampleCountPass, "SampleCountPass")

    PathTracer = createPass("PathTracer", {'samplesPerPixel': 1})
    g.addPass(PathTracer, "PathTracer")

    AccumulatePass = createPass("AccumulatePass")
    g.addPass(AccumulatePass, "AccumulatePass")

    ToneMapper = createPass("ToneMapper")
    g.addPass(ToneMapper, "ToneMapper")

    # --- Edges ------------------------------------------------------------
    # 1. Primary Visibility
    g.addEdge("GBufferRT.vbuffer", "PathTracer.vbuffer")
    
    # 2. Importance Pipeline (GBuffer -> Importance -> SampleCount)
    # GBufferRT의 표준 채널명을 ImportancePass 입력에 연결
    g.addEdge("GBufferRT.guideNormalW", "ImportancePass.guideNormalW")
    g.addEdge("GBufferRT.diffuseOpacity", "ImportancePass.diffuseOpacity")
    g.addEdge("GBufferRT.linearZ", "ImportancePass.linearZ")
    
    g.addEdge("ImportancePass.importance", "SampleCountPass.importance")
    g.addEdge("SampleCountPass.sampleCount", "PathTracer.sampleCount")

    # 3. PathTracer Connections
    g.addEdge("GBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("GBufferRT.mvecW",  "PathTracer.mvec")

    # 4. Post-processing
    g.addEdge("PathTracer.color",      "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")

    # --- Output -----------------------------------------------------------
    g.markOutput("ToneMapper.dst")

    return g

AdaptiveGraph = render_graph_AdaptivePathTracer()
try: m.addGraph(AdaptiveGraph)
except NameError: None