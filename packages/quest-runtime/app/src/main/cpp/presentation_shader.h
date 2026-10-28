#pragma once
namespace quest {
inline constexpr const char* presentationVertex="#version 300 es\nprecision highp float;out vec2 uv;uniform int eye;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=vec2((p.x+float(eye))*0.5,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
inline constexpr const char* presentationFragment="#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D frame;uniform int format;uniform bool preserveAlpha;uniform bool decode;out vec4 color;void main(){color=texture(frame,uv);if(format==44||format==50)color.rgb=color.bgr;if(decode){vec3 low=color.rgb/12.92;vec3 high=pow((color.rgb+0.055)/1.055,vec3(2.4));color.rgb=mix(low,high,step(vec3(0.04045),color.rgb));}if(!preserveAlpha)color.a=1.0;}";
}
