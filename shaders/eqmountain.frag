// Continuous "mountain" EQ for the video output, shown when the incoming deck
// is playing audio with no video.
//
// Same one-row input as eqbars.frag — a kBands x 1 RGBA image with the bar
// height in R and the peak-hold height in G — but the slots are stitched
// together: each column takes a curve through its neighbouring bands instead
// of the straight line between two of them, so the graph comes out as one
// silhouette without a corner at every slot boundary.
//
// There is deliberately no uGap here. A gutter between columns would cut the
// silhouette back into pieces, and "no gutter" is the entire difference
// between this and eqbars.frag.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;

uniform sampler2D from;   // the kBands x 1 level row (unit 0)
uniform float uBands;     // number of bands
uniform float uPeakSize;  // peak marker thickness, in screen height

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

// One band's sample, clamped instead of wrapped. mod() would reach back to
// band 0 past the end of the row and drop a cliff at x = 1, which is exactly
// the discontinuity a silhouette is meant not to have.
vec4 bandAt(float k)
{
    return texture2D(from, vec2((clamp(k, 0.0, uBands - 1.0) + 0.5) / uBands, 0.5));
}

// Uniform Catmull-Rom: the cubic joining p1 to p2, shaped by the sample on
// either side of them. It passes exactly through p1 and p2 — so every band's
// reported height stays true — and leaves p2 with the same slope the next
// segment starts with, so consecutive slots meet in a curve rather than a
// corner. That corner, once per slot, is what makes a straight-line join read
// as polygonal no matter how many columns are drawn.
float curve(float p0, float p1, float p2, float p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5 * ((2.0 * p1)
                  + (-p0 + p2) * t
                  + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                  + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

void main()
{
    // Band-space position, with each band's value sitting in the middle of its
    // own slot — the same place eqbars.frag puts it, so switching modes doesn't
    // slide the spectrum sideways. Both ends are floored through bandAt(), so
    // the fetches below are in uniform control flow whatever pos turns out to
    // be; only the arithmetic is branched.
    float pos = v_texCoord.x * uBands - 0.5;
    float i = floor(pos);
    float f = pos - i;

    vec4 sPrev = bandAt(i - 1.0);
    vec4 s0 = bandAt(i);
    vec4 s1 = bandAt(i + 1.0);
    vec4 s2 = bandAt(i + 2.0);

    // Half a slot of margin past either end, where there is no band on the far
    // side to lean on: hold the nearest band's height. That also pins the
    // silhouette to both screen edges at the level the row actually reports,
    // instead of tapering off past the last sample.
    bool margin = pos < 0.0 || pos >= uBands - 1.0;
    vec2 levelPeak = margin
        ? s0.rg
        : vec2(curve(sPrev.r, s0.r, s1.r, s2.r, f),
               curve(sPrev.g, s0.g, s1.g, s2.g, f));

    // A cubic can overshoot: a sharp fall pulls the curve below zero in the
    // valley, and a level below zero is a tip above the baseline — a hole
    // punched through the silhouette. Clamping keeps the shape solid and
    // inside the screen.
    float level = clamp(levelPeak.x, 0.0, 1.0);
    float peak = clamp(levelPeak.y, 0.0, 1.0);

    // v_texCoord.y runs 0 at the top of the screen to 1 at the bottom, so the
    // silhouette stands on v = 1 exactly like the bars do.
    float y = v_texCoord.y;
    float tip = 1.0 - level;

    if (y >= tip) {
        // 0 at the tip of the silhouette, 1 at its foot.
        float t = level > 0.001 ? (y - tip) / level : 0.0;
        gl_FragColor = vec4(barColor(t), 1.0);
    } else if (peak > 0.002 && y >= 1.0 - peak && y < 1.0 - peak + uPeakSize) {
        gl_FragColor = vec4(0.85, 0.87, 0.90, 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
