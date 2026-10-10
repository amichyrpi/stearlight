#!/bin/sh
set -eu

runtime=${STEARLIGHT_STEAMVR_ROOT:-/opt/steamvr}
panel=$runtime/resources/webinterface/dashboard/chunk~1a88854fa.js
binding='const F=this.props.overlay_key,f=(0,h.w5)();'
embedded='y.properties["embedded-uv-index"]=this.m_nEmbeddedIndex'
bound='const F=this.props.overlay_key,f=document.title==="systemui"?"system.systemui":(0,h.w5)();'
explicit='y.properties["embedded-uv-index"]=document.title==="systemui"?void 0:this.m_nEmbeddedIndex'
# Frame OS 0.4.5 keeps the same atlas contract with renamed minified locals.
if [ -f "$panel" ] && grep -qF 'const f=this.props.overlay_key,F=(0,h.w5)();' "$panel";then
    binding='const f=this.props.overlay_key,F=(0,h.w5)();'
    bound='const f=this.props.overlay_key,F=document.title==="systemui"?"system.systemui":(0,h.w5)();'
elif [ -f "$panel" ] && grep -qF 'const f=this.props.overlay_key,F=document.title==="systemui"?' "$panel";then
    bound='const f=this.props.overlay_key,F=document.title==="systemui"?"system.systemui":(0,h.w5)();'
fi
if [ -f "$panel" ] && grep -qF "$bound" "$panel" && grep -qF "$explicit" "$panel"; then exit 0; fi
if [ ! -f "$panel" ] ||
   [ "$(grep -oF "$binding" "$panel" | wc -l)" -ne 1 ] ||
   [ "$(grep -oF "$embedded" "$panel" | wc -l)" -ne 1 ]; then
    echo 'Quest control adapter does not match this SteamVR UI; leaving it unchanged.' >&2
    exit 1
fi

# CPU-painted Frame controls already supply their atlas UV bounds. Embedded
# scanline coordinates sample the wrong regions on Quest. Keep Valve's controls
# and use their explicit bounds and systemui texture; external Steam panels keep
# their own overlay keys. Other browser pages retain the original behavior.
temporary=$panel.quest-controls
trap 'rm -f "$temporary"' EXIT
LC_ALL=C sed \
    -e 's/const F=this\.props\.overlay_key,f=(0,h\.w5)();/const F=this.props.overlay_key,f=document.title==="systemui"?"system.systemui":(0,h.w5)();/' \
    -e 's/const f=this\.props\.overlay_key,F=(0,h\.w5)();/const f=this.props.overlay_key,F=document.title==="systemui"?"system.systemui":(0,h.w5)();/' \
    -e 's/y\.properties\["embedded-uv-index"\]=this\.m_nEmbeddedIndex/y.properties["embedded-uv-index"]=document.title==="systemui"?void 0:this.m_nEmbeddedIndex/' \
    "$panel" > "$temporary"
grep -qF "$bound" "$temporary"
grep -qF "$explicit" "$temporary"
chmod --reference="$panel" "$temporary"
mv "$temporary" "$panel"
echo STEARLIGHT_QUEST_CONTROL_ATLAS_READY
