import subprocess

from .privileged import call

CONTROLLER_TYPE = "/usr/libexec/armada/controller-type"
DEFAULT_TYPE = "deck-uhid"
CONTROLLER_TYPES = {
    "deck-uhid": "Steam Deck",
    "xb360": "Xbox 360",
    "ds5": "DualSense",
}


def controller_type():
    try:
        value = str(call("get_controller_type").get("value") or "")
        if value in CONTROLLER_TYPES:
            return value
    except Exception:
        pass
    try:
        value = subprocess.check_output((CONTROLLER_TYPE, "get"), text=True, timeout=3).strip()
    except (OSError, subprocess.SubprocessError):
        return DEFAULT_TYPE
    return value if value in CONTROLLER_TYPES else DEFAULT_TYPE


def set_controller_type(value):
    if value not in CONTROLLER_TYPES:
        raise ValueError("invalid controller type")
    return str(call("set_controller_type", value=value).get("value") or controller_type())
