// Book-flip transition: two pages hinged at the center open away from the
// viewer. Front pages show the outgoing video; once a page has flipped past
// edge-on it reveals the incoming video. Both faces share one uv mapping so
// the pages join seamlessly and progress = 1 ends on a clean fullscreen `to`.
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

    bool rightPage = (sx >= 0.0);
    float local = clamp(abs(sx) / halfSpan, 0.0, 1.0); // 0 = hinge, 1 = outer edge

    // Perspective: the outer edge of each page is closer to the viewer while
    // the book opens, so it is magnified.
    float depth = local * sinT;
    float w = 1.0 + depth * 0.55;
    float pu = clamp(local / w, 0.0, 1.0);
    float py = clamp((p.y - 0.5) / w + 0.5, 0.0, 1.0);

    float tx;
    if (rightPage)
        tx = 0.5 + 0.5 * pu;
    else
        tx = 0.5 - 0.5 * pu;

    vec4 col;
    if (cosT >= 0.0) {
        // front face: outgoing video split across the two pages
        col = texture2D(from, vec2(clamp(tx, 0.0, 1.0), py));
    } else {
        // Back face: incoming video. Same uv mapping as the front so the two
        // pages join seamlessly and progress = 1 ends on an exact fullscreen
        // copy of `to` (mirroring here displaced the halves and left a 0/1
        // seam at the spine).
        col = texture2D(to, vec2(clamp(tx, 0.0, 1.0), py));
    }
    gl_FragColor = col;
}