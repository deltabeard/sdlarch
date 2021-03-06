
#include <SDL.h>
#include "libretro.h"
#include "glad.h"

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static SDL_Texture *g_tex = NULL;
static const uint8_t *g_kbd = NULL;

bool running = true;

static struct {
    int glmajor;
    int glminor;

	GLuint pixfmt;
	GLuint pixtype;
	GLuint bpp;

    struct retro_hw_render_callback hw;
} g_video  = {0};

static struct {
	void *handle;
	bool initialized;

	void (*retro_init)(void);
	void (*retro_deinit)(void);
	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info *info);
	void (*retro_get_system_av_info)(struct retro_system_av_info *info);
	void (*retro_set_controller_port_device)(unsigned port, unsigned device);
	void (*retro_reset)(void);
	void (*retro_run)(void);
//	size_t retro_serialize_size(void);
//	bool retro_serialize(void *data, size_t size);
//	bool retro_unserialize(const void *data, size_t size);
//	void retro_cheat_reset(void);
//	void retro_cheat_set(unsigned index, bool enabled, const char *code);
	bool (*retro_load_game)(const struct retro_game_info *game);
//	bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
	void (*retro_unload_game)(void);
//	unsigned retro_get_region(void);
//	void *retro_get_memory_data(unsigned id);
//	size_t retro_get_memory_size(unsigned id);
} g_retro;


struct keymap {
	unsigned k;
	unsigned rk;
};

static struct keymap g_binds[] = {
    { SDL_SCANCODE_X, RETRO_DEVICE_ID_JOYPAD_A },
    { SDL_SCANCODE_Z, RETRO_DEVICE_ID_JOYPAD_B },
    { SDL_SCANCODE_A, RETRO_DEVICE_ID_JOYPAD_Y },
    { SDL_SCANCODE_S, RETRO_DEVICE_ID_JOYPAD_X },
    { SDL_SCANCODE_UP, RETRO_DEVICE_ID_JOYPAD_UP },
    { SDL_SCANCODE_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN },
    { SDL_SCANCODE_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT },
    { SDL_SCANCODE_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
    { SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START },
    { SDL_SCANCODE_BACKSPACE, RETRO_DEVICE_ID_JOYPAD_SELECT },
    { SDL_SCANCODE_Q, RETRO_DEVICE_ID_JOYPAD_L },
    { SDL_SCANCODE_W, RETRO_DEVICE_ID_JOYPAD_R },
    { 0, 0 }
};

static unsigned g_joy[RETRO_DEVICE_ID_JOYPAD_R3+1] = { 0 };

#define load_sym(V, S) do {\
    if (!((*(void**)&V) = SDL_LoadFunction(g_retro.handle, #S))) \
        die("Failed to load symbol '" #S "'': %s", SDL_GetError()); \
	} while (0)
#define load_retro_sym(S) load_sym(g_retro.S, S)


static void core_log(enum retro_log_level level, const char *fmt, ...) {
    char buffer[4096] = {0};
    static const char * levelstr[] = { "dbg", "inf", "wrn", "err" };
    va_list va;

    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);

    if (level == 0)
        return;

    fprintf(stderr, "[%s] %s", levelstr[level], buffer);
    fflush(stderr);

    if (level == RETRO_LOG_ERROR)
        exit(EXIT_FAILURE);
}

static void die(const char *fmt, ...) {
	char buffer[4096];

	va_list va;
	va_start(va, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	fputs(buffer, stderr);
	fputc('\n', stderr);
	fflush(stderr);

	exit(EXIT_FAILURE);
}

static void create_window(int width, int height) {
    g_win = SDL_CreateWindow("sdlarch", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL);

	if (!g_win)
        die("Failed to create window: %s", SDL_GetError());

	/* Creates context instead of using createRenderer. */
	   g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!g_ren)
        die("Failed to create OpenGL context: %s", SDL_GetError());

    if (g_video.hw.context_type == RETRO_HW_CONTEXT_OPENGLES2) {
        if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
            die("Failed to initialize glad.");
    } else {
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
            die("Failed to initialize glad.");
    }

    fprintf(stderr, "GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    fprintf(stderr, "GL_VERSION: %s\n", glGetString(GL_VERSION));
}

static void video_configure(const struct retro_game_geometry *geom) {
    int nwidth = geom->base_width;
    int nheight = geom->base_height;

    if (!g_win)
        create_window(nwidth, nheight);

    if (!g_video.pixfmt)
        g_video.pixfmt = GL_UNSIGNED_INT_8_8_8_8_REV;

    SDL_SetWindowSize(g_win, nwidth, nheight);


    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET,
                              nwidth, nheight);

    if(g_tex == NULL)
        die("Failed to create texture");

    SDL_SetRenderTarget(g_ren, g_tex);

    if ( g_video.hw.depth && g_video.hw.stencil ) {
	GLuint rbo_id;
        glGenRenderbuffers ( 1, &rbo_id );
        glBindRenderbuffer ( GL_RENDERBUFFER, rbo_id );
        glRenderbufferStorage ( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, nwidth, nheight );

        glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_id);
    } else if ( g_video.hw.depth ) {
	GLuint rbo_id;
        glGenRenderbuffers ( 1, &rbo_id );
        glBindRenderbuffer ( GL_RENDERBUFFER, rbo_id );
        glRenderbufferStorage ( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, nwidth, nheight );

        glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo_id );
    }

    core_log(RETRO_LOG_INFO, "Texture: %dw * %dh, %d * %d\n",  geom->base_width, geom->base_height,
           geom->max_width, geom->max_height);

    g_video.hw.context_reset();
    SDL_SetRenderTarget(g_ren, NULL);
}


static bool video_set_pixel_format(unsigned format)
{
	switch (format) {
	case RETRO_PIXEL_FORMAT_0RGB1555:
		g_video.pixfmt = GL_UNSIGNED_SHORT_5_5_5_1;
		g_video.pixtype = GL_BGRA;
		g_video.bpp = sizeof(uint16_t);
		break;
	case RETRO_PIXEL_FORMAT_XRGB8888:
		g_video.pixfmt = GL_UNSIGNED_INT_8_8_8_8_REV;
		g_video.pixtype = GL_BGRA;
		g_video.bpp = sizeof(uint32_t);
		break;
	case RETRO_PIXEL_FORMAT_RGB565:
		g_video.pixfmt  = GL_UNSIGNED_SHORT_5_6_5;
		g_video.pixtype = GL_RGB;
		g_video.bpp = sizeof(uint16_t);
		break;
	default:
		die("Unknown pixel type %u", format);
	}

	return true;
}

unsigned frame_valid = 0;

static void video_refresh(const void *data, unsigned width, unsigned height, unsigned pitch)
{
    if(data != RETRO_HW_FRAME_BUFFER_VALID)
	return;
}

static void video_deinit() {

}

static uintptr_t core_get_current_framebuffer() {
    return 1;
}

static bool core_environment(unsigned cmd, void *data) {
	bool *bval;

	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
		struct retro_log_callback *cb = (struct retro_log_callback *)data;
		cb->log = core_log;
        return true;
	}
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		bval = (bool*)data;
		*bval = true;
        return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		const enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;

