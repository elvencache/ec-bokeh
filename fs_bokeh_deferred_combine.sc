$input v_texcoord0

/*
* Copyright 2021 elven cache. All rights reserved.
* License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
*/

#include "../common/common.sh"
#include "parameters.sh"
#include "normal_encoding.sh"

SAMPLER2D(s_color,	0);
SAMPLER2D(s_normal,	1);
SAMPLER2D(s_depth,	2);

int ModHelper (float a, float b)
{
	return int( a - (b*floor(a/b)));
}

// from assao sample, cs_assao_prepare_depths.sc
vec3 NDCToViewspace( vec2 pos, float viewspaceDepth )
{
	vec3 ret;

	ret.xy = (u_ndcToViewMul * pos.xy + u_ndcToViewAdd) * viewspaceDepth;

	ret.z = viewspaceDepth;

	return ret;
}

void main()
{
	vec2 texCoord = v_texcoord0;

	vec4 normalRoughness = texture2D(s_normal, texCoord).xyzw;
	vec3 normal = NormalDecode(normalRoughness.xyz);
	float roughness = normalRoughness.w;

	// need to get a valid view vector for any microfacet stuff :(
	float gloss = 1.0-roughness;
	float specPower = 1022.0 * gloss + 2.0;

	// transform normal into view space
	mat4 worldToView = mat4(
		u_worldToView0,
		u_worldToView1,
		u_worldToView2,
		u_worldToView3
	);
	vec3 vsNormal = instMul(worldToView, vec4(normal, 0.0)).xyz;

	// read depth and recreate position
	float linearDepth = texture2D(s_depth, texCoord).x;
	vec3 viewSpacePosition = NDCToViewspace(texCoord, linearDepth);

	vec3 light = (u_lightPosition - viewSpacePosition);
	float lightDistSq = dot(light, light) + 1e-5;
	light = normalize(light);

	float NdotL = saturate(dot(vsNormal, light));
	float diffuse = NdotL * (1.0/lightDistSq);
	
	vec3 V = -normalize(viewSpacePosition);
	vec3 H = normalize(V+light);
	float NdotH = saturate(dot(vsNormal, H));
	float specular = 5.0 * pow(NdotH, specPower);

	float lightAmount = mix(diffuse, specular, 0.04);

	gl_FragColor = vec4(vec3_splat(lightAmount), 1.0);
}
