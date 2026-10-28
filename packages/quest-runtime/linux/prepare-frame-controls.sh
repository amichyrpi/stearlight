#!/bin/sh
set -eu

runtime=${STEARLIGHT_STEAMVR_ROOT:-/opt/steamvr}
# Empty/hidden panels must clear all three packed UV pixels. Clearing o+1
# three times leaves the old validity marker and bounds in the painted atlas.
# Run this repair even when the panel binding adapter is already installed.
painter=$runtime/resources/webinterface/dashboard/chunk~336cd93ad.js
broken='for(let e=0;e<3;e++)this.setPixel(o+1,0,0,0,0)'
fixed='for(let e=0;e<3;e++)this.setPixel(o+e,0,0,0,0)'
if [ -f "$painter" ] && grep -qF "$broken" "$painter";then
    if [ "$(grep -oF "$broken" "$painter" | wc -l)" -ne 1 ];then
        echo 'Quest atlas cleanup adapter is ambiguous; leaving it unchanged.' >&2
        exit 1
    fi
    temporary=$painter.quest-atlas-clear
    trap 'rm -f "$temporary"' EXIT
    LC_ALL=C sed 's/for(let e=0;e<3;e++)this\.setPixel(o+1,0,0,0,0)/for(let e=0;e<3;e++)this.setPixel(o+e,0,0,0,0)/' "$painter" > "$temporary"
    grep -qF "$fixed" "$temporary"
    chmod --reference="$painter" "$temporary"
    mv "$temporary" "$painter"
    trap - EXIT
    echo STEARLIGHT_QUEST_ATLAS_CLEAR_READY
fi
panel=$runtime/resources/webinterface/dashboard/chunk~1a88854fa.js
binding='const F=this.props.overlay_key,f=(0,h.w5)();'
embedded='y.properties["embedded-uv-index"]=this.m_nEmbeddedIndex'
bound='const F=this.props.overlay_key,f=document.title==="systemui"?"system.systemui":(0,h.w5)();'
legacy_explicit='y.properties["embedded-uv-index"]=document.title==="systemui"?void 0:this.m_nEmbeddedIndex'
explicit='y.properties["embedded-uv-index"]=document.title==="systemui"&&this.props.overlay_key?void 0:this.m_nEmbeddedIndex'
# Frame OS 0.4.5 keeps the same atlas contract with renamed minified locals.
if [ -f "$panel" ] && grep -qF 'const f=this.props.overlay_key,F=(0,h.w5)();' "$panel";then
    binding='const f=this.props.overlay_key,F=(0,h.w5)();'
    bound='const f=this.props.overlay_key,F=document.title==="systemui"?"system.systemui":(0,h.w5)();'
elif [ -f "$panel" ] && grep -qF 'const f=this.props.overlay_key,F=document.title==="systemui"?' "$panel";then
    bound='const f=this.props.overlay_key,F=document.title==="systemui"?"system.systemui":(0,h.w5)();'
fi
if [ -f "$panel" ] && grep -qF "$bound" "$panel" && grep -qF "$explicit" "$panel"; then exit 0; fi
if [ ! -f "$panel" ] || ! {
   { [ "$(grep -oF "$binding" "$panel" | wc -l)" -eq 1 ] &&
     [ "$(grep -oF "$embedded" "$panel" | wc -l)" -eq 1 ]; } ||
   { [ "$(grep -oF "$bound" "$panel" | wc -l)" -eq 1 ] &&
     [ "$(grep -oF "$legacy_explicit" "$panel" | wc -l)" -eq 1 ]; }
}; then
    echo 'Quest control adapter does not match this SteamVR UI; leaving it unchanged.' >&2
    exit 1
fi

# Bind internal panels to the actual systemui atlas. Its embedded coordinates
# travel with the painted pixels: publishing layout UVs ahead of a repaint
# briefly samples old controls when Settings or a game changes the atlas layout.
# External Steam panels keep their own overlay keys and explicit UV bounds.
# Other browser pages retain the original behavior.
temporary=$panel.quest-controls
trap 'rm -f "$temporary"' EXIT
LC_ALL=C sed \
    -e 's/const F=this\.props\.overlay_key,f=(0,h\.w5)();/const F=this.props.overlay_key,f=document.title==="systemui"?"system.systemui":(0,h.w5)();/' \
    -e 's/const f=this\.props\.overlay_key,F=(0,h\.w5)();/const f=this.props.overlay_key,F=document.title==="systemui"?"system.systemui":(0,h.w5)();/' \
    -e 's/y\.properties\["embedded-uv-index"\]=this\.m_nEmbeddedIndex/y.properties["embedded-uv-index"]=document.title==="systemui"\&\&this.props.overlay_key?void 0:this.m_nEmbeddedIndex/' \
    -e 's/y\.properties\["embedded-uv-index"\]=document\.title==="systemui"?void 0:this\.m_nEmbeddedIndex/y.properties["embedded-uv-index"]=document.title==="systemui"\&\&this.props.overlay_key?void 0:this.m_nEmbeddedIndex/' \
    "$panel" > "$temporary"
grep -qF "$bound" "$temporary"
grep -qF "$explicit" "$temporary"
chmod --reference="$panel" "$temporary"
mv "$temporary" "$panel"
echo STEARLIGHT_QUEST_CONTROL_ATLAS_READY
