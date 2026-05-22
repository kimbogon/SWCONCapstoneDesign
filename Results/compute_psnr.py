"""
compute_psnr.py
---------------
ref / test 디렉터리에 있는 EXR 프레임 시퀀스를 읽어
프레임별 PSNR / MSE 를 계산하고 CSV 및 요약 통계를 출력한다.

사용법:
    python compute_psnr.py \
        --ref   C:/Users/bg001/Desktop/Falcor/Results/reference \
        --test  C:/Users/bg001/Desktop/Falcor/Results/test \
        --out   C:/Users/bg001/Desktop/Falcor/Results/psnr_result.csv

옵션:
    --ref       레퍼런스 EXR 디렉터리 (기본값: ./reference)
    --test      테스트 EXR 디렉터리   (기본값: ./test)
    --out       결과 CSV 저장 경로     (기본값: ./psnr_result.csv)
    --ref-pat   레퍼런스 파일명 패턴   (기본값: ref_AccumulatePass_output_{frame}.exr)
    --test-pat  테스트 파일명 패턴     (기본값: test_AccumulatePass_output_{frame}.exr)
    --step      캡처 프레임 간격       (기본값: 10)
    --total     총 프레임 수           (기본값: 300)
    --maxval    PSNR 계산용 최대값.
                'auto' 이면 레퍼런스 전체 최대값 사용 (기본값: auto)
    --no-plot   그래프 출력 생략
"""

import argparse
import os
import csv
import sys

import numpy as np

# ──────────────────────────────────────────────
# EXR 읽기 (OpenEXR 우선, 없으면 imageio 시도)
# ──────────────────────────────────────────────
def read_exr(path: str) -> np.ndarray:
    """EXR 파일을 float32 (H, W, 3) numpy 배열로 반환."""
    try:
        import OpenEXR, Imath
        f = OpenEXR.InputFile(path)
        dw = f.header()["dataWindow"]
        w = dw.max.x - dw.min.x + 1
        h = dw.max.y - dw.min.y + 1
        pt = Imath.PixelType(Imath.PixelType.FLOAT)
        R = np.frombuffer(f.channel("R", pt), dtype=np.float32).reshape(h, w)
        G = np.frombuffer(f.channel("G", pt), dtype=np.float32).reshape(h, w)
        B = np.frombuffer(f.channel("B", pt), dtype=np.float32).reshape(h, w)
        return np.stack([R, G, B], axis=-1)
    except ImportError:
        pass

    try:
        import imageio
        img = imageio.imread(path)          # imageio[freeimage] 로 EXR 지원
        return img.astype(np.float32)
    except Exception as e:
        raise RuntimeError(f"EXR 읽기 실패: {path}\n  {e}\n"
                           "OpenEXR 또는 imageio[freeimage] 패키지를 설치하세요.")


# ──────────────────────────────────────────────
# 지표 계산
# ──────────────────────────────────────────────
def compute_metrics(ref: np.ndarray, test: np.ndarray, max_val: float):
    """MSE, PSNR (dB) 반환. ref/test: float32 (H,W,3)."""
    diff = ref.astype(np.float64) - test.astype(np.float64)
    mse = float(np.mean(diff ** 2))
    if mse == 0.0:
        psnr = float("inf")
    else:
        psnr = float(10.0 * np.log10((max_val ** 2) / mse))
    return mse, psnr


