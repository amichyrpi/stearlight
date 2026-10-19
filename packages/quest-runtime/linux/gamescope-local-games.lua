-- Select the local game's native Frame dashboard overlay when it first paints.
-- Its dedicated single-application compositor keeps helper windows from
-- creating another overlay and taking focus. Other sessions retain their policy.
if os.getenv("STEARLIGHT_FEX_GAMESCOPE") == "1" or os.getenv("STEARLIGHT_PROTON_GAMESCOPE") == "1" then
    gamescope.convars.vr_nudge_to_visible.value = true
    gamescope.convars.vr_nudge_to_visible_per_connector.value = false
end
