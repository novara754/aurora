#version 450
#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec2 tex_coords;
layout (location = 1) out vec3 normal;

struct Vertex
{
	vec3 position;
	float tex_coord_x;
	vec3 normal;
	float tex_coord_y;
};

layout (buffer_reference, std430) readonly buffer VertexBuffer
{
	Vertex vertices[];
};

layout (push_constant) uniform PushConstants
{
	mat4 camera;
	VertexBuffer vertex_buffer;
} constants;

void main()
{
	Vertex vertex = constants.vertex_buffer.vertices[gl_VertexIndex];
	gl_Position = constants.camera * vec4(vertex.position, 1.0);
	tex_coords = vec2(vertex.tex_coord_x, vertex.tex_coord_y);
	normal = vertex.normal;
}