# ──────────────────────────────────────────────
# 메인
# ──────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="EXR 프레임 시퀀스 PSNR/MSE 계산")
    parser.add_argument("--ref",      default="./reference")
    parser.add_argument("--test",     default="./test")
    parser.add_argument("--out",      default="./psnr_result.csv")
    parser.add_argument("--ref-pat",  default="ref.AccumulatePass.output.{frame}.exr",
                        help="레퍼런스 파일명 패턴. {frame} 이 프레임 번호로 치환됨")
    parser.add_argument("--test-pat", default="test.AccumulatePass.output.{frame}.exr",
                        help="테스트 파일명 패턴")
    parser.add_argument("--step",     type=int, default=10,  help="캡처 프레임 간격")
    parser.add_argument("--total",    type=int, default=300, help="총 프레임 수")
    parser.add_argument("--maxval",   default="auto",
                        help="PSNR 최대값 ('auto' 또는 float 값)")
    parser.add_argument("--no-plot",  action="store_true", help="그래프 출력 생략")
    args = parser.parse_args()

    frames = list(range(0, args.total, args.step))
    print(f"[설정] 프레임 수: {len(frames)}  (0 ~ {frames[-1]}, 간격 {args.step})")
    print(f"       ref  디렉터리: {args.ref}")
    print(f"       test 디렉터리: {args.test}")

    # ── 1단계: maxval 결정 ──────────────────────
    if args.maxval == "auto":
        print("\n[1/3] maxval 자동 결정 중 (레퍼런스 전체 스캔)...")
        global_max = 0.0
        missing = []
        for fr in frames:
            path = os.path.join(args.ref, args.ref_pat.format(frame=fr))
            if not os.path.exists(path):
                missing.append(fr)
                continue
            img = read_exr(path)
            global_max = max(global_max, float(img.max()))
        if missing:
            print(f"  ⚠ 레퍼런스 파일 없음: 프레임 {missing}")
        max_val = global_max if global_max > 0 else 1.0
        print(f"  → maxval = {max_val:.6f}")
    else:
        max_val = float(args.maxval)
        print(f"\n[1/3] maxval = {max_val:.6f} (수동 지정)")

    # ── 2단계: 프레임별 계산 ───────────────────
    print("\n[2/3] 프레임별 PSNR/MSE 계산 중...")
    results = []   # list of dict
    skipped = []

    for fr in frames:
        ref_path  = os.path.join(args.ref,  args.ref_pat.format(frame=fr))
        test_path = os.path.join(args.test, args.test_pat.format(frame=fr))

        if not os.path.exists(ref_path):
            print(f"  SKIP  frame {fr:4d} — ref 없음:  {ref_path}")
            skipped.append(fr)
            continue
        if not os.path.exists(test_path):
            print(f"  SKIP  frame {fr:4d} — test 없음: {test_path}")
            skipped.append(fr)
            continue

        ref_img  = read_exr(ref_path)
        test_img = read_exr(test_path)

        if ref_img.shape != test_img.shape:
            print(f"  SKIP  frame {fr:4d} — 해상도 불일치 "
                  f"ref={ref_img.shape} test={test_img.shape}")
            skipped.append(fr)
            continue

        mse, psnr = compute_metrics(ref_img, test_img, max_val)
        results.append({"frame": fr, "mse": mse, "psnr": psnr})
        print(f"  frame {fr:4d}  MSE={mse:.6f}  PSNR={psnr:7.3f} dB")

    if not results:
        print("\n유효한 프레임이 없습니다. 경로/패턴을 확인하세요.")
        sys.exit(1)

    # ── 3단계: 집계 및 저장 ────────────────────
    mse_vals  = [r["mse"]  for r in results]
    # inf(MSE=0) 프레임은 집계/그래프에서 제외하고 별도 표시
    inf_frames   = [r["frame"] for r in results if r["psnr"] == float("inf")]
    finite_results = [r for r in results if r["psnr"] != float("inf")]
    psnr_vals    = [r["psnr"] for r in finite_results]

    print("\n[3/3] 집계 결과")
    print(f"  유효 프레임:  {len(results)} / {len(frames)}")
    if skipped:
        print(f"  스킵 프레임:  {skipped}")
    if inf_frames:
        print(f"  PSNR=inf 프레임 (MSE=0, 집계 제외): {inf_frames}")
    print(f"  PSNR  평균={np.mean(psnr_vals):.3f} dB  "
          f"최소={np.min(psnr_vals):.3f}  최대={np.max(psnr_vals):.3f}")
    print(f"  MSE   평균={np.mean(mse_vals):.6f}  "
          f"최소={np.min(mse_vals):.6f}  최대={np.max(mse_vals):.6f}")
    print(f"  사용 maxval: {max_val:.6f}")

    # CSV 저장
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["frame", "mse", "psnr"])
        writer.writeheader()
        writer.writerows(results)
        # 집계 행 추가
        writer.writerow({"frame": "MEAN",
                         "mse":   f"{np.mean(mse_vals):.6f}",
                         "psnr":  f"{np.mean(psnr_vals):.3f}"})
        writer.writerow({"frame": "MIN",
                         "mse":   f"{np.min(mse_vals):.6f}",
                         "psnr":  f"{np.min(psnr_vals):.3f}"})
        writer.writerow({"frame": "MAX",
                         "mse":   f"{np.max(mse_vals):.6f}",
                         "psnr":  f"{np.max(psnr_vals):.3f}"})
    print(f"\n  → CSV 저장: {args.out}")

    # ── 4단계: 그래프 ─────────────────────────
    if not args.no_plot:
        try:
            import matplotlib.pyplot as plt
            fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
            fr_list      = [r["frame"] for r in results]        # MSE용 (전체)
            fr_list_psnr = [r["frame"] for r in finite_results]  # PSNR용 (inf 제외)

            axes[0].plot(fr_list_psnr, psnr_vals, marker="o", markersize=4, color="steelblue")
            axes[0].axhline(np.mean(psnr_vals), color="red", linestyle="--",
                            label=f"평균 {np.mean(psnr_vals):.2f} dB")
            axes[0].set_ylabel("PSNR (dB)")
            axes[0].set_title("프레임별 PSNR")
            axes[0].legend()
            axes[0].grid(True, alpha=0.4)

            axes[1].plot(fr_list, mse_vals, marker="o", markersize=4, color="darkorange")
            axes[1].axhline(np.mean(mse_vals), color="red", linestyle="--",
                            label=f"평균 {np.mean(mse_vals):.5f}")
            axes[1].set_ylabel("MSE")
            axes[1].set_xlabel("Frame")
            axes[1].set_title("프레임별 MSE")
            axes[1].legend()
            axes[1].grid(True, alpha=0.4)

            plt.tight_layout()
            plot_path = os.path.splitext(args.out)[0] + "_plot.png"
            plt.savefig(plot_path, dpi=150)
            print(f"  → 그래프 저장: {plot_path}")
            plt.show()
        except ImportError:
            print("  (matplotlib 없음 — 그래프 생략)")


if __name__ == "__main__":
    main()
