#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    int channel;
    vec4 canvasRect;
} ubuf;
layout(binding = 1) uniform sampler2D source;
void main() {
    if (qt_TexCoord0.x < ubuf.canvasRect.x || qt_TexCoord0.y < ubuf.canvasRect.y ||
        qt_TexCoord0.x > ubuf.canvasRect.x + ubuf.canvasRect.z ||
        qt_TexCoord0.y > ubuf.canvasRect.y + ubuf.canvasRect.w) {
        fragColor = vec4(0.0);
        return;
    }
    vec4 value = texture(source, qt_TexCoord0);
    float component = ubuf.channel == 1 ? value.r : ubuf.channel == 2 ? value.g : ubuf.channel == 3 ? value.b : value.a;
    fragColor = ubuf.channel == 0 ? value * ubuf.qt_Opacity : vec4(vec3(component), ubuf.channel == 4 ? 1.0 : value.a) * ubuf.qt_Opacity;
}