		if (*fmt > RETRO_PIXEL_FORMAT_RGB565)
			return false;

		return video_set_pixel_format(*fmt);
	}
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        struct retro_hw_render_callback *hw = (struct retro_hw_render_callback*)data;
        hw->get_current_framebuffer = core_get_current_framebuffer;
        hw->get_proc_address = (retro_hw_get_proc_address_t)SDL_GL_GetProcAddress;
        g_video.hw = *hw;
        return true;
    }
	default:
		core_log(RETRO_LOG_DEBUG, "Unhandled env #%u", cmd);
		return false;
	}

    return false;
}


static void core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    video_refresh(data, width, height, pitch);
}


static void core_input_poll(void) {
	int i;
    g_kbd = SDL_GetKeyboardState(NULL);

	for (i = 0; g_binds[i].k || g_binds[i].rk; ++i)
        g_joy[g_binds[i].rk] = g_kbd[g_binds[i].k];

    if (g_kbd[SDL_SCANCODE_ESCAPE])
        running = false;
}


static int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
	if (port || index || device != RETRO_DEVICE_JOYPAD)
		return 0;

	return g_joy[id];
}


static void core_audio_sample(int16_t left, int16_t right) {
	return;
}


static size_t core_audio_sample_batch(const int16_t *data, size_t frames) {
	return frames;
}


