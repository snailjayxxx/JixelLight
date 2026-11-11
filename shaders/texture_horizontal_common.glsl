layout(local_size_x=16,local_size_y=16) in;
layout(std140,binding=0) uniform Plan {vec4 p[56];};
layout(rgba32f,binding=1) readonly uniform image2D guideImage;
layout(rgba32f,binding=2) writeonly uniform image2D horizontalImage;
layout(std140,binding=3) uniform Kernel {vec4 weights[6];};
float weightAt(int n){int i=n+TEXTURE_WEIGHT_OFFSET;return weights[i/4][i%4];}
void main() {
    ivec2 c=ivec2(gl_GlobalInvocationID.xy),size=ivec2(p[24].xy);if(any(greaterThanEqual(c,size)))return;
    vec4 original=imageLoad(guideImage,c);precise float reference=original.r;
    int extent=int(ceil(TEXTURE_RADIUS));
    if(original.a<=1.e-20)for(int i=-extent;i<=extent;++i) {
        vec4 pixel=imageLoad(guideImage,ivec2(clamp(c.x+i,0,size.x-1),c.y));
        if(pixel.a>1.e-20){reference=pixel.r;break;}
    }
    precise float value=0.0,alpha=0.0;
    for(int i=-extent;i<=extent;++i) {
        vec4 pixel=imageLoad(guideImage,ivec2(clamp(c.x+i,0,size.x-1),c.y));if(pixel.a<=1.e-20)continue;
        precise float weight=weightAt(i+extent);
        value+=weight*(pixel.r-reference)*pixel.a;alpha+=weight*pixel.a;
    }
    imageStore(horizontalImage,c,vec4(value,alpha,reference,0));
}
