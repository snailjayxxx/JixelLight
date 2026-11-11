// Scene transforms mirror the CPU reference's ordered FP32 operations.
float dnDot(vec3 a,vec3 b) { precise float value=(a.x*b.x+a.y*b.y)+a.z*b.z;return value; }
vec3 dnRows(vec3 a,vec3 b,vec3 c,vec3 v) {return vec3(dnDot(a,v),dnDot(b,v),dnDot(c,v));}
float dnRoot(float v) {
    float magnitude=abs(v);if(magnitude==0.0)return 0.0;
    precise float r=pow(magnitude,1.0/3.0);r=(2.0*r+magnitude/(r*r))/3.0;r=(2.0*r+magnitude/(r*r))/3.0;return sign(v)*r;
}
float dnDecode(float v) {v=max(v,0.0);return v<=.04045?v/12.92:pow((v+.055)/1.055,2.4);}
vec3 dnInput(vec3 v) {
    if(p[5].y==0.0)v=dnRows(p[25].xyz,p[26].xyz,p[27].xyz,vec3(dnDecode(v.r),dnDecode(v.g),dnDecode(v.b)));
    return v;
}
vec3 dnToSrgb(vec3 v) {
    v=dnRows(vec3(.7976749,.1351917,.0313534),vec3(.2880402,.7118741,.0000857),vec3(0,0,.8252100),v);
    v=dnRows(vec3(.9555766,-.0230393,.0631636),vec3(-.0282895,1.0099416,.0210077),vec3(.0122982,-.0204830,1.3299098),v);
    return dnRows(vec3(3.2404542,-1.5371385,-.4985314),vec3(-.9692660,1.8760108,.0415560),vec3(.0556434,-.2040259,1.0572252),v);
}
vec3 dnToPro(vec3 v) {
    v=dnRows(vec3(.4124564,.3575761,.1804375),vec3(.2126729,.7151522,.0721750),vec3(.0193339,.1191920,.9503041),v);
    v=dnRows(vec3(1.0478112,.0228866,-.0501270),vec3(.0295424,.9904844,-.0170491),vec3(-.0092345,.0150436,.7521316),v);
    return dnRows(vec3(1.3459433,-.2556075,-.0511118),vec3(-.5445989,1.5081673,.0205351),vec3(0,0,1.2118128),v);
}
vec3 dnToLab(vec3 v) {
    v=dnRows(vec3(.4122214708,.5363325363,.0514459929),vec3(.2119034982,.6806995451,.1073969566),vec3(.0883024619,.2817188376,.6299787005),v);
    v=vec3(dnRoot(v.x),dnRoot(v.y),dnRoot(v.z));
    return dnRows(vec3(.2104542553,.7936177850,-.0040720468),vec3(1.9779984951,-2.4285922050,.4505937099),vec3(.0259040371,.7827717662,-.8086757660),v);
}
vec3 dnFromLab(vec3 v) {
    v=dnRows(vec3(1,.3963377774,.2158037573),vec3(1,-.1055613458,-.0638541728),vec3(1,-.0894841775,-1.2914855480),v);
    v=v*v*v;
    return dnRows(vec3(4.0767416621,-3.3077115913,.2309699292),vec3(-1.2684380046,2.6097574011,-.3413193965),vec3(-.0041960863,-.7034186147,1.7076147010),v);
}
