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
	VertexBuffer vertex_buffer;
} constants;

vec2 positions[3] = vec2[](
	vec2(0.0, -0.5),
	vec2(0.5, 0.5),
	vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0)
);

void main()
{
	Vertex vertex = constants.vertex_buffer.vertices[gl_VertexIndex];
	gl_Position = vec4(vertex.position, 1.0);
	color = colors[gl_VertexIndex];
}
