/*
* Copyright 2021 elven cache. All rights reserved.
* License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
*/

/*
* ...
*/


#include <common.h>
#include <camera.h>
#include <bgfx_utils.h>
#include <imgui/imgui.h>
#include <bx/rng.h>
#include <bx/os.h>


namespace {

#define FRAMEBUFFER_RT_COLOR		0
#define FRAMEBUFFER_RT_DEPTH		1
#define FRAMEBUFFER_RENDER_TARGETS	2

#define MODEL_COUNT					100

enum Meshes
{
	MeshSphere = 0,
	MeshCube,
	MeshTree,
	MeshHollowCube,
	MeshBunny
};

static const char * s_meshPaths[] =
{
	"meshes/unit_sphere.bin",
	"meshes/cube.bin",
	"meshes/tree.bin",
	"meshes/hollowcube.bin",
	"meshes/bunny.bin"
};

static const float s_meshScale[] =
{
	0.15f,
	0.05f,
	0.15f,
	0.25f,
	0.25f
};

// Vertex decl for our screen space quad (used in deferred rendering)
struct PosTexCoord0Vertex
{
	float m_x;
	float m_y;
	float m_z;
	float m_u;
	float m_v;

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	}

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosTexCoord0Vertex::ms_layout;

struct Uniforms
{
	enum { NumVec4 = 13 };

	void init() {
		u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, NumVec4);
	};

	void submit() const {
		bgfx::setUniform(u_params, m_params, NumVec4);
	}

	void destroy() {
		bgfx::destroy(u_params);
	}

	union
	{
		struct
		{
			/* 0    */ struct { float m_depthUnpackConsts[2]; float m_frameIdx; float m_unused0; };
			/* 1    */ struct { float m_ndcToViewMul[2]; float m_ndcToViewAdd[2]; };
			/* 2    */ struct { float m_lightPosition[3]; float m_unused2; };
			/* 3    */ struct { float m_blurSteps; float m_useSqrtDistribution; float m_unused3[2]; };
			/* 4    */ struct { float m_maxBlurSize; float m_focusPoint; float m_focusScale; float m_radiusScale; };
			/* 5-8  */ struct { float m_worldToView[16]; }; // built-in u_view will be transform for quad during screen passes
			/* 9-12 */ struct { float m_viewToProj[16]; };	 // built-in u_proj will be transform for quad during screen passes
		};

		float m_params[NumVec4 * 4];
	};

	bgfx::UniformHandle u_params;
};

struct RenderTarget
{
	void init(uint32_t _width, uint32_t _height, bgfx::TextureFormat::Enum _format, uint64_t _flags)
	{
		m_texture = bgfx::createTexture2D(uint16_t(_width), uint16_t(_height), false, 1, _format, _flags);
		const bool destroyTextures = true;
		m_buffer = bgfx::createFrameBuffer(1, &m_texture, destroyTextures);
	}

	void destroy()
	{
		// also responsible for destroying texture
		bgfx::destroy(m_buffer);
	}

	bgfx::TextureHandle m_texture;
	bgfx::FrameBufferHandle m_buffer;
};

static float floatFromBool(bool val)
{
	return (val) ? 1.0f : 0.0f;
}

void screenSpaceQuad(float _textureWidth, float _textureHeight, float _texelHalf, bool _originBottomLeft, float _width = 1.0f, float _height = 1.0f)
{
	if (3 == bgfx::getAvailTransientVertexBuffer(3, PosTexCoord0Vertex::ms_layout))
	{
		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 3, PosTexCoord0Vertex::ms_layout);
		PosTexCoord0Vertex* vertex = (PosTexCoord0Vertex*)vb.data;

		const float minx = -_width;
		const float maxx =  _width;
		const float miny = 0.0f;
		const float maxy =  _height * 2.0f;

		const float texelHalfW = _texelHalf / _textureWidth;
		const float texelHalfH = _texelHalf / _textureHeight;
		const float minu = -1.0f + texelHalfW;
		const float maxu =  1.0f + texelHalfW;

		const float zz = 0.0f;

		float minv = texelHalfH;
		float maxv = 2.0f + texelHalfH;

		if (_originBottomLeft)
		{
			float temp = minv;
			minv = maxv;
			maxv = temp;

			minv -= 1.0f;
			maxv -= 1.0f;
		}

		vertex[0].m_x = minx;
		vertex[0].m_y = miny;
		vertex[0].m_z = zz;
		vertex[0].m_u = minu;
		vertex[0].m_v = minv;

		vertex[1].m_x = maxx;
		vertex[1].m_y = miny;
		vertex[1].m_z = zz;
		vertex[1].m_u = maxu;
		vertex[1].m_v = minv;

		vertex[2].m_x = maxx;
		vertex[2].m_y = maxy;
		vertex[2].m_z = zz;
		vertex[2].m_u = maxu;
		vertex[2].m_v = maxv;

		bgfx::setVertexBuffer(0, &vb);
	}
}

void vec2Set(float* _v, float _x, float _y)
{
	_v[0] = _x;
	_v[1] = _y;
}

void vec4Set(float* _v, float _x, float _y, float _z, float _w)
{
	_v[0] = _x;
	_v[1] = _y;
	_v[2] = _z;
	_v[3] = _w;
}

void mat4Set(float * _m, const float * _src)
{
	const uint32_t MAT4_FLOATS = 16;
	for (uint32_t ii = 0; ii < MAT4_FLOATS; ++ii) {
		_m[ii] = _src[ii];
	}
}

class ExampleBokeh : public entry::AppI
{
public:
	ExampleBokeh(const char* _name, const char* _description)
		: entry::AppI(_name, _description)
		, m_currFrame(UINT32_MAX)
		, m_texelHalf(0.0f)
	{
	}

	void init(int32_t _argc, const char* const* _argv, uint32_t _width, uint32_t _height) override
	{
		Args args(_argc, _argv);

		m_width = _width;
		m_height = _height;
		m_debug = BGFX_DEBUG_NONE;
		m_reset = BGFX_RESET_VSYNC;

		bgfx::Init init;
		init.type = args.m_type;

		init.vendorId = args.m_pciId;
		init.resolution.width = m_width;
		init.resolution.height = m_height;
		init.resolution.reset = m_reset;
		bgfx::init(init);

		// Enable debug text.
		bgfx::setDebug(m_debug);

		// Create uniforms
		m_uniforms.init();

		// Create texture sampler uniforms (used when we bind textures)
		s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
		s_color = bgfx::createUniform("s_color", bgfx::UniformType::Sampler);
		s_normal = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
		s_depth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
		s_blurredColor = bgfx::createUniform("s_blurredColor", bgfx::UniformType::Sampler);

		// Create program from shaders.
		m_forwardProgram			= loadProgram("vs_bokeh_forward",		"fs_bokeh_forward");
		m_copyProgram				= loadProgram("vs_bokeh_screenquad",	"fs_bokeh_copy"); 
		m_linearDepthProgram		= loadProgram("vs_bokeh_screenquad",	"fs_bokeh_linear_depth");
		m_dofSinglePassProgram		= loadProgram("vs_bokeh_screenquad",	"fs_bokeh_dof_single_pass");
		m_dofDownsampleProgram		= loadProgram("vs_bokeh_screenquad",	"fs_bokeh_dof_downsample");
		m_dofQuarterProgram			= loadProgram("vs_bokeh_screenquad",	"fs_bokeh_dof_second_pass");
		m_dofCombineProgram			= loadProgram("vs_bokeh_screenquad",	"fs_bokeh_dof_combine");

		// Load some meshes
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_meshPaths); ++ii)
		{
			m_meshes[ii] = meshLoad(s_meshPaths[ii]);
		}

		// Randomly create some models
		bx::RngMwc mwc;
		for (uint32_t ii = 0; ii < BX_COUNTOF(m_models); ++ii)
		{
			Model& model = m_models[ii];

			model.mesh = mwc.gen() % BX_COUNTOF(s_meshPaths);
			model.position[0] = (((mwc.gen() % 256)) - 128.0f) / 20.0f;
			model.position[1] = 0;
			model.position[2] = (((mwc.gen() % 256)) - 128.0f) / 20.0f;
		}

		m_groundTexture = loadTexture("textures/fieldstone-rgba.dds");
		m_normalTexture = loadTexture("textures/fieldstone-n.dds");

		m_recreateFrameBuffers = false;
		createFramebuffers();
	
		// Vertex decl
		PosTexCoord0Vertex::init();

		// Init camera
		cameraCreate();
		cameraSetPosition({ 0.0f, 1.5f, -4.0f });
		cameraSetVerticalAngle(-0.3f);
		m_fovY = 60.0f;

		// Init "prev" matrices, will be same for first frame
		cameraGetViewMtx(m_view);
		bx::mtxProj(m_proj, m_fovY, float(m_size[0]) / float(m_size[1]), 0.01f, 100.0f,  bgfx::getCaps()->homogeneousDepth);

		// Get renderer capabilities info.
		const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
		m_texelHalf = bgfx::RendererType::Direct3D9 == renderer ? 0.5f : 0.0f;

		imguiCreate();
	}

	int32_t shutdown() override
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_meshPaths); ++ii)
		{
			meshUnload(m_meshes[ii]);
		}

		bgfx::destroy(m_normalTexture);
		bgfx::destroy(m_groundTexture);

		bgfx::destroy(m_forwardProgram);
		bgfx::destroy(m_copyProgram);
		bgfx::destroy(m_linearDepthProgram);
		bgfx::destroy(m_dofSinglePassProgram);
		bgfx::destroy(m_dofDownsampleProgram);
		bgfx::destroy(m_dofQuarterProgram);
		bgfx::destroy(m_dofCombineProgram);

		m_uniforms.destroy();

		bgfx::destroy(s_albedo);
		bgfx::destroy(s_color);
		bgfx::destroy(s_normal);
		bgfx::destroy(s_depth);
		bgfx::destroy(s_blurredColor);

		destroyFramebuffers();

		cameraDestroy();

		imguiDestroy();

		bgfx::shutdown();

		return 0;
	}

	bool update() override
	{
		if (!entry::processEvents(m_width, m_height, m_debug, m_reset, &m_mouseState))
		{
			// skip processing when minimized, otherwise crashing
			if (0 == m_width || 0 == m_height)
			{
				return true;
			}

			// Update frame timer
			int64_t now = bx::getHPCounter();
			static int64_t last = now;
			const int64_t frameTime = now - last;
			last = now;
			const double freq = double(bx::getHPFrequency());
			const float deltaTime = float(frameTime / freq);
			const bgfx::Caps* caps = bgfx::getCaps();

			if (m_size[0] != (int32_t)m_width
			||  m_size[1] != (int32_t)m_height
			||  m_recreateFrameBuffers)
			{
				destroyFramebuffers();
				createFramebuffers();
				m_recreateFrameBuffers = false;
			}

			// Update camera
			cameraUpdate(deltaTime*0.15f, m_mouseState);

			cameraGetViewMtx(m_view);

			updateUniforms();

			bx::mtxProj(m_proj, m_fovY, float(m_size[0]) / float(m_size[1]), 0.01f, 100.0f, caps->homogeneousDepth);
			bx::mtxProj(m_proj2, m_fovY, float(m_size[0]) / float(m_size[1]), 0.01f, 100.0f, false);


			bgfx::ViewId view = 0;

			// Draw models into scene
			{
				bgfx::setViewName(view, "forward scene");
				bgfx::setViewClear(view
					, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
					, 0
					, 1.0f
					, 0
				);

				bgfx::setViewRect(view, 0, 0, uint16_t(m_size[0]), uint16_t(m_size[1]));
				bgfx::setViewTransform(view, m_view, m_proj);
				bgfx::setViewFrameBuffer(view, m_frameBuffer);

				bgfx::setState(0
					| BGFX_STATE_WRITE_RGB
					| BGFX_STATE_WRITE_A
					| BGFX_STATE_WRITE_Z
					| BGFX_STATE_DEPTH_TEST_LESS
					);

				drawAllModels(view, m_forwardProgram, m_uniforms);

				++view;
			}

			float orthoProj[16];
			bx::mtxOrtho(orthoProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, caps->homogeneousDepth);
			{
				// clear out transform stack
				float identity[16];
				bx::mtxIdentity(identity);
				bgfx::setTransform(identity);
			}

			// Convert depth to linear depth for shadow depth compare
			{
				bgfx::setViewName(view, "linear depth");
				bgfx::setViewRect(view, 0, 0, uint16_t(m_width), uint16_t(m_height));
				bgfx::setViewTransform(view, NULL, orthoProj);
				bgfx::setViewFrameBuffer(view, m_linearDepth.m_buffer);
				bgfx::setState(0
					| BGFX_STATE_WRITE_RGB
					| BGFX_STATE_WRITE_A
					| BGFX_STATE_DEPTH_TEST_ALWAYS
					);
				bgfx::setTexture(0, s_depth, m_frameBufferTex[FRAMEBUFFER_RT_DEPTH]);
				m_uniforms.submit();
				screenSpaceQuad(float(m_width), float(m_height), m_texelHalf, caps->originBottomLeft);
				bgfx::submit(view, m_linearDepthProgram);
				++view;
			}

			// update last texture written, to chain passes together
			bgfx::TextureHandle lastTex = m_frameBufferTex[FRAMEBUFFER_RT_COLOR];

			//// Copy color result to swap chain
			//{
			//	bgfx::setViewName(view, "display");
			//	bgfx::setViewClear(view
			//		, BGFX_CLEAR_NONE
			//		, 0
			//		, 1.0f
			//		, 0
			//	);

			//	bgfx::setViewRect(view, 0, 0, uint16_t(m_width), uint16_t(m_height));
			//	bgfx::setViewTransform(view, NULL, orthoProj);

			//	if (m_useBokehDof)
			//	{
			//		bgfx::setViewFrameBuffer(view, m_temporaryColor.m_buffer);
			//	}
			//	else
			//	{
			//		bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
			//	}

			//	bgfx::setState(0
			//		| BGFX_STATE_WRITE_RGB
			//		| BGFX_STATE_WRITE_A
			//		);
			//	bgfx::setTexture(0, s_color, lastTex);
			//	screenSpaceQuad(float(m_width), float(m_height), m_texelHalf, caps->originBottomLeft);
			//	bgfx::submit(view, m_copyProgram);
			//	++view;
			//	lastTex = m_temporaryColor.m_texture;
			//}

			// optionally, apply dof
			if (m_useBokehDof)
			{
				view = drawDepthOfField(view, lastTex, orthoProj, caps->originBottomLeft);
			}
			else
			{
				bgfx::setViewName(view, "display");
				bgfx::setViewClear(view
					, BGFX_CLEAR_NONE
					, 0
					, 1.0f
					, 0
				);

				bgfx::setViewRect(view, 0, 0, uint16_t(m_width), uint16_t(m_height));
				bgfx::setViewTransform(view, NULL, orthoProj);
				bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
				bgfx::setState(0
					| BGFX_STATE_WRITE_RGB
					| BGFX_STATE_WRITE_A
					);
				bgfx::setTexture(0, s_color, lastTex);
				screenSpaceQuad(float(m_width), float(m_height), m_texelHalf, caps->originBottomLeft);
				bgfx::submit(view, m_copyProgram);
				++view;
			}

			// Draw UI
			imguiBeginFrame(m_mouseState.m_mx
				, m_mouseState.m_my
				, (m_mouseState.m_buttons[entry::MouseButton::Left] ? IMGUI_MBUT_LEFT : 0)
				| (m_mouseState.m_buttons[entry::MouseButton::Right] ? IMGUI_MBUT_RIGHT : 0)
				| (m_mouseState.m_buttons[entry::MouseButton::Middle] ? IMGUI_MBUT_MIDDLE : 0)
				, m_mouseState.m_mz
				, uint16_t(m_width)
				, uint16_t(m_height)
				);

			showExampleDialog(this);

			ImGui::SetNextWindowPos(
				ImVec2(m_width - m_width / 4.0f - 10.0f, 10.0f)
				, ImGuiCond_FirstUseEver
				);
			ImGui::SetNextWindowSize(
				ImVec2(m_width / 4.0f, m_height / 1.24f)
				, ImGuiCond_FirstUseEver
				);
			ImGui::Begin("Settings"
				, NULL
				, 0
				);

			ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);

			{
				ImGui::Checkbox("use bokeh dof", &m_useBokehDof);
				ImGui::Checkbox("use single pass", &m_useSinglePassBokehDof);
				ImGui::SliderFloat("max blur size", &m_maxBlurSize, 10.0f, 50.0f);
				ImGui::SliderFloat("focusPoint", &m_focusPoint, 1.0f, 20.0f);
				ImGui::SliderFloat("focusScale", &m_focusScale, 0.0f, 2.0f);
				ImGui::SliderFloat("radiusScale", &m_radiusScale, 0.5f, 4.0f);

				// having a difficult time reasoning about how many steps are taken when increasing
				// radius by (scale/radius) so calculate value instead. general pattern, take smaller
				// steps further from center. maybe use different formula that directly sets steps?
				const float maxRadius = m_maxBlurSize;
				float radius = m_radiusScale;
				int counter = 0;
				while (radius < maxRadius)
				{
					++counter;
					radius += m_radiusScale / radius;
				}
				ImGui::SliderInt("steps debug:", &counter, 0, counter);

				ImGui::Checkbox("use sqrt distribution", &m_useSqrtDistribution);
				ImGui::SliderFloat("blur steps", &m_blurSteps, 10.f, 100.0f);
			}

			ImGui::End();

			imguiEndFrame();

			// Advance to next frame. Rendering thread will be kicked to
			// process submitted rendering primitives.
			m_currFrame = bgfx::frame();

			return true;
		}

		return false;
	}

	void drawAllModels(bgfx::ViewId _pass, bgfx::ProgramHandle _program, const Uniforms & _uniforms)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(m_models); ++ii)
		{
			const Model& model = m_models[ii];

			// Set up transform matrix for each model
			const float scale = s_meshScale[model.mesh];
			float mtx[16];
			bx::mtxSRT(mtx
				, scale
				, scale
				, scale
				, 0.0f
				, 0.0f
				, 0.0f
				, model.position[0]
				, model.position[1]
				, model.position[2]
				);

			bgfx::setTexture(0, s_albedo, m_groundTexture);
			bgfx::setTexture(1, s_normal, m_normalTexture);
			_uniforms.submit();

			meshSubmit(m_meshes[model.mesh], _pass, _program, mtx);
		}

		// Draw ground
		float mtxScale[16];
		const float scale = 10.0f;
		bx::mtxScale(mtxScale, scale, scale, scale);

		float mtxTranslate[16];
		bx::mtxTranslate(mtxTranslate
			, 0.0f
			, -10.0f
			, 0.0f
			);

		float mtx[16];
		bx::mtxMul(mtx, mtxScale, mtxTranslate);
		bgfx::setTexture(0, s_albedo, m_groundTexture);
		bgfx::setTexture(1, s_normal, m_normalTexture);
		_uniforms.submit();

		meshSubmit(m_meshes[MeshCube], _pass, _program, mtx);
	}

	bgfx::ViewId drawDepthOfField(bgfx::ViewId _pass, bgfx::TextureHandle _colorTexture, float* _orthoProj, bool _originBottomLeft)
	{
		bgfx::ViewId view = _pass;
		bgfx::TextureHandle lastTex = _colorTexture;

		if (m_useSinglePassBokehDof)
		{
			bgfx::setViewName(view, "bokeh dof single pass");
			bgfx::setViewRect(view, 0, 0, uint16_t(m_width), uint16_t(m_height));
			bgfx::setViewTransform(view, NULL, _orthoProj);
			bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				| BGFX_STATE_DEPTH_TEST_ALWAYS
				);
			bgfx::setTexture(0, s_color, lastTex);
			bgfx::setTexture(1, s_depth, m_linearDepth.m_texture);
			screenSpaceQuad(float(m_width), float(m_height), m_texelHalf, _originBottomLeft);
			bgfx::submit(view, m_dofSinglePassProgram);
			++view;
		}
		else
		{
			unsigned halfWidth = (m_width/2);
			unsigned halfHeight = (m_height/2);

			bgfx::setViewName(view, "bokeh dof downsample");
			bgfx::setViewRect(view, 0, 0, uint16_t(halfWidth), uint16_t(halfHeight));
			bgfx::setViewTransform(view, NULL, _orthoProj);
			bgfx::setViewFrameBuffer(view, m_dofQuarterInput.m_buffer);
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				| BGFX_STATE_DEPTH_TEST_ALWAYS
				);
			bgfx::setTexture(0, s_color, lastTex);
			bgfx::setTexture(1, s_depth, m_linearDepth.m_texture);
			screenSpaceQuad(float(halfWidth), float(halfHeight), m_texelHalf, _originBottomLeft);
			bgfx::submit(view, m_dofDownsampleProgram);
			++view;
			lastTex = m_dofQuarterInput.m_texture;

			/*
				replace the copy with bokeh dof combine
				able to read circle of confusion and color from downsample pass
				along with full res color and depth?
				do we need half res depth? i'm confused about that...
			*/

			bgfx::setViewName(view, "bokeh dof quarter");
			bgfx::setViewRect(view, 0, 0, uint16_t(halfWidth), uint16_t(halfHeight));
			bgfx::setViewTransform(view, NULL, _orthoProj);
			bgfx::setViewFrameBuffer(view, m_dofQuarterOutput.m_buffer);
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				| BGFX_STATE_DEPTH_TEST_ALWAYS
				);
			bgfx::setTexture(0, s_color, lastTex);
			screenSpaceQuad(float(halfWidth), float(halfHeight), m_texelHalf, _originBottomLeft);
			bgfx::submit(view, m_dofQuarterProgram);
			++view;
			lastTex = m_dofQuarterOutput.m_texture;

			bgfx::setViewName(view, "bokeh dof combine");
			bgfx::setViewRect(view, 0, 0, uint16_t(m_width), uint16_t(m_height));
			bgfx::setViewTransform(view, NULL, _orthoProj);
			bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				| BGFX_STATE_DEPTH_TEST_ALWAYS
				);
			bgfx::setTexture(0, s_color, _colorTexture);
			bgfx::setTexture(1, s_blurredColor, lastTex);
			screenSpaceQuad(float(m_width), float(m_height), m_texelHalf, _originBottomLeft);
			bgfx::submit(view, m_dofCombineProgram);
			++view;
		}

		return view;
	}

	void createFramebuffers()
	{
		m_size[0] = m_width;
		m_size[1] = m_height;

		const uint64_t bilinearFlags = 0
			| BGFX_TEXTURE_RT
			| BGFX_SAMPLER_U_CLAMP
			| BGFX_SAMPLER_V_CLAMP
			;

		const uint64_t pointSampleFlags = bilinearFlags
			| BGFX_SAMPLER_MIN_POINT
			| BGFX_SAMPLER_MAG_POINT
			| BGFX_SAMPLER_MIP_POINT
			;

		m_frameBufferTex[FRAMEBUFFER_RT_COLOR]    = bgfx::createTexture2D(uint16_t(m_size[0]), uint16_t(m_size[1]), false, 1, bgfx::TextureFormat::BGRA8, pointSampleFlags);
		m_frameBufferTex[FRAMEBUFFER_RT_DEPTH]    = bgfx::createTexture2D(uint16_t(m_size[0]), uint16_t(m_size[1]), false, 1, bgfx::TextureFormat::D24, pointSampleFlags);
		m_frameBuffer = bgfx::createFrameBuffer(BX_COUNTOF(m_frameBufferTex), m_frameBufferTex, true);

		m_currentColor.init(m_size[0], m_size[1], bgfx::TextureFormat::RG11B10F, bilinearFlags);
		m_temporaryColor.init(m_size[0], m_size[1], bgfx::TextureFormat::RG11B10F, bilinearFlags);
		m_linearDepth.init(m_size[0], m_size[1], bgfx::TextureFormat::R16F, pointSampleFlags);

		unsigned halfWidth = m_size[0]/2;
		unsigned halfHeight = m_size[1]/2;
		m_dofQuarterInput.init(halfWidth, halfHeight, bgfx::TextureFormat::RGBA16F, bilinearFlags);
		m_dofQuarterOutput.init(halfWidth, halfHeight, bgfx::TextureFormat::RGBA16F, bilinearFlags);
	}

	// all buffers set to destroy their textures
	void destroyFramebuffers()
	{
		bgfx::destroy(m_frameBuffer);

		m_currentColor.destroy();
		m_temporaryColor.destroy();
		m_linearDepth.destroy();
		m_dofQuarterInput.destroy();
		m_dofQuarterOutput.destroy();
	}

	void updateUniforms()
	{
		mat4Set(m_uniforms.m_worldToView, m_view);
		mat4Set(m_uniforms.m_viewToProj, m_proj);

		// from assao sample, cs_assao_prepare_depths.sc
		{
			// float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
			// float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
			// correct the handedness issue. need to make sure this below is correct, but I think it is.

			float depthLinearizeMul = -m_proj2[3*4+2];
			float depthLinearizeAdd =  m_proj2[2*4+2];

			if (depthLinearizeMul * depthLinearizeAdd < 0)
			{
				depthLinearizeAdd = -depthLinearizeAdd;
			}

			vec2Set(m_uniforms.m_depthUnpackConsts, depthLinearizeMul, depthLinearizeAdd);

			float tanHalfFOVY = 1.0f / m_proj2[1*4+1];	// = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
			float tanHalfFOVX = 1.0F / m_proj2[0];		// = tanHalfFOVY * drawContext.Camera.GetAspect( );

			if (bgfx::getRendererType() == bgfx::RendererType::OpenGL)
			{
				vec2Set(m_uniforms.m_ndcToViewMul, tanHalfFOVX * 2.0f, tanHalfFOVY * 2.0f);
				vec2Set(m_uniforms.m_ndcToViewAdd, tanHalfFOVX * -1.0f, tanHalfFOVY * -1.0f);
			}
			else
			{
				vec2Set(m_uniforms.m_ndcToViewMul, tanHalfFOVX * 2.0f, tanHalfFOVY * -2.0f);
				vec2Set(m_uniforms.m_ndcToViewAdd, tanHalfFOVX * -1.0f, tanHalfFOVY * 1.0f);
			}
		}

		m_uniforms.m_frameIdx = float(m_currFrame % 8);

		{
			float lightPosition[] = { -10.0f, 10.0f, -10.0f };
			bx::memCopy(m_uniforms.m_lightPosition, lightPosition, 3*sizeof(float));
		}

		// bokeh depth of field
		{
			// reduce dimensions by half to go along with smaller render target
			const float blurScale = (m_useSinglePassBokehDof) ? 1.0f : 0.5f;
			m_uniforms.m_blurSteps = m_blurSteps;
			m_uniforms.m_useSqrtDistribution = floatFromBool(m_useSqrtDistribution);
			m_uniforms.m_maxBlurSize = m_maxBlurSize * blurScale;
			m_uniforms.m_focusPoint = m_focusPoint;
			m_uniforms.m_focusScale = m_focusScale;
			m_uniforms.m_radiusScale = m_radiusScale * blurScale;
		}
	}


	uint32_t m_width;
	uint32_t m_height;
	uint32_t m_debug;
	uint32_t m_reset;

	entry::MouseState m_mouseState;

	// Resource handles
	bgfx::ProgramHandle m_forwardProgram;
	bgfx::ProgramHandle m_copyProgram;
	bgfx::ProgramHandle m_linearDepthProgram;
	bgfx::ProgramHandle m_dofSinglePassProgram;
	bgfx::ProgramHandle m_dofDownsampleProgram;
	bgfx::ProgramHandle m_dofQuarterProgram;
	bgfx::ProgramHandle m_dofCombineProgram;

	// Shader uniforms
	Uniforms m_uniforms;

	// Uniforms to indentify texture samplers
	bgfx::UniformHandle s_albedo;
	bgfx::UniformHandle s_color;
	bgfx::UniformHandle s_normal;
	bgfx::UniformHandle s_depth;
	bgfx::UniformHandle s_blurredColor;

	bgfx::FrameBufferHandle m_frameBuffer;
	bgfx::TextureHandle m_frameBufferTex[FRAMEBUFFER_RENDER_TARGETS];

	RenderTarget m_currentColor;
	RenderTarget m_temporaryColor; // need another buffer to ping-pong results
	RenderTarget m_linearDepth;
	RenderTarget m_dofQuarterInput;
	RenderTarget m_dofQuarterOutput;

	struct Model
	{
		uint32_t mesh; // Index of mesh in m_meshes
		float position[3];
	};

	Model m_models[MODEL_COUNT];
	Mesh* m_meshes[BX_COUNTOF(s_meshPaths)];
	bgfx::TextureHandle m_groundTexture;
	bgfx::TextureHandle m_normalTexture;

	uint32_t m_currFrame;
	float m_lightRotation = 0.0f;
	float m_texelHalf = 0.0f;
	float m_fovY = 60.0f;
	bool m_recreateFrameBuffers = false;

	float m_view[16];
	float m_proj[16];
	float m_proj2[16];
	int32_t m_size[2];

	// UI parameters
	bool m_useBokehDof = true;
	bool m_useSinglePassBokehDof = true;
	float m_maxBlurSize = 20.0f;
	float m_focusPoint = 1.0f;
	float m_focusScale = 2.0f;
	float m_radiusScale = 3.856f;//0.5f;
	float m_blurSteps = 50.0f;
	bool m_useSqrtDistribution = false;
};

} // namespace

ENTRY_IMPLEMENT_MAIN(ExampleBokeh, "xx-bokeh", "bokeh depth of field");
