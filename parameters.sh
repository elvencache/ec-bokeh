/*
* Copyright 2021 elven cache. All rights reserved.
* License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
*/

#ifndef PARAMETERS_SH
#define PARAMETERS_SH

uniform vec4 u_params[13];

#define u_depthUnpackConsts			(u_params[0].xy)
#define u_frameIdx					(u_params[0].z)
#define u_ndcToViewMul				(u_params[1].xy)
#define u_ndcToViewAdd				(u_params[1].zw)
#define u_lightPosition				(u_params[2].xyz)

#define u_blurSteps					(u_params[3].x)
#define u_useSqrtDistribution		(u_params[3].y)
#define u_maxBlurSize				(u_params[4].x)
#define u_focusPoint				(u_params[4].y)
#define u_focusScale				(u_params[4].z)
#define u_radiusScale				(u_params[4].w)

#define u_worldToView0				(u_params[5])
#define u_worldToView1				(u_params[6])
#define u_worldToView2				(u_params[7])
#define u_worldToView3				(u_params[8])
#define u_viewToProj0				(u_params[9])
#define u_viewToProj1				(u_params[10])
#define u_viewToProj2				(u_params[11])
#define u_viewToProj3				(u_params[12])


#endif // PARAMETERS_SH
