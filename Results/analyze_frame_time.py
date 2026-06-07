"""
analyze_frame_time.py — 1.2 frame time 측정 및 보고

[원리]
  Falcor가 캡처한 NUM_CAPTURES(=6)장의 파일을 프레임 번호 순으로 정렬하고,
  인접한 쌍들의 파일 시스템 mtime 차이를 구간 프레임 수로 나눠
  구간별 평균 frame time을 계산한다.
  5구간의 평균(mean)과 표준편차(std)를 최종 결과로 제시한다.

[전제]
  PathTracerBaselineTiming.py / PathTracerAdaptiveTiming.py 실행 후
  아래 OUTPUT_BASE 아래에 캡처 파일이 생성되어 있어야 한다.

  디렉터리 구조 (프레임 번호는 캡처 오프셋에 따라 달라짐):
    {OUTPUT_BASE}/baseline_aiming/    timing_base.OverlayPass.output.{N}.exr (6장)
    {OUTPUT_BASE}/baseline_pointing/  ...
    {OUTPUT_BASE}/adaptive_aiming/    timing_adaptive.OverlayPass.output.{N}.exr (6장)
    {OUTPUT_BASE}/adaptive_pointing/  ...

  파일들은 프레임 번호 순으로 정렬되며, 인접 쌍마다 독립적으로 구간 측정된다.

[출력]
  - 콘솔: 시나리오별 구간별 frame time 및 mean ± std 표
  - frame_time_results.csv
  - frame_time_comparison.png (오차 막대 포함 bar chart)
"""

import os
import re
import csv
import glob
import statistics

# ============================================================
# 설정
# ============================================================
OUTPUT_BASE = "C:/Users/bg001/Desktop/Falcor/Results/timing"

# 결과물 저장 경로
RESULTS_DIR = OUTPUT_BASE
CSV_PATH    = os.path.join(RESULTS_DIR, "frame_time_results.csv")
PLOT_PATH   = os.path.join(RESULTS_DIR, "frame_time_comparison.png")

# ============================================================
# 파일 시퀀스 탐색
# ============================================================
def find_frame_sequence(run_dir: str, base_filename: str):
    """
    run_dir 안의 캡처 파일 전체를 스캔해 프레임 번호 오름차순으로
    [(frame_no, filepath), ...] 리스트를 반환한다.

    Falcor 캡처 파일명 패턴: {baseFilename}.{output_name}.{frame}.exr
    output_name에 점(.)이 포함될 수 있으므로 마지막 숫자 토큰을 프레임 번호로 파싱한다.
    """
    pattern = os.path.join(run_dir, f"{base_filename}.*.exr")
    all_files = glob.glob(pattern)

    # 파일명 끝의 숫자(.{frame}.exr)를 프레임 번호로 추출
    frame_map: dict[int, str] = {}
    for filepath in all_files:
        m = re.search(r'\.(\d+)\.exr$', os.path.basename(filepath))
        if m:
            frame_no = int(m.group(1))
            # 같은 프레임 번호가 여러 파일이면 가장 최근 것을 사용
            if frame_no not in frame_map or \
               os.path.getmtime(filepath) > os.path.getmtime(frame_map[frame_no]):
                frame_map[frame_no] = filepath

    return sorted(frame_map.items())  # [(frame_no, path), ...]


# ============================================================
# 구간별 frame time 계산 → mean ± std 반환
# ============================================================
def compute_avg_frame_time(run_dir: str, base_filename: str) -> tuple:
    """
    캡처 파일 시퀀스에서 인접 쌍들의 구간별 frame time(ms)을 계산하고
    (mean_ms, std_ms) 를 반환한다.
    캡처 파일이 2개 미만이면 (None, None) 반환.
    """
    sequence = find_frame_sequence(run_dir, base_filename)

    if len(sequence) < 2:
        print(f"  [경고] {run_dir}: 캡처 파일이 2개 미만 → 건너뜀")
        return None, None

    measurements = []
    for i in range(len(sequence) - 1):
        fn_start, path_start = sequence[i]
        fn_end,   path_end   = sequence[i + 1]
        frame_diff = fn_end - fn_start
        if frame_diff <= 0:
            continue

        elapsed_ms = (os.path.getmtime(path_end) - os.path.getmtime(path_start)) * 1000.0
        interval_avg_ms = elapsed_ms / frame_diff
        measurements.append(interval_avg_ms)

        print(f"  구간 {i + 1}: frame {fn_start}→{fn_end} ({frame_diff}f) "
              f" {elapsed_ms:.1f} ms  →  {interval_avg_ms:.3f} ms/f "
              f" ({1000.0 / interval_avg_ms:.1f} FPS)")

    if not measurements:
        return None, None

    mean_ms = statistics.mean(measurements)
    # 구간이 1개뿐이면 std 계산 불가 → 0으로 처리
    std_ms  = statistics.stdev(measurements) if len(measurements) > 1 else 0.0

    print(f"  ─────────────────────────────────────────────────────")
    print(f"  평균: {mean_ms:.3f} ms  std: {std_ms:.3f} ms  ({1000.0 / mean_ms:.1f} FPS)")
    return mean_ms, std_ms


