#version 330 core

uniform sampler2DRect uSampler;
uniform vec2 uVideoSize;

in VertexData
{
	vec2 texcoord0;
} vVertex;

out vec4 oFragColor;

void main()
{
	oFragColor = texture( uSampler, vVertex.texcoord0 * uVideoSize );
	// oFragColor = vec4( 1.0, 0.0, 0.0, 1.0 );
}
