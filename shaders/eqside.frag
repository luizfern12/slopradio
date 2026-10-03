// Horizontal ("side") EQ for the video output, shown when the incoming deck is
// playing audio with no video.
//
// The same one-row input as eqbars.frag, rotated a quarter turn: frequency
// runs up the screen instead of across it, each band owns a horizontal slot,
// and the level extends from the left edge — the classic side-mounted
// analyzer, and a different silhouette without asking the analyzer for any
// new data.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;

uniform sampler2D from;   // the kBands x 1 level row (unit 0)
uniform float uBands;     // number of horizontal slots
uniform float uGap;       // empty fraction of each slot's height, 0..0.5
uniform float uPeakSize;  // peak marker thickness, measured along x here

// Green at the base, through yellow, to red at the tip: same ramp as the bar
// graph so the modes read as one instrument.
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
    // v_texCoord.y is 0 at the top, so flipping it puts band 0 on the bottom
    // edge and leaves the high frequencies at the top: read bottom to top, the
    // way eqbars.frag reads left to right.
    float slot = (1.0 - v_texCoord.y) * uBands;
    float band = floor(slot);
    float within = fract(slot);

    if (within < uGap * 0.5 || within > 1.0 - uGap * 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 s = texture2D(from, vec2((band + 0.5) / uBands, 0.5));
    float level = s.r;
    float peak = s.g;

    float x = v_texCoord.x;

    // Grows rightward from the left edge. The level guard is what keeps a
    // silent graph black: without it x <= level is true at x = 0 even when the
    // band has nothing, and the left edge would stay lit through silence.
    if (level > 0.001 && x <= level) {
        // 0 at the foot of the bar, 1 at its tip, matching eqbars.frag.
        gl_FragColor = vec4(barColor(x / level), 1.0);
        return;
    }
    if (peak > 0.002 && x >= peak && x < peak + uPeakSize) {
        gl_FragColor = vec4(0.85, 0.87, 0.90, 1.0);
        return;
    }
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
