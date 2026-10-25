#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 direction;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    // Sample every texel: sparse taps leave visible steps on high-contrast art.
    for (int i = -48; i <= 48; ++i) {
        float weight = exp(-float(i * i) / 512.0);
        color += texture(source, clamp(qt_TexCoord0 + float(i) * direction,
                                       vec2(0.0), vec2(1.0))) * weight;
        totalWeight += weight;
    }
    fragColor = (color / totalWeight) * qt_Opacity;
}
