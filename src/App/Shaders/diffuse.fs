#version 330 core

struct DirectionalLight {
	vec3 direction;
	vec3 color;
	float intensity;
};

struct SpotLight {
	vec3 position;
	vec3 direction;
	vec3 color;
	float intensity;
	float cutoff;
	float outerCutoff;
};

uniform sampler2D tex_2d;

uniform vec3 viewPos;
uniform float ambientStrength;
uniform float specularStrength;
uniform float shininess;

uniform DirectionalLight dirLight;
uniform SpotLight spotLight;

in vec3 fragPos;
in vec3 fragNormal;
in vec2 fragTex;

out vec4 out_col;

vec3 applyDirectionalLight(vec3 baseColor, vec3 normal, vec3 viewDir)
{
	vec3 lightDir = normalize(-dirLight.direction);
	float diff = max(dot(normal, lightDir), 0.0);

	vec3 reflectDir = reflect(-lightDir, normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

	vec3 ambient = ambientStrength * baseColor;
	vec3 diffuse = diff * baseColor;
	vec3 specular = specularStrength * spec * vec3(1.0);

	return (ambient + diffuse + specular) * dirLight.color * dirLight.intensity;
}

vec3 applySpotLight(vec3 baseColor, vec3 normal, vec3 viewDir)
{
	vec3 lightDir = normalize(spotLight.position - fragPos);
	float theta = dot(lightDir, normalize(-spotLight.direction));

	float epsilon = spotLight.cutoff - spotLight.outerCutoff;
	float intensity = clamp((theta - spotLight.outerCutoff) / epsilon, 0.0, 1.0);

	if (theta < spotLight.outerCutoff)
	{
		intensity = 0.0;
	}

	float diff = max(dot(normal, lightDir), 0.0);
	vec3 reflectDir = reflect(-lightDir, normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

	vec3 ambient = ambientStrength * baseColor;
	vec3 diffuse = diff * baseColor;
	vec3 specular = specularStrength * spec * vec3(1.0);

	return (ambient + diffuse + specular) * spotLight.color * spotLight.intensity
		* intensity;
}

void main()
{
	vec3 baseColor = texture(tex_2d, fragTex).rgb;
	if (length(baseColor) < 0.001)
	{
		baseColor = vec3(1.0);
	}

	vec3 normal = normalize(fragNormal);
	vec3 viewDir = normalize(viewPos - fragPos);

	vec3 color = vec3(0.0);
	color += applyDirectionalLight(baseColor, normal, viewDir);
	color += applySpotLight(baseColor, normal, viewDir);

	out_col = vec4(color, 1.0);
}
