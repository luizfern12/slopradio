// Radial (circular) EQ for the video output, shown when the incoming deck is
// playing audio with no video.
//
// Same one-row input as eqbars.frag — a kBands x 1 RGBA image with bar height
// in R and the peak-hold height in G — but the bars are fanned around the
// centre of the screen instead of standing on its bottom edge.
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
// graph so the two modes read as one instrument.
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
    // Both axes end up scaled by widget height, so shader radius maps straight
    // onto pixels without a further factor.
    vec2 pos = v_texCoord - vec2(0.5);
    pos.x *= uRatio;

    float r = length(pos);
    float angle = atan(pos.y, pos.x) + 3.14159265359;   // 0..2*PI

    // Half a slot of offset puts a spoke on every screen axis instead of a
    // gap: without it the seams fall on 0/90/180/270 degrees and those four
    // directions come out blank.
    float slot = (angle / 6.28318530718) * uBands + 0.5;

    // atan hands back exactly 2*PI along the negative-x half of the centre
    // line, so the slot reaches bands + 0.5 and floor() lands on bands. mod()
    // wraps it back to band 0, which is where that seam belongs, rather than
    // clamping to a one-texel-thin strip at the end of the row.
    float band = mod(floor(slot), uBands);
    float within = fract(slot);

    if (within < uGap * 0.5 || within > 1.0 - uGap * 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 s = texture2D(from, vec2((band + 0.5) / uBands, 0.5));
    float level = s.r;
    float peak = s.g;

    // Spokes grow outward from a clear hub. The peak marker rides at its own
    // radius rather than butting against the tip, so a decaying bar never
    // swallows it.
    const float hub = 0.10;
    const float maxR = 0.42;
    float tip = hub + (maxR - hub) * level;
    float peakR = hub + (maxR - hub) * peak;

    if (r >= hub && r < tip) {
        gl_FragColor = vec4(barColor(level), 1.0);
        return;
    }
    if (peak > 0.0 && abs(r - peakR) < uPeakSize) {
        gl_FragColor = vec4(barColor(peak), 1.0);
        return;
    }
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
