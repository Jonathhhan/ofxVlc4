uniform sampler2D tex0;
uniform vec3 leftTint;
uniform vec3 rightTint;
uniform float eyeSeparation;
uniform float swapEyes;

varying vec2 vTexCoord;

void main() {
    float separation = clamp(eyeSeparation, -0.5, 0.5);
    bool swapped = swapEyes > 0.5;

    vec2 leftUv = vec2(vTexCoord.x * 0.5, vTexCoord.y);
    vec2 rightUv = vec2(0.5 + (vTexCoord.x * 0.5), vTexCoord.y);
    leftUv.x = clamp(leftUv.x + separation * 0.5, 0.0, 1.0);
    rightUv.x = clamp(rightUv.x - separation * 0.5, 0.0, 1.0);

    vec4 leftColor = texture2D(tex0, swapped ? rightUv : leftUv);
    vec4 rightColor = texture2D(tex0, swapped ? leftUv : rightUv);
    vec3 color = (leftColor.rgb * leftTint) + (rightColor.rgb * rightTint);
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), max(leftColor.a, rightColor.a));
}
