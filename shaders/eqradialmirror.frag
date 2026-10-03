// Radial mirror EQ for the video output, shown when the incoming deck is
// playing audio with no video.
//
// Same row and same spoke geometry as eqcircle.frag, but a different anchor.
// eqcircle hangs every spoke off the hub and grows it outward, so the inner
// edge of the graph is a fixed circle and only the outer one moves. Here the
// spoke is centred on a ring in the middle of the output and both tips move
// with the level, which reads as an iris breathing rather than a fan opening.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;

uniform sampler2D from;   // the kBands x 1 level row (unit 0)
uniform float uBands;     // number of spokes
uniform float uGap;       // empty fraction of each spoke's slot, 0..0.5
uniform float uPeakSize;  // peak marker thickness, in screen height
uniform float uRatio;     // widget width / height, so spokes stay even

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
    // Correct for aspect ratio first, otherwise the ring comes out as an
    // ellipse on a 16:9 output and the spokes bunch up on the top and bottom.
    vec2 pos = v_texCoord - vec2(0.5);
    pos.x *= uRatio;

    float r = length(pos);
    float angle = atan(pos.y, pos.x) + 3.14159265359;   // 0..2*PI

    // Half a slot of offset puts a spoke on every screen axis instead of a
    // gap, and mod() wraps the seam atan returns exactly 2*PI at back to band
    // 0 — both straight out of eqcircle.frag, for the same reasons.
    float slot = (angle / 6.28318530718) * uBands + 0.5;
    float band = mod(floor(slot), uBands);
    float within = fract(slot);

    if (within < uGap * 0.5 || within > 1.0 - uGap * 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 s = texture2D(from, vec2((band + 0.5) / uBands, 0.5));
    float level = s.r;
    float peak = s.g;

    // rMid +- span reaches exactly the hub and the outer radius at full level,
    // so the hub stays clear by construction rather than by a test.
    const float hub = 0.10;
    const float maxR = 0.42;
    const float rMid = (hub + maxR) * 0.5;
    const float span = (maxR - hub) * 0.5;

    float reach = span * level;
    float peakReach = span * peak;

    // The level guard keeps a silent graph black: at level 0 the spoke
    // collapses onto rMid and abs(r - rMid) <= 0 would otherwise paint a
    // one-texel ring for free.
    if (level > 0.001 && abs(r - rMid) <= reach) {
        gl_FragColor = vec4(barColor(level), 1.0);
        return;
    }
    // Peak markers ride both tips at once — inside and outside the mid ring —
    // which is what keeps a decaying bar from swallowing them.
    if (peak > 0.002 && abs(abs(r - rMid) - peakReach) < uPeakSize) {
        gl_FragColor = vec4(barColor(peak), 1.0);
        return;
    }
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
