#version 450
#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec3 color;

struct Vertex
{
	vec3 position;
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
	color = vec3(1.0);
}
