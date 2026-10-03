// Continuous "mountain" EQ for the video output, shown when the incoming deck
// is playing audio with no video.
//
// Same one-row input as eqbars.frag — a kBands x 1 RGBA image with the bar
// height in R and the peak-hold height in G — but the slots are stitched
// together: each column takes the straight line between the two bands it lies
// between, so the graph comes out as one silhouette instead of a comb of
// separate bars.
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

void main()
{
    // The two bands this column lies between, and how far across the step it
    // sits. The right edge clamps rather than wraps: mod() would reach back to
    // band 0 for the last columns and drop a cliff at x = 1, which is exactly
    // the discontinuity the silhouette is meant not to have.
    float slot = v_texCoord.x * uBands;
    float i0 = clamp(floor(slot), 0.0, uBands - 1.0);
    float i1 = min(i0 + 1.0, uBands - 1.0);
    float f = fract(slot);

    vec4 s0 = texture2D(from, vec2((i0 + 0.5) / uBands, 0.5));
    vec4 s1 = texture2D(from, vec2((i1 + 0.5) / uBands, 0.5));

    float level = mix(s0.r, s1.r, f);
    float peak = mix(s0.g, s1.g, f);

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
