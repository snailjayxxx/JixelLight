#version 440
layout(binding=0) uniform sampler2D renderedImage;
layout(binding=1) uniform sampler2D monitorLut;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 fragColor;

vec3 toMonitor(vec3 encodedSrgb) {
    // 33^3 LUT packed as blue slices across a 1089x33 2D atlas. The LUT
    // sampler is linear, so two samples interpolate R/G inside adjacent blue
    // slices and the final mix interpolates B. This keeps display ICC cost to
    // two filtered texture reads per presented pixel.
    const float n=33.0;
    vec3 p=clamp(encodedSrgb,vec3(0.0),vec3(1.0))*(n-1.0);
    float b0=floor(p.b);
    float b1=min(b0+1.0,n-1.0);
    float y=(p.g+0.5)/n;
    float x0=(b0*n+p.r+0.5)/(n*n);
    float x1=(b1*n+p.r+0.5)/(n*n);
    vec3 a=texture(monitorLut,vec2(x0,y)).rgb;
    vec3 b=texture(monitorLut,vec2(x1,y)).rgb;
    return clamp(mix(a,b,fract(p.b)),vec3(0.0),vec3(1.0));
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
