import time as _time

_t_start = None
_WARMUP = 100
_MEASURE = 500

def onFrameRender(rg, t, dt):
    global _t_start
    frame = m.clock.frame
    if frame == _WARMUP:
        _t_start = _time.perf_counter()
    elif frame == _WARMUP + _MEASURE and _t_start is not None:
        elapsed_ms = (_time.perf_counter() - _t_start) * 1000
        avg_ms = elapsed_ms / _MEASURE
        with open("C:/Users/bg001/Desktop/Falcor/Results/timing/test_timing.txt", "w") as f:
            f.write(f"{avg_ms:.4f} ms/frame\n")
        m.clock.exitFrame = frame + 1