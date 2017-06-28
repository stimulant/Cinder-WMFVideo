#version 330 core

uniform float uTime;

in VertexData
{
	vec2 texcoord0;
} vVertex;

out vec4 oFragColor;

float rand( vec2 co )
{
    return fract( sin( dot( co.xy, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

void main()
{
	float n = rand( vVertex.texcoord0 * uTime );
	oFragColor = vec4( n, n, n, 1 );
}