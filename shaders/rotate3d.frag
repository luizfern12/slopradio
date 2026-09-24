// Rotate 3D around the horizontal axis (tumble): the frame tips forward and
// flips vertically, the perspective making the near edge bulge.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D from;
uniform sampler2D to;
uniform float progress;

void main()
{
    float theta = progress * 3.14159265358979323846;
    float cosT = cos(theta);
    float sinT = sin(theta);
    float absCos = max(abs(cosT), 0.001);

    vec2 p = v_texCoord;
    float sy = p.y - 0.5;
    float halfSpan = 0.5 * absCos;

    if (abs(sy) > halfSpan + 0.002) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float u = clamp(sy / halfSpan, -1.0, 1.0);
    float depth = u * sinT;
    float w = 1.0 + depth * 0.6;
    float pu = clamp(u / w, -1.0, 1.0);
    float py = clamp(pu * 0.5 + 0.5, 0.0, 1.0);
    float px = clamp((p.x - 0.5) / w + 0.5, 0.0, 1.0);

    vec4 col;
    if (cosT >= 0.0)
        col = texture2D(from, vec2(px, py));
    else
        col = texture2D(to, vec2(px, 1.0 - py));
    gl_FragColor = col;
}