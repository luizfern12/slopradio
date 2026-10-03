// Mirrored bar-graph EQ for the video output, shown when the incoming deck is
// playing audio with no video.
//
// eqbars with its reference line moved off the floor and onto the middle of
// the screen. The slots, the gutter, the levels, the peak markers and the
// colour ramp are all eqbars'; the only change is that a bar grows level/2
// upward and level/2 downward from the centre instead of `level` upward from
// the bottom. Each column therefore covers exactly the rows it covered before,
// and the output mirrors about the horizontal mid-line by construction rather
// than by drawing the shape twice.
//
// The CPU side is untouched: one kBands x 1 row, ~256 bytes, bar height in red
// and peak-hold height in green.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;

uniform sampler2D from;   // the kBands x 1 level row (unit 0)
uniform float uBands;     // number of bars
uniform float uGap;       // empty fraction of each bar's slot, 0..0.5
uniform float uPeakSize;  // peak marker thickness, in screen height

// Green at the base, through yellow, to red at the tip: eqbars' ramp, read
// from the middle of the screen outward — t = 0 where the two arms meet, t = 1
// at the outer tip. Each arm is then a mirror image of the other, which is the
// whole point of the mode, and a short bar still spans the full ramp exactly
// as a short bar does in eqbars.
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

    // Twice the distance from the middle of the screen, so it runs 0 on the
    // centre line and 1 at either edge. eqbars works out the same quantity
    // from the floor as `1 - y`; moving the origin is the entire edit, and it
    // is what makes the two halves agree without a second pass — y and 1 - y
    // produce the same number, so the comparison cannot pick one side over the
    // other.
    float d = abs(v_texCoord.y - 0.5) * 2.0;

    if (d <= level) {
        // 0 at the centre — the base both arms grow from — 1 at the tip.
        float t = level > 0.001 ? d / level : 0.0;
        gl_FragColor = vec4(barColor(t), 1.0);
    } else if (peak > 0.002 && d <= peak && d > peak - uPeakSize * 2.0) {
        // Both arms carry one, sitting between the held peak and the current
        // tip exactly as eqbars' sits between them — the same branch, on the
        // other side of the same line. d doubles screen height, hence the 2.
        gl_FragColor = vec4(0.85, 0.87, 0.90, 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
