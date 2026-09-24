// Default fullscreen vertex shader.
// Custom .vert shaders placed next to a .frag in the shaders folder use the
// same interface: attributes `vertex` (vec2, NDC) and `texCoord` (vec2), and
// must write the interpolated coordinate to `v_texCoord`.
attribute vec2 vertex;
attribute vec2 texCoord;
varying vec2 v_texCoord;

void main()
{
    v_texCoord = texCoord;
    gl_Position = vec4(vertex, 0.0, 1.0);
}