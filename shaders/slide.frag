// The incoming video slides in from the right, pushing the outgoing one out.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D from;
uniform sampler2D to;
uniform float progress;

void main()
{
    float t = clamp(progress, 0.0, 1.0);
    float edge = 1.0 - t;

    if (v_texCoord.x >= edge) {
        // inside the incoming corner: sample it compressed toward the right edge
        float k = edge > 0.001 ? t : 1.0;
        vec2 uv = vec2(edge + (v_texCoord.x - edge) / k, v_texCoord.y);
        gl_FragColor = texture2D(to, uv);
    } else {
        gl_FragColor = texture2D(from, v_texCoord);
    }
}