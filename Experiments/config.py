PRESETS = {
    "default": {
        "ref": {"ENABLE_AUTO_CAPTURE": False, "CAMERA_ANIMATION": 'none'},
        "baseline": {"ENABLE_AUTO_CAPTURE": False, "ENABLE_FRAMETIME_MEASUREMENT": False, "CAMERA_ANIMATION": 'none'},
        "proposed": {"ENABLE_AUTO_CAPTURE": False, "ENABLE_FRAMETIME_MEASUREMENT": False, "CAMERA_ANIMATION": 'none'},
        "pyscene": {"CAMERA_ANIMATION": 'none'}
    },
    "exp1": {
        "ref": {"ENABLE_AUTO_CAPTURE": True, "CAMERA_ANIMATION": 'aiming'},
        "baseline": {"ENABLE_AUTO_CAPTURE": True, "ENABLE_FRAMETIME_MEASUREMENT": False, "CAMERA_ANIMATION": 'aiming'},
        "proposed": {"ENABLE_AUTO_CAPTURE": True, "ENABLE_FRAMETIME_MEASUREMENT": False, "CAMERA_ANIMATION": 'aiming'},
        "pyscene": {"CAMERA_ANIMATION": 'aiming'}
    }, # aiming 시나리오에서 PSNR 측정
    "exp2": {
        "ref": {"ENABLE_AUTO_CAPTURE": True, "CAMERA_ANIMATION": 'pointing'},
        "baseline": {"ENABLE_AUTO_CAPTURE": True, "ENABLE_FRAMETIME_MEASUREMENT": False, "CAMERA_ANIMATION": 'pointing'},
        "proposed": {"ENABLE_AUTO_CAPTURE": True, "ENABLE_FRAMETIME_MEASUREMENT": False, "CAMERA_ANIMATION": 'pointing'},
        "pyscene": {"CAMERA_ANIMATION": 'pointing'}
    }, # pointing 시나리오에서 PSNR 측정
    "exp3": {
        "ref": {"ENABLE_AUTO_CAPTURE": False, "CAMERA_ANIMATION": 'aiming'},
        "baseline": {"ENABLE_AUTO_CAPTURE": False, "ENABLE_FRAMETIME_MEASUREMENT": True, "CAMERA_ANIMATION": 'aiming'},
        "proposed": {"ENABLE_AUTO_CAPTURE": False, "ENABLE_FRAMETIME_MEASUREMENT": True, "CAMERA_ANIMATION": 'aiming'},
        "pyscene": {"CAMERA_ANIMATION": 'aiming'}
    }, # aiming 시나리오에서 frame time 측정
    "exp4": {
        "ref": {"ENABLE_AUTO_CAPTURE": False, "CAMERA_ANIMATION": 'pointing'},
        "baseline": {"ENABLE_AUTO_CAPTURE": False, "ENABLE_FRAMETIME_MEASUREMENT": True, "CAMERA_ANIMATION": 'pointing'},
        "proposed": {"ENABLE_AUTO_CAPTURE": False, "ENABLE_FRAMETIME_MEASUREMENT": True, "CAMERA_ANIMATION": 'pointing'},
        "pyscene": {"CAMERA_ANIMATION": 'pointing'}
    } # pointing 시나리오에서 frame time 측정
}

CURRENT_PRESET = "default"