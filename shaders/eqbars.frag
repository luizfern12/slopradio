// Classic bar-graph EQ for the video output, shown when the incoming deck is
// playing audio with no video.
//
// The CPU only produces one tiny row per frame: a kBands x 1 RGBA image with
// the bar height in red and the peak-hold height in green. Everything visual
// happens here, so the per-frame upload is ~256 bytes and the bars, the gaps
// between them and the colour ramp are all free.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;

uniform sampler2D from;   // the kBands x 1 level row (unit 0)
uniform float uBands;     // number of bars
uniform float uGap;       // empty fraction of each bar's slot, 0..0.5
uniform float uPeakSize;  // peak marker thickness, in screen height

// Green at the base, through yellow, to red at the tip: the usual equalizer
// look, and it reads as "getting loud" without needing a legend.
vec3 barColor(float t)
{
    vec3 low  = vec3(0.05, 0.70, 0.20);
    vec3 mid  = vec3(0.95, 0.88, 0.10);
    vec3 high = vec3(1.00, 0.18, 0.08);
    return t < 0.5 ? mix(low, mid, t * 2.0)
                   : mix(mid, high, (t - 0.5) * 2.0);
}

void main()
{
    // Which bar this pixel belongs to, and where it sits inside that bar's
    // slot. Nearest filtering on a kBands-wide texture makes this exact.
    float slot = v_texCoord.x * uBands;
    float band = floor(slot);
    float within = fract(slot);

    if (within < uGap * 0.5 || within > 1.0 - uGap * 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 s = texture2D(from, vec2((band + 0.5) / uBands, 0.5));
    float level = s.r;
    float peak = s.g;

    // v_texCoord.y runs 0 at the top of the screen to 1 at the bottom, so bars
    // grow up from v = 1.
    float y = v_texCoord.y;
    float tip = 1.0 - level;

    if (y >= tip) {
        // 0 at the base of the bar, 1 at its tip.
        float t = level > 0.001 ? (y - tip) / level : 0.0;
        gl_FragColor = vec4(barColor(t), 1.0);
    } else if (peak > 0.002 && y >= 1.0 - peak && y < 1.0 - peak + uPeakSize) {
        gl_FragColor = vec4(0.85, 0.87, 0.90, 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}