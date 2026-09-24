// Cube spin (vertical axis): a single flat panel hinged at the screen center
// rotates 180 degrees around the Y axis — like a revolving sign. The front
// shows the outgoing video, the back the incoming one (mirrored).
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
    float sx = p.x - 0.5;
    float halfSpan = 0.5 * absCos;

    if (abs(sx) > halfSpan + 0.002) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float u = clamp(sx / halfSpan, -1.0, 1.0);
    // near half is magnified by perspective
    float depth = u * sinT;
    float w = 1.0 + depth * 0.6;
    float pu = clamp(u / w, -1.0, 1.0);
    float py = clamp((p.y - 0.5) / w + 0.5, 0.0, 1.0);

    vec2 tc = vec2(clamp(pu * 0.5 + 0.5, 0.0, 1.0), py);

    vec4 col;
    if (cosT >= 0.0)
        col = texture2D(from, tc);
    else
        col = texture2D(to, vec2(1.0 - tc.x, tc.y));
    gl_FragColor = col;
}