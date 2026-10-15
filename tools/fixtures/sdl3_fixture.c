// Independent SDL3 C ABI/behavior fixture; main owns lifecycle and verdict.
// graphics checks both SDL readback and actual X11 window pixels.
// input observes a real XTest-injected key and a tagged SDL event callback.
// threads exchanges known data; audio compares converted buffers and an
// independently captured PulseAudio virtual-sink stream to specified samples.
// Software servers are not physical GPU, keyboard or audible-device evidence.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BUSTER_SDL_WIDTH 80
#define BUSTER_SDL_HEIGHT 60
#define BUSTER_SDL_TIMEOUT 3000

typedef struct SdlExchange SdlExchange;
struct SdlExchange
{
    SDL_Mutex* mutex;
    SDL_Condition* condition;
    int input;
    int output;
    bool done;
};

static int sdl_worker(void* userdata)
{
    SdlExchange* state = userdata;
    SDL_LockMutex(state->mutex);
    state->output = state->input * 19 + 5;
    state->done = true;
    SDL_SignalCondition(state->condition);
    SDL_UnlockMutex(state->mutex);
    return 27;
}

static bool sdl_threads(void)
{
    SdlExchange state = {0};
    state.input = 17;
    state.mutex = SDL_CreateMutex();
    state.condition = SDL_CreateCondition();
    bool ok = state.mutex && state.condition;
    SDL_Thread* thread = ok ? SDL_CreateThread(sdl_worker, "sdl-abi-exchange", &state) : NULL;
    ok = ok && thread;
    if (thread)
    {
        SDL_LockMutex(state.mutex);
        Uint64 deadline = SDL_GetTicks() + BUSTER_SDL_TIMEOUT;
        while (!state.done && SDL_GetTicks() < deadline)
        {
            SDL_WaitConditionTimeout(state.condition, state.mutex, 50);
        }
        ok = state.done && state.output == 328;
        SDL_UnlockMutex(state.mutex);
        int status = 0;
        SDL_WaitThread(thread, &status);
        ok = ok && status == 27;
    }
    SDL_DestroyCondition(state.condition);
    SDL_DestroyMutex(state.mutex);
    return ok;
}

static bool sdl_watch(void* userdata, SDL_Event* event)
{
    unsigned* count = userdata;
    if (event->type == SDL_EVENT_USER && event->user.code == 3188)
    {
        *count += 1;
    }
    return true;
}

static bool sdl_input(SDL_Window* window, Display* display, Window native)
{
    unsigned callbacks = 0;
    fprintf(stderr, "SDL_INPUT add_watch begin\n");
    bool ok = SDL_AddEventWatch(sdl_watch, &callbacks);
    fprintf(stderr, "SDL_INPUT add_watch end ok=%d\n", ok);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    SDL_Event sent = {0};
    sent.type = SDL_EVENT_USER;
    sent.user.windowID = SDL_GetWindowID(window);
    sent.user.code = 3188;
    sent.user.data1 = &callbacks;
    fprintf(stderr, "SDL_INPUT push begin\n");
    ok = ok && SDL_PushEvent(&sent);
    fprintf(stderr, "SDL_INPUT push end ok=%d callbacks=%u\n", ok, callbacks);
    int event_base = 0, error_base = 0, major = 0, minor = 0;
    ok = ok && XTestQueryExtension(display, &event_base, &error_base, &major, &minor);
    KeyCode key = XKeysymToKeycode(display, XK_a);
    if (ok)
    {
        XSetInputFocus(display, native, RevertToParent, CurrentTime);
        ok = key != 0 && XTestFakeKeyEvent(display, key, True, CurrentTime) &&
             XTestFakeKeyEvent(display, key, False, CurrentTime);
        XSync(display, False);
    }
    fprintf(stderr, "SDL_INPUT native_injection end ok=%d\n", ok);
    bool user_seen = false, key_seen = false;
    Uint64 deadline = SDL_GetTicks() + BUSTER_SDL_TIMEOUT;
    while (ok && !(user_seen && key_seen) && SDL_GetTicks() < deadline)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_USER && event.user.code == 3188)
            {
                user_seen = event.user.windowID == sent.user.windowID && event.user.data1 == &callbacks;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == sent.user.windowID)
            {
                key_seen = event.key.scancode == SDL_SCANCODE_A && event.key.key == SDLK_A;
            }
        }
        SDL_Delay(1);
    }
    fprintf(stderr, "SDL_INPUT poll end user=%d key=%d\n", user_seen, key_seen);
    SDL_RemoveEventWatch(sdl_watch, &callbacks);
    ok = ok && user_seen && key_seen && callbacks == 1;
    unsigned before = callbacks;
    ok = SDL_PushEvent(&sent) && callbacks == before && ok;
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    return ok;
}

static bool sdl_expected_pixel(int x, int y, unsigned* red, unsigned* green, unsigned* blue)
{
    *red = x < BUSTER_SDL_WIDTH / 2 ? 255 : 0;
    *green = x >= BUSTER_SDL_WIDTH / 2 ? 255 : 0;
    *blue = y < BUSTER_SDL_HEIGHT / 2 ? 0 : 255;
    return true;
}

static unsigned sdl_x_component(unsigned long pixel, unsigned long mask)
{
    unsigned shift = 0;
    while (mask && !(mask & 1))
    {
        mask >>= 1;
        shift += 1;
    }
    unsigned value = mask ? (unsigned)(((pixel >> shift) & mask) * 255 / mask) : 0;
    return value;
}

static bool sdl_graphics(SDL_Window* window, Display* display, Window native)
{
    bool ok = SDL_SetWindowSize(window, BUSTER_SDL_WIDTH, BUSTER_SDL_HEIGHT);
    int width = 0, height = 0;
    Uint64 deadline = SDL_GetTicks() + BUSTER_SDL_TIMEOUT;
    while (ok && SDL_GetTicks() < deadline && (width != BUSTER_SDL_WIDTH || height != BUSTER_SDL_HEIGHT))
    {
        SDL_PumpEvents();
        ok = SDL_GetWindowSize(window, &width, &height);
        SDL_Delay(1);
    }
    ok = ok && width == BUSTER_SDL_WIDTH && height == BUSTER_SDL_HEIGHT;
    SDL_Renderer* renderer = ok ? SDL_CreateRenderer(window, "software") : NULL;
    ok = ok && renderer && !strcmp(SDL_GetRendererName(renderer), "software");
    for (int row = 0; ok && row < 2; row += 1)
    {
        for (int column = 0; ok && column < 2; column += 1)
        {
            unsigned red = 0, green = 0, blue = 0;
            const Uint8 palette[4][3] = {{255, 0, 0}, {0, 255, 0}, {255, 0, 255}, {0, 255, 255}};
            red = palette[row * 2 + column][0];
            green = palette[row * 2 + column][1];
            blue = palette[row * 2 + column][2];
            SDL_FRect rect = {(float)(column * 40), (float)(row * 30), 40.0f, 30.0f};
            ok = SDL_SetRenderDrawColor(renderer, (Uint8)red, (Uint8)green, (Uint8)blue, 255) &&
                 SDL_RenderFillRect(renderer, &rect);
        }
    }
    SDL_Surface* surface = ok ? SDL_RenderReadPixels(renderer, NULL) : NULL;
    ok = ok && surface && surface->w == BUSTER_SDL_WIDTH && surface->h == BUSTER_SDL_HEIGHT;
    for (int y = 0; ok && y < BUSTER_SDL_HEIGHT; y += 1)
    {
        for (int x = 0; ok && x < BUSTER_SDL_WIDTH; x += 1)
        {
            unsigned red = 0, green = 0, blue = 0;
            Uint8 actual_red = 0, actual_green = 0, actual_blue = 0, alpha = 0;
            sdl_expected_pixel(x, y, &red, &green, &blue);
            ok = SDL_ReadSurfacePixel(surface, x, y, &actual_red, &actual_green, &actual_blue, &alpha) &&
                 actual_red == red && actual_green == green && actual_blue == blue && alpha == 255;
        }
    }
    SDL_DestroySurface(surface);
    ok = ok && SDL_RenderPresent(renderer);
    XSync(display, False);
    XImage* image = ok ? XGetImage(display, native, 0, 0, BUSTER_SDL_WIDTH, BUSTER_SDL_HEIGHT, AllPlanes, ZPixmap) : NULL;
    ok = ok && image;
    for (int y = 0; ok && y < BUSTER_SDL_HEIGHT; y += 1)
    {
        for (int x = 0; ok && x < BUSTER_SDL_WIDTH; x += 1)
        {
            unsigned red = 0, green = 0, blue = 0;
            sdl_expected_pixel(x, y, &red, &green, &blue);
            unsigned long pixel = XGetPixel(image, x, y);
            ok = sdl_x_component(pixel, image->red_mask) == red &&
                 sdl_x_component(pixel, image->green_mask) == green &&
                 sdl_x_component(pixel, image->blue_mask) == blue;
        }
    }
    if (image)
    {
        XDestroyImage(image);
    }
    SDL_DestroyRenderer(renderer);
    return ok;
}

static bool sdl_audio_buffer(void)
{
    Sint16 input[256];
    float output[256];
    for (int i = 0; i < 256; i += 1)
    {
        input[i] = (Sint16)((i % 4 - 2) * 8192);
        output[i] = 99.0f;
    }
    SDL_AudioSpec source = {SDL_AUDIO_S16, 1, 48000};
    SDL_AudioSpec destination = {SDL_AUDIO_F32, 1, 48000};
    SDL_AudioStream* stream = SDL_CreateAudioStream(&source, &destination);
    bool ok = stream && SDL_SetAudioStreamGain(stream, 0.5f) &&
              SDL_PutAudioStreamData(stream, input, sizeof(input)) && SDL_FlushAudioStream(stream);
    ok = ok && SDL_GetAudioStreamData(stream, output, sizeof(output)) == sizeof(output);
    for (int i = 0; ok && i < 256; i += 1)
    {
        ok = fabsf(output[i] - (float)input[i] / 65536.0f) <= 0.0000001f;
    }
    ok = ok && SDL_GetAudioStreamAvailable(stream) == 0;
    SDL_DestroyAudioStream(stream);
    return ok;
}

