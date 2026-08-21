from falcor import *
import os   # 캡처 폴더 자동 생성을 위해 추가

import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT_DIR))

from Experiments import config
_cfg = config.PRESETS[config.CURRENT_PRESET]["ref"]

CAMERA_ANIMATION = _cfg["CAMERA_ANIMATION"]

def render_graph_PathTracerBaseline():
    g = RenderGraph("PathTracerBaseline")

    # --- Passes -----------------------------------------------------------
    PathTracer = createPass("PathTracer", {'samplesPerPixel': 16})
    g.addPass(PathTracer, "PathTracer")

    GBufferRT = createPass("GBufferRT")
    g.addPass(GBufferRT, "GBufferRT")

    SVGFPass = createPass("SVGFPass", {
        'Enabled': True,
        'Iterations': 4,
        'FeedbackTap': 1,
        'VarianceEpsilon': 1e-4,
        'PhiColor': 10.0,
        'PhiNormal': 128.0,
        'Alpha': 0.05,
        'MomentsAlpha': 0.5,
    })
    g.addPass(SVGFPass, "SVGFPass")

    ErrorMeasurePass = createPass("ErrorMeasurePass", {
        'ComputeSquaredDifference': True,   # MSE(L2) 모드 사용
        'ComputeAverage': True,             # RGB 평균 오차 계산
        'IgnoreBackground': True,           # WorldPosition 입력으로 배경 픽셀 제외
        'UseLoadedReference': False,        # Reference 입력 채널 사용 (외부 파일 불필요)
    })
    g.addPass(ErrorMeasurePass, "ErrorMeasurePass")

    OverlayPass = createPass("OverlayPass")
    g.addPass(OverlayPass, "OverlayPass")

    # --- Edges ------------------------------------------------------------
    g.addEdge("GBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("GBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("GBufferRT.mvecW", "PathTracer.mvec")

    g.addEdge("PathTracer.color",      "SVGFPass.Color")
    g.addEdge("PathTracer.albedo",     "SVGFPass.Albedo")
    g.addEdge("GBufferRT.emissive",    "SVGFPass.Emission")             
    g.addEdge("GBufferRT.mvecW",       "SVGFPass.MotionVec")
    g.addEdge("GBufferRT.guideNormalW","SVGFPass.WorldNormal")
    g.addEdge("GBufferRT.posW",        "SVGFPass.WorldPosition")       
    g.addEdge("GBufferRT.linearZ",     "SVGFPass.LinearZ") 

    g.addEdge("SVGFPass.Filtered image", "ErrorMeasurePass.Source")
    g.addEdge("GBufferRT.linearZ",       "ErrorMeasurePass.WorldPosition")

    g.addEdge("ErrorMeasurePass.Output",  "OverlayPass.input")

    # --- Output -----------------------------------------------------------
    g.markOutput("OverlayPass.output")
    return g

PathTracerBaseline = render_graph_PathTracerBaseline()
try: m.addGraph(PathTracerBaseline)
except NameError: None

# ============================================================
# 캡처 설정
# ============================================================
ENABLE_AUTO_CAPTURE = _cfg["ENABLE_AUTO_CAPTURE"]
CAMERA_ANIMATION = _cfg["CAMERA_ANIMATION"]
CAPTURE_EVERY_N_FRAMES = 10   # 매 N프레임마다 캡처 (1 = 매 프레임)
CAPTURE_TOTAL_FRAMES = 600   # 총 프레임 수
FIXED_FRAMERATE = 60         # 카메라 애니메이션 고정 fps

OUTPUT_BASE = "C:/Users/bg001/Desktop/SWCONCapstoneDesign/Experiments"

try:
    if ENABLE_AUTO_CAPTURE:
        output_dir = OUTPUT_BASE + "/PSNR/ref_" + CAMERA_ANIMATION
        os.makedirs(output_dir, exist_ok=True)   # 캡처 폴더가 없으면 생성
        m.frameCapture.outputDir    = output_dir
        m.frameCapture.baseFilename = "ref"
        
        m.clock.framerate = FIXED_FRAMERATE
        m.clock.time = 0        # 시각 리셋
        m.clock.frame = 0       # 프레임 카운터 리셋
    
        # CAPTURE_TOTAL_FRAMES 도달 시 자동 종료
        # m.clock.exitFrame = CAPTURE_TOTAL_FRAMES

        # 캡처할 프레임 번호 목록을 미리 등록
        frames_to_capture = list(range(0, CAPTURE_TOTAL_FRAMES, CAPTURE_EVERY_N_FRAMES))
        m.frameCapture.addFrames(PathTracerBaseline, frames_to_capture)

except NameError:
    None  # 렌더 그래프 에디터에서 단독 로드 시 m이 없을 때 무시

