#version 450
layout(location=0) in vec4 inPosition;  // x,y,z,reciprocal-W in game pixels
layout(location=1) in vec4 inColor;
layout(location=0) out vec4 fragColor;
layout(push_constant) uniform Viewport { vec2 size; vec2 offset; } viewport;
void main() {
    float invW = max(inPosition.w, 0.00001);
    vec2 xy = (inPosition.xy + vec2(.5) - viewport.offset) / viewport.size;
    // Vulkan framebuffer NDC: the clip Y axis is downward from the top of the viewport.
    // (A positive viewport height maps -1 to the top and +1 to the bottom.)
    gl_Position = vec4((xy*2.0-1.0)/invW, inPosition.z/invW, 1.0/invW);
    fragColor=inColor;
}