static void core_load(const char *sofile) {
	void (*set_environment)(retro_environment_t) = NULL;
	void (*set_video_refresh)(retro_video_refresh_t) = NULL;
	void (*set_input_poll)(retro_input_poll_t) = NULL;
	void (*set_input_state)(retro_input_state_t) = NULL;
	void (*set_audio_sample)(retro_audio_sample_t) = NULL;
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;
	memset(&g_retro, 0, sizeof(g_retro));
    g_retro.handle = SDL_LoadObject(sofile);

	if (!g_retro.handle)
        die("Failed to load core: %s", SDL_GetError());

	load_retro_sym(retro_init);
	load_retro_sym(retro_deinit);
	load_retro_sym(retro_api_version);
	load_retro_sym(retro_get_system_info);
	load_retro_sym(retro_get_system_av_info);
	load_retro_sym(retro_set_controller_port_device);
	load_retro_sym(retro_reset);
	load_retro_sym(retro_run);
	load_retro_sym(retro_load_game);
	load_retro_sym(retro_unload_game);

	load_sym(set_environment, retro_set_environment);
	load_sym(set_video_refresh, retro_set_video_refresh);
	load_sym(set_input_poll, retro_set_input_poll);
	load_sym(set_input_state, retro_set_input_state);
	load_sym(set_audio_sample, retro_set_audio_sample);
	load_sym(set_audio_sample_batch, retro_set_audio_sample_batch);

	set_environment(core_environment);
	set_video_refresh(core_video_refresh);
	set_input_poll(core_input_poll);
	set_input_state(core_input_state);
	set_audio_sample(core_audio_sample);
	set_audio_sample_batch(core_audio_sample_batch);

	g_retro.retro_init();
	g_retro.initialized = true;

	puts("Core loaded");
}


static void core_load_game(const char *filename) {
	struct retro_system_av_info av = {0};
	struct retro_system_info system = {0};
	struct retro_game_info info = { filename, 0 };

    SDL_RWops *file = SDL_RWFromFile(filename, "rb");

    if (!file)
        die("Failed to load %s: %s", filename, SDL_GetError());

    info.path = filename;
    info.meta = "";
    info.data = NULL;
    info.size = SDL_RWsize(file);

	g_retro.retro_get_system_info(&system);

	if (!system.need_fullpath) {
        info.data = SDL_malloc(info.size);

        if (!info.data)
            die("Failed to allocate memory for the content");

        if (!SDL_RWread(file, (void*)info.data, info.size, 1))
            die("Failed to read file data: %s", SDL_GetError());
	}

	if (!g_retro.retro_load_game(&info))
		die("The core failed to load the content.");

	g_retro.retro_get_system_av_info(&av);

	video_configure(&av.geometry);

    if (info.data)
        SDL_free((void*)info.data);

    SDL_RWclose(file);

    // Now that we have the system info, set the window title.
    char window_title[255];
    snprintf(window_title, sizeof(window_title), "sdlarch %s %s", system.library_name, system.library_version);
    SDL_SetWindowTitle(g_win, window_title);
}

static void core_unload() {
	if (g_retro.initialized)
		g_retro.retro_deinit();

	if (g_retro.handle)
        SDL_UnloadObject(g_retro.handle);
}

static void noop() {}

int main(int argc, char *argv[]) {
	if (argc < 3)
		die("usage: %s <core> <game>", argv[0]);

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
        die("Failed to initialize SDL");

    g_video.hw.version_major = 3;
    g_video.hw.version_minor = 3;
    g_video.hw.context_type  = RETRO_HW_CONTEXT_OPENGL_CORE;
    g_video.hw.context_reset   = noop;
    g_video.hw.context_destroy = noop;

    // Load the core.
    core_load(argv[1]);

    // Load the game.
    core_load_game(argv[2]);

    // Configure the player input devices.
    g_retro.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                switch (ev.window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                    running = false;
                    break;
                }
            }
        }

        SDL_RenderClear( g_ren );
        SDL_SetRenderTarget(g_ren, g_tex);
        SDL_GL_BindTexture(g_tex, NULL, NULL);
        g_retro.retro_run();
	SDL_GL_UnbindTexture(g_tex);
        SDL_RenderFlush(g_ren);
	SDL_SetRenderTarget(g_ren, NULL);

	SDL_RenderCopyEx(g_ren, g_tex, NULL, NULL, 30, NULL, SDL_FLIP_VERTICAL);
        SDL_RenderPresent(g_ren);
    }

    core_unload();
    video_deinit();

    SDL_Quit();

    return EXIT_SUCCESS;
}
