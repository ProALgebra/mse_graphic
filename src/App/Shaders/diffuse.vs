#version 330 core

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTex;
layout(location = 3) in vec3 inMorphPos;
layout(location = 4) in vec3 inMorphNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;

uniform float morphFactor;

out vec3 fragPos;
out vec3 fragNormal;
out vec2 fragTex;

void main()
{
	vec3 pos = mix(inPos, inMorphPos, morphFactor);
	vec3 normal = normalize(mix(inNormal, inMorphNormal, morphFactor));

	vec4 worldPos = model * vec4(pos, 1.0);
	fragPos = worldPos.xyz;
	fragNormal = normalize(normalMatrix * normal);
	fragTex = inTex;

	gl_Position = projection * view * worldPos;
}
