// Crossfade / dissolve between the two decks.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D from;
uniform sampler2D to;
uniform float progress;

void main()
{
    vec4 a = texture2D(from, v_texCoord);
    vec4 b = texture2D(to, v_texCoord);
    gl_FragColor = mix(a, b, clamp(progress, 0.0, 1.0));
}