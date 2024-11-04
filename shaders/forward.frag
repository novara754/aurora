#version 450

layout (location = 0) in vec2 tex_coords;
layout (location = 1) in vec3 normal;
layout (location = 0) out vec4 frag_color;

layout (set = 0, binding = 0) uniform sampler2D diffuse_sampler;

void main()
{
	frag_color = texture(diffuse_sampler, tex_coords);
	// frag_color = vec4(tex_coords, 0.0, 1.0);
	// frag_color = vec4(normal, 1.0);
}