static bool sdl_audio_sink(void)
{
    pa_sample_spec pulse_spec = {PA_SAMPLE_FLOAT32NE, 48000, 1};
    pa_buffer_attr buffering = {(uint32_t)-1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1024};
    int error = 0;
    pa_simple* monitor = pa_simple_new(NULL, "Buster SDL3 oracle", PA_STREAM_RECORD, "buster_sdl3.monitor",
        "known samples", &pulse_spec, NULL, &buffering, &error);
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, 48000};
    SDL_AudioStream* stream = monitor ? SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL) : NULL;
    float input[24000];
    const float wave[4] = {0.125f, -0.125f, 0.0625f, -0.0625f};
    for (int i = 0; i < 24000; i += 1)
    {
        input[i] = wave[i % 4];
    }
    bool ok = stream && SDL_PutAudioStreamData(stream, input, sizeof(input)) &&
              SDL_FlushAudioStream(stream) && SDL_ResumeAudioStreamDevice(stream);
    bool matched = false;
    for (int block = 0; ok && !matched && block < 96; block += 1)
    {
        float captured[1024];
        ok = pa_simple_read(monitor, captured, sizeof(captured), &error) == 0;
        for (int offset = 0; ok && !matched && offset <= 1024 - 64; offset += 1)
        {
            for (int phase = 0; !matched && phase < 4; phase += 1)
            {
                bool run = true;
                for (int i = 0; run && i < 64; i += 1)
                {
                    run = fabsf(captured[offset + i] - wave[(i + phase) % 4]) <= 0.00001f;
                }
                matched = run;
            }
        }
    }
    if (!ok)
    {
        fprintf(stderr, "Pulse monitor failure: %s\n", pa_strerror(error));
    }
    SDL_DestroyAudioStream(stream);
    if (monitor)
    {
        pa_simple_free(monitor);
    }
    return ok && matched;
}

int main(void)
{
    fprintf(stderr, "SDL_PHASE init begin\n");
    SDL_SetMainReady();
    bool initialized = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    bool ok = initialized && !strcmp(SDL_GetCurrentVideoDriver(), "x11") &&
              !strcmp(SDL_GetCurrentAudioDriver(), "pulseaudio");
    fprintf(stderr, "SDL_PHASE init end ok=%d\n", ok);
    fprintf(stderr, "SDL_PHASE window begin\n");
    SDL_Window* window = ok ? SDL_CreateWindow("Buster SDL3 qualification", 64, 64, 0) : NULL;
    ok = ok && window;
    SDL_PropertiesID properties = window ? SDL_GetWindowProperties(window) : 0;
    Display* display = (Display*)SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    Window native = (Window)SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    fprintf(stderr, "SDL_PHASE window end ok=%d display=%d native=%d\n", ok, display != NULL, native != 0);
    fprintf(stderr, "SDL_PHASE graphics begin\n");
    bool graphics = ok && display && native && sdl_graphics(window, display, native);
    fprintf(stderr, "SDL_PHASE graphics end ok=%d\n", graphics);
    fprintf(stderr, "SDL_PHASE input begin\n");
    bool input = ok && display && native && sdl_input(window, display, native);
    fprintf(stderr, "SDL_PHASE input end ok=%d\n", input);
    fprintf(stderr, "SDL_PHASE threads begin\n");
    bool threads = ok && sdl_threads();
    fprintf(stderr, "SDL_PHASE threads end ok=%d\n", threads);
    fprintf(stderr, "SDL_PHASE audio_buffer begin\n");
    bool audio_buffer = ok && sdl_audio_buffer();
    fprintf(stderr, "SDL_PHASE audio_buffer end ok=%d\n", audio_buffer);
    fprintf(stderr, "SDL_PHASE audio_sink begin\n");
    bool audio_sink = ok && sdl_audio_sink();
    fprintf(stderr, "SDL_PHASE audio_sink end ok=%d\n", audio_sink);
    fprintf(stderr, "SDL_PHASE quit begin\n");
    SDL_DestroyWindow(window);
    if (initialized)
    {
        SDL_Quit();
    }
    bool quit = SDL_WasInit(0) == 0;
    fprintf(stderr, "SDL_PHASE quit end ok=%d\n", quit);
    printf("SDL_FIXTURE x11_pixels=%d xtest_input=%d thread_exchange=%d audio_buffer=%d pulse_monitor=%d quit=%d\n",
        graphics, input, threads, audio_buffer, audio_sink, quit);
    if (!(graphics && input && threads && audio_buffer && audio_sink && quit))
    {
        fprintf(stderr, "SDL fixture failed: %s\n", SDL_GetError());
    }
    return graphics && input && threads && audio_buffer && audio_sink && quit ? 0 : 1;
}
