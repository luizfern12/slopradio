// Segmented ("LED matrix") EQ for the video output, shown when the incoming
// deck is playing audio with no video.
//
// Same one-row input as eqbars.frag, but each bar is quantised into a stack of
// discrete segments with a gutter between them. That quantising is what makes
// it read as a hardware meter rather than a bar with lines drawn over it: a
// segment only lights once the level clears its midpoint, so the stack counts
// up in visible steps and the top of the graph is always on a segment
// boundary instead of somewhere inside one.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;

uniform sampler2D from;   // the kBands x 1 level row (unit 0)
uniform float uBands;     // number of columns
uniform float uGap;       // empty fraction of each column's width, 0..0.5
uniform float uPeakSize;  // peak marker thickness, in screen height

const float kSegs = 24.0;   // segments per column
const float kSegGap = 0.16; // empty fraction of one segment's height, 0..0.5

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
    float slot = v_texCoord.x * uBands;
    float band = floor(slot);
    float within = fract(slot);

    // Measured up from the baseline, so segment 0 sits on the bottom edge the
    // way the lowest LED on a meter does.
    float stack = (1.0 - v_texCoord.y) * kSegs;
    float seg = floor(stack);
    float segFrac = fract(stack);

    if (within < uGap * 0.5 || within > 1.0 - uGap * 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (segFrac < kSegGap * 0.5 || segFrac > 1.0 - kSegGap * 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 s = texture2D(from, vec2((band + 0.5) / uBands, 0.5));
    float level = s.r;
    float peak = s.g;

    if (seg + 0.5 < level * kSegs) {
        // Ramp across the lit stack rather than across the whole column, so a
        // quiet column still runs green to red over its own height — the same
        // "relative to this bar" reading eqbars.frag uses.
        float t = 1.0 - (seg + 0.5) / (level * kSegs);
        gl_FragColor = vec4(barColor(t), 1.0);
        return;
    }
    // The peak marker is the segment the level has fallen away from, held on
    // its own: one extra lit LED rather than a line across the stack.
    if (peak > 0.002 && seg + 0.5 < peak * kSegs) {
        gl_FragColor = vec4(0.85, 0.87, 0.90, 1.0);
        return;
    }
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
