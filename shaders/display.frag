#version 440
layout(binding=0) uniform sampler2D renderedImage;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 fragColor;
void main() {
    // Manual bilinear interpolation keeps RGBA32F usable even on hardware where
    // floating-point textures are sampleable but not linearly filterable.
    ivec2 size=textureSize(renderedImage,0);
    vec2 p=clamp(uv,vec2(0.0),vec2(1.0))*vec2(size)-vec2(.5);
    ivec2 a=ivec2(floor(p)); vec2 f=fract(p);
    ivec2 limit=size-ivec2(1);
    vec4 c00=texelFetch(renderedImage,clamp(a,ivec2(0),limit),0);
    vec4 c10=texelFetch(renderedImage,clamp(a+ivec2(1,0),ivec2(0),limit),0);
    vec4 c01=texelFetch(renderedImage,clamp(a+ivec2(0,1),ivec2(0),limit),0);
    vec4 c11=texelFetch(renderedImage,clamp(a+ivec2(1),ivec2(0),limit),0);
    fragColor=mix(mix(c00,c10,f.x),mix(c01,c11,f.x),f.y);
}
