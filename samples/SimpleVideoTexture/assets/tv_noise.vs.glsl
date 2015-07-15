#version 330 core

uniform mat4 ciModelViewProjection;

in vec4 ciPosition;
in vec2 ciTexCoord0;

out VertexData
{
	vec2 texcoord0;
} vVertex;

void main()
{
	vVertex.texcoord0 = ciTexCoord0;
	gl_Position = ciModelViewProjection * ciPosition;
}
