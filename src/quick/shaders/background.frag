#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float useArtwork;
    vec2 viewportSize;
    vec2 artworkSize;
    vec4 atmosphereTop;
    vec4 atmosphereBottom;
    vec4 glowA;
    vec4 glowB;
    vec4 glowC;
    vec4 artworkBase;
    vec4 shadeTop;
    vec4 shadeBottom;
};
layout(binding = 1) uniform sampler2D source;

float glow(vec2 uv, vec2 center, vec2 spread)
{
    vec2 p = (uv - center) / spread;
    return exp(-2.0 * dot(p, p));
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float lowerFade = smoothstep(0.12, 1.0, uv.y);
    vec3 color = mix(atmosphereTop.rgb, atmosphereBottom.rgb, lowerFade);
    vec3 light = glowA.rgb * glow(uv, vec2(0.18, 0.20), vec2(0.65, 0.70));
    light += glowB.rgb * glow(uv, vec2(0.77, 0.22), vec2(0.80, 0.80));
    light += glowC.rgb * glow(uv, vec2(1.0, 0.50), vec2(0.70, 0.65));
    color += light * (1.0 - lowerFade);

    if (useArtwork > 0.5) {
        // PreserveAspectCrop, including non-square covers and window resizing.
        float viewAspect = viewportSize.x / max(1.0, viewportSize.y);
        float coverAspect = artworkSize.x / max(1.0, artworkSize.y);
        vec2 crop = vec2(min(1.0, viewAspect / coverAspect), min(1.0, coverAspect / viewAspect));
        vec4 cover = texture(source, (uv - 0.5) * crop + 0.5);
        vec3 base = artworkBase.rgb;
        vec3 rgb = cover.rgb + base * (1.0 - cover.a);
        float luminance = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb = mix(vec3(luminance), rgb, 0.7);
        color = mix(base, rgb * 0.65, 0.65);
        float shadeAlpha = mix(21.0, 237.0, uv.y) / 255.0;
        vec3 shade = mix(shadeTop.rgb, shadeBottom.rgb, uv.y);
        color = mix(color, shade, shadeAlpha);
    }

    // Interleaved gradient noise, fixed to physical output pixels. One LSB peak
    // to peak, monochrome and zero centered, after all darkening/compositing.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
    fragColor = vec4(clamp(color + noise / 255.0, 0.0, 1.0), 1.0) * qt_Opacity;
}
