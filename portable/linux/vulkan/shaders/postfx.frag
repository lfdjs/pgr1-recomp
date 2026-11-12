#version 450
layout(set=0,binding=0) uniform sampler2D srcImage;
layout(push_constant) uniform Options {
    vec4 rect;       // x,y,w,h in physical output pixels
    vec2 invSource;  // 1/w, 1/h of source texture
    float sharpen;  // [0,1]
    float fxaa;     // 0/1
} pc;
layout(location=0) out vec4 color;
float luminance(vec3 c) { return dot(c,vec3(.299,.587,.114)); }
vec3 at(vec2 uv) { return texture(srcImage,clamp(uv,vec2(0),vec2(1))).rgb; }
vec3 antialiased(vec2 uv) {
    vec3 center=at(uv);
    if(pc.fxaa<.5)return center;
    vec3 nw=at(uv+pc.invSource*vec2(-1,-1));
    vec3 ne=at(uv+pc.invSource*vec2(1,-1));
    vec3 sw=at(uv+pc.invSource*vec2(-1,1));
    vec3 se=at(uv+pc.invSource*vec2(1,1));
    float a=luminance(nw),b=luminance(ne),c=luminance(sw),d=luminance(se);
    float m=luminance(center);
    float low=min(m,min(min(a,b),min(c,d)));
    float high=max(m,max(max(a,b),max(c,d)));
    if(high-low < max(.0312,high*.125))return center;
    vec2 dir=vec2(-(a+b-c-d),a+c-b-d);
    float reduction=max((a+b+c+d)*(.25*.0312),.0078125);
    dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduction),vec2(-8),vec2(8))*pc.invSource;
    vec3 first=.5*(at(uv+dir*(1.0/3.0-.5))+at(uv+dir*(2.0/3.0-.5)));
    vec3 second=first*.5+.25*(at(uv+dir*(-.5))+at(uv+dir*(.5)));
    float l=luminance(second);
    return (l<low || l>high)?first:second;
}
void main() {
    vec2 uv=(gl_FragCoord.xy-pc.rect.xy)/pc.rect.zw;
    if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) {
        color=vec4(0,0,0,1);return;
    }
    vec3 result=antialiased(uv);
    if(pc.sharpen>.001) {
        // Apply unsharp masking to the antialiased signal, including neighbors.
        // Clamping to its local range avoids ringing and new bright/dark halos.
        vec3 n=antialiased(uv+vec2(0,-pc.invSource.y));
        vec3 s=antialiased(uv+vec2(0,pc.invSource.y));
        vec3 e=antialiased(uv+vec2(pc.invSource.x,0));
        vec3 w=antialiased(uv+vec2(-pc.invSource.x,0));
        vec3 low=min(result,min(min(n,s),min(e,w)));
        vec3 high=max(result,max(max(n,s),max(e,w)));
        result=clamp(result+(result-(n+s+e+w)*.25)*(.25*pc.sharpen),low,high);
    }
    color=vec4(result,1);
}
