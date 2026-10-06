-- The patched Windows renderer shares linear DMA-BUF images through Vulkan
-- handles. Native Steam VR surface forwarding requires these modifiers.
if os.getenv("STEARLIGHT_GAMESCOPE_VM_UPLOAD") == "1" then
    gamescope.convars.vr_use_modifiers.value = true
end
