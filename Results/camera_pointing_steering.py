# ============================================================
# camera_pointing_steering.py  —  Pointing + Steering 시나리오 카메라 keyframe
#
# [사용 방법]
#   FPS.pyscene의 "# 8. 카메라 자동 궤적" 섹션에 아래 코드를 붙여넣기.
#   camera 변수는 섹션 5에서 이미 정의되어 있어야 한다.
#
# [시나리오]
#   0 ~ 3초 : 카메라 (0,1.7,-8) → (0,1.7,-2) 전진 (Steering)
#   3 ~ 4초 : (0,1.7,-2) 고정, -x 방향으로 90° 회전
#   4 ~ 7초 : 카메라 (0,1.7,-2) → (-6,1.7,-2) 측면 이동 (Steering)
#   7 ~10초 : (-6,1.7,-2) 고정, 아래로 회전하여 초록 큐브 조준 (Pointing)
#
# [오브젝트 위치 (FPS.pyscene 기준)]
#   초록 큐브: center=(-7, 0.25, -2)
#
# [좌표계 참고]
#   카메라 초기 forward = +z. "오른쪽 90°" 명령이지만
#   이후 이동 방향이 -x("전방")이므로 -x 방향 전환으로 구현함.
#   +x 방향 전환이 의도라면 t=4의 target을 (10, 1.7, -2)로 교체.
# ============================================================

_up = float3(0, 1, 0)

_cam_anim = sceneBuilder.createAnimation(camera, 'pointing_steering_trajectory', 10.0)
_cam_anim.postInfinityBehavior = Animation.Behavior.Constant
_cam_anim.interpolationMode = Animation.InterpolationMode.Linear

# ---------- 0~3초: 전진 구간 (Steering) ----------
# 카메라 +z 방향으로 이동. target을 항상 10m 전방으로 유지.

# t=0: pos=(0,1.7,-8), 전방 +z
_cam_anim.addKeyframe(0.0, Transform(
    position=float3( 0, 1.7, -8),
    target=float3(  0, 1.7,  2),
    up=_up))

# t=3: pos=(0,1.7,-2), 전진 완료
_cam_anim.addKeyframe(3.0, Transform(
    position=float3( 0, 1.7, -2),
    target=float3(  0, 1.7,  8),
    up=_up))

# ---------- 3~4초: 회전 구간 (-x 방향 전환) ----------
# t=4: pos=(0,1.7,-2), 전방 -x (이후 이동 방향과 일치)
_cam_anim.addKeyframe(4.0, Transform(
    position=float3(  0, 1.7, -2),
    target=float3( -10, 1.7, -2),
    up=_up))

# ---------- 4~7초: 측면 이동 구간 (Steering) ----------
# 카메라 -x 방향으로 이동. target offset = (-10, 0, 0) 유지.

# t=7: pos=(-6,1.7,-2), 이동 완료
_cam_anim.addKeyframe(7.0, Transform(
    position=float3( -6, 1.7, -2),
    target=float3( -10, 1.7, -2),
    up=_up))

# ---------- 7~10초: 하향 회전 구간 (Pointing) ----------
# 카메라 (-6,1.7,-2) 고정. 초록 큐브 center(-7, 0.25, -2) 조준.

# t=8: 초록 큐브 정조준, 카메라 정지
_cam_anim.addKeyframe(8.0, Transform(
    position=float3(-6, 1.7, -2),
    target=float3( -7, 0.25, -2),
    up=_up))

# t=10: 초록 큐브 정조준, 카메라 정지
_cam_anim.addKeyframe(10.0, Transform(
    position=float3(-6, 1.7, -2),
    target=float3( -7, 0.25, -2),
    up=_up))

sceneBuilder.addAnimation(_cam_anim)