# ============================================================
# 측정 실행
# ============================================================
RUNS = [
    # (파이프라인, 시나리오, 서브디렉터리, 기본파일명)
    ("Baseline", "Aiming",   "baseline_aiming",   "timing_base"),
    ("Baseline", "Pointing", "baseline_pointing", "timing_base"),
    ("Adaptive", "Aiming",   "adaptive_aiming",   "timing_adaptive"),
    ("Adaptive", "Pointing", "adaptive_pointing", "timing_adaptive"),
]

results = []
print("=" * 60)
print("Frame Time 분석")
print("=" * 60)

for pipeline, scenario, subdir, base_fn in RUNS:
    run_dir = os.path.join(OUTPUT_BASE, subdir)
    print(f"\n[{pipeline} / {scenario}]  →  {run_dir}")

    if not os.path.isdir(run_dir):
        print(f"  [경고] 디렉터리 없음: {run_dir}")
        mean_ms = std_ms = None
    else:
        mean_ms, std_ms = compute_avg_frame_time(run_dir, base_fn)

    results.append({
        "pipeline":            pipeline,
        "scenario":            scenario,
        "mean_frame_time_ms":  round(mean_ms, 3) if mean_ms is not None else "N/A",
        "std_frame_time_ms":   round(std_ms,  3) if std_ms  is not None else "N/A",
        "fps":                 round(1000.0 / mean_ms, 1) if mean_ms is not None else "N/A",
    })

# ============================================================
# 결과 출력
# ============================================================
print("\n" + "=" * 60)
print(f"{'Pipeline':<12} {'Scenario':<22} {'Mean Frame Time':>16}  {'Std':>8}  {'FPS':>8}")
print("-" * 60)
for r in results:
    ft  = f"{r['mean_frame_time_ms']} ms" if r['mean_frame_time_ms'] != "N/A" else "N/A"
    std = f"±{r['std_frame_time_ms']} ms" if r['std_frame_time_ms']  != "N/A" else "N/A"
    fps = str(r['fps'])
    print(f"{r['pipeline']:<12} {r['scenario']:<22} {ft:>16}  {std:>10}  {fps:>8}")
print("=" * 60)

# ============================================================
# CSV 저장
# ============================================================
os.makedirs(RESULTS_DIR, exist_ok=True)
with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "pipeline", "scenario", "mean_frame_time_ms", "std_frame_time_ms", "fps"
    ])
    writer.writeheader()
    writer.writerows(results)
print(f"\nCSV 저장: {CSV_PATH}")

# ============================================================
# Bar chart 생성 (오차 막대 포함)
# ============================================================
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    scenarios = ["Aiming", "Pointing"]
    pipelines = ["Baseline", "Adaptive"]
    colors    = {"Baseline": "#4C72B0", "Adaptive": "#DD8452"}

    # 유효한 측정값만 추출
    means = {}
    stds  = {}
    for r in results:
        key = (r["pipeline"], r["scenario"])
        m_val = r["mean_frame_time_ms"]
        s_val = r["std_frame_time_ms"]
        means[key] = float(m_val) if m_val != "N/A" else 0.0
        stds[key]  = float(s_val) if s_val != "N/A" else 0.0

    x     = np.arange(len(scenarios))
    width = 0.35
    fig, ax = plt.subplots(figsize=(8, 5))

    for i, pipe in enumerate(pipelines):
        vals = [means.get((pipe, sc), 0.0) for sc in scenarios]
        errs = [stds.get((pipe, sc),  0.0) for sc in scenarios]
        bars = ax.bar(x + (i - 0.5) * width, vals, width,
                      label=pipe, color=colors[pipe], alpha=0.85,
                      yerr=errs, capsize=5, error_kw={"elinewidth": 1.5})
        # 막대 위에 수치 표시
        for bar, v, e in zip(bars, vals, errs):
            if v > 0:
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + e + 0.3,
                        f"{v:.1f} ms", ha="center", va="bottom", fontsize=9)

    ax.set_xlabel("Scenario")
    ax.set_ylabel("Mean Frame Time (ms)")
    ax.set_title("Frame Time Comparison: Baseline vs Adaptive\n(error bars = ±1 std, 5 intervals per run)")
    ax.set_xticks(x)
    ax.set_xticklabels(scenarios)
    ax.legend()
    ax.set_ylim(bottom=0)
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    plt.tight_layout()
    plt.savefig(PLOT_PATH, dpi=150)
    print(f"Bar chart 저장: {PLOT_PATH}")

except ImportError:
    print("[알림] matplotlib 없음 — bar chart 생성 건너뜀 (pip install matplotlib)")
