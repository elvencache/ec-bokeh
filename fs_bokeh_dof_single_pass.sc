$input v_texcoord0

/*
* Copyright 2021 elven cache. All rights reserved.
* License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
*/

#include "../common/common.sh"
#include "parameters.sh"
#include "bokeh_dof.sh"

SAMPLER2D(s_color,			0);
SAMPLER2D(s_depth,			1);

void main()
{
	vec2 texCoord = v_texcoord0.xy;

	bool useSqrtDistribution = (0.0 < u_useSqrtDistribution);
	vec3 output = DepthOfField(s_color, s_depth, texCoord, u_focusPoint, u_focusScale, useSqrtDistribution).xyz;

	gl_FragColor = vec4(output, 1.0);
}
