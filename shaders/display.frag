#version 440
layout(binding=0) uniform sampler2D renderedImage;
layout(binding=1) uniform sampler2D monitorLut;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 fragColor;

vec3 lutFetch(ivec3 q) {
    const int n=33;
    return texelFetch(monitorLut,ivec2(q.b*n+q.r,q.g),0).rgb;
}

vec3 toMonitor(vec3 encodedSrgb) {
    // 33^3 LUT packed as blue slices across a 1089x33 2D atlas. Use explicit
    // trilinear interpolation instead of float-texture linear filtering so the
    // display transform behaves identically on D3D11, Metal and OpenGL paths.
    const int n=33;
    vec3 p=clamp(encodedSrgb,vec3(0.0),vec3(1.0))*float(n-1);
    ivec3 q0=min(ivec3(floor(p)),ivec3(n-1));
    ivec3 q1=min(q0+ivec3(1),ivec3(n-1));
    vec3 f=p-vec3(q0);
    vec3 c000=lutFetch(ivec3(q0.r,q0.g,q0.b));
    vec3 c100=lutFetch(ivec3(q1.r,q0.g,q0.b));
    vec3 c010=lutFetch(ivec3(q0.r,q1.g,q0.b));
    vec3 c110=lutFetch(ivec3(q1.r,q1.g,q0.b));
    vec3 c001=lutFetch(ivec3(q0.r,q0.g,q1.b));
    vec3 c101=lutFetch(ivec3(q1.r,q0.g,q1.b));
    vec3 c011=lutFetch(ivec3(q0.r,q1.g,q1.b));
    vec3 c111=lutFetch(ivec3(q1.r,q1.g,q1.b));
    vec3 z0=mix(mix(c000,c100,f.r),mix(c010,c110,f.r),f.g);
    vec3 z1=mix(mix(c001,c101,f.r),mix(c011,c111,f.r),f.g);
    return clamp(mix(z0,z1,f.b),vec3(0.0),vec3(1.0));
}

void main() {
    // Manual bilinear interpolation keeps RGBA32F usable even on hardware where
    // floating-point render textures are sampleable but not linearly filterable.
    ivec2 size=textureSize(renderedImage,0);
    vec2 p=clamp(uv,vec2(0.0),vec2(1.0))*vec2(size)-vec2(.5);
    ivec2 a=ivec2(floor(p)); vec2 f=fract(p);
    ivec2 limit=size-ivec2(1);
    vec4 c00=texelFetch(renderedImage,clamp(a,ivec2(0),limit),0);
    vec4 c10=texelFetch(renderedImage,clamp(a+ivec2(1,0),ivec2(0),limit),0);
    vec4 c01=texelFetch(renderedImage,clamp(a+ivec2(0,1),ivec2(0),limit),0);
    vec4 c11=texelFetch(renderedImage,clamp(a+ivec2(1),ivec2(0),limit),0);
    vec4 rendered=mix(mix(c00,c10,f.x),mix(c01,c11,f.x),f.y);
    fragColor=vec4(toMonitor(rendered.rgb),rendered.a);
}
