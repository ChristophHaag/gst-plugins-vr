#version 330

in vec3 out_color;

void main()
{
  gl_FragColor = vec4 (out_color, 1);
}
