#version 330 core

in vec2 v_tex;

out vec4 out_col;

uniform vec2 u_center;
uniform float u_scale;
uniform int u_maxIter;
uniform float u_aspect;
uniform float u_intensity;
uniform float u_colorShift;

void main() {
	float x = (v_tex.x - 0.5) * u_scale;
	float y = (v_tex.y - 0.5) * u_scale / u_aspect;

	float cx = u_center.x + x;
	float cy = u_center.y + y;

	float zx = 0.0;
	float zy = 0.0;
	float threshold2 = 4.0;

	int i;
	for (i = 0; i < u_maxIter && zx * zx + zy * zy <= threshold2; ++i) {
		float xTemp = zx * zx - zy * zy + cx;
		zy = 2.0 * zx * zy + cy;
		zx = xTemp;
	}
	if (i < u_maxIter) {
		out_col = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	float angle = u_colorShift;
	vec3 color = vec3(
		0.5 + 0.5 * cos(angle),
		0.5 + 0.5 * cos(angle + 2.0),
		0.5 + 0.5 * cos(angle + 4.0)
	);

	color *= u_intensity;
	color = clamp(color, 0.0, 1.0);

	out_col = vec4(color, 1.0);
}
