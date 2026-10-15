// Actual window pixels, not a source-image readback: content may occupy only
// part of a grow-only attachment. No game, dump or external renderer is used.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_device.h"

namespace {
using namespace ps2r;
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (false)
constexpr int window_size = 64;

std::vector<uint8_t> WindowPixels() {
    std::vector<uint8_t> pixels(window_size*window_size*4);
    glReadBuffer(GL_BACK);
    glReadPixels(0,0,window_size,window_size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    return pixels;
}

std::array<uint8_t,4> Pattern(int x,int y) {
    return {uint8_t(9+x*7),uint8_t(11+y*9),uint8_t(13+(x^y)*5),255};
}

void Compare(const std::vector<uint8_t>& actual,const render::ScanoutDesc& desc) {
    // Physical source texel selected at each destination pixel center.
    const auto& r = desc.src;
    const auto& d = desc.dst;
    unsigned differences = 0;
    for (int y = 0; y < window_size; ++y) for (int x = 0; x < window_size; ++x) {
        std::array<uint8_t,4> expected{0,0,0,255};
        if (x >= d.x && y >= d.y && x < d.x+d.w && y < d.y+d.h) {
            const int sx = ((2*(x-d.x)+1)*r.w)/(2*d.w);
            const int sample_y = (2*(y-d.y)+1)*r.h;
            const int sy = (desc.flip_y ? 2*r.h*d.h-sample_y : sample_y)/(2*d.h);
            expected = Pattern(r.x+sx,r.y+sy);
        }
        const size_t at = (size_t(y)*window_size+x)*4;
        for (unsigned c = 0; c < 4; ++c) differences += actual[at+c] != expected[c];
    }
    if (differences) std::fprintf(stderr,"window differences=%u src=%d,%d,%d,%d flip=%d\n",
        differences,r.x,r.y,r.w,r.h,desc.flip_y);
    CHECK(differences == 0);
}

void Mapping(render::GlDevice& backend,std::vector<uint8_t>& shown,unsigned& swaps) {
    for (uint8_t scale : {1,2}) {
        render::SurfaceDesc surface;
        surface.width = 32; surface.height = 16; surface.scale = scale;
        surface.renderable = true;
        const auto image = backend.CreateSurface(surface);
        CHECK(image.id != 0);
        std::vector<uint8_t> pixels(surface.width*surface.height*4);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) {
            const auto color = Pattern(x,y);
            std::copy(color.begin(),color.end(),pixels.begin()+(y*32+x)*4);
        }
        backend.Upload(image,{0,0,32,16},pixels.data(),32*4);
        const std::array<render::ScanoutDesc,7> cases{{
            {image,{0,0,32,16},{0,0,64,64},true}, // existing full-image convention
            {image,{0,0,16,12},{0,0,64,48},true}, // crop away old rows and columns
            {image,{3,5,8,8},{8,12,32,32},true},  // source and destination origins
            {image,{3,5,8,8},{8,12,32,32},false},
            {image,{31,15,1,1},{0,0,64,64},false},
            {image,{0,0,32,16},{0,0,64,64},false},
            {image,{0,0,32,16},{0,0,64,64},true}, // crop/flip must not leak
        }};
        for (const auto& desc : cases) {
            const unsigned before = swaps;
            backend.Present(desc);
            CHECK(swaps == before+1);
            Compare(shown,desc);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        auto fallback = cases.front();
        fallback.src = {}; fallback.dst = {};
        backend.Present(fallback);
        Compare(shown,cases.front()); // legacy unspecified extent remains full image

        // The armed present capture (the frame dump's presented image): the
        // window image of each case is read back at the window's size, the
        // window still shows the same image, and an unarmed device reads nothing.
        std::vector<uint8_t> presented;
        int pw = 0, ph = 0;
        CHECK(!backend.ReadPresented(&presented,&pw,&ph));
        backend.ArmPresentCapture(true);
        for (const auto& desc : {cases[2],cases[5]}) {
            const unsigned before = swaps;
            backend.Present(desc);
            CHECK(swaps == before+1);
            Compare(shown,desc);
            CHECK(backend.ReadPresented(&presented,&pw,&ph) && pw == window_size && ph == window_size);
            Compare(presented,desc);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        backend.ArmPresentCapture(false);
        CHECK(!backend.ReadPresented(&presented,&pw,&ph));

        const auto before_pixels = WindowPixels();
        const unsigned before_swaps = swaps;
        for (const render::Rect rect : {
                 render::Rect{-1,0,1,1}, {0,-1,1,1}, {0,0,0,1}, {0,0,1,-1},
                 {1,0,0,0}, {31,0,2,1}, {0,15,1,2}, {INT_MAX,0,INT_MAX,1},
                 {0,INT_MAX,1,INT_MAX}}) {
            auto invalid = cases.front(); invalid.src = rect;
            bool refused = false;
            try { backend.Present(invalid); }
            catch (const std::invalid_argument&) { refused = true; }
            CHECK(refused && swaps == before_swaps && WindowPixels() == before_pixels);
        }
        backend.DestroySurface(image);
        bool refused = false;
        try { backend.Present(cases.front()); }
        catch (const std::invalid_argument&) { refused = true; }
        CHECK(refused && swaps == before_swaps && WindowPixels() == before_pixels);
    }
    const unsigned before = swaps;
    backend.Present({}); // cold no-image presentation keeps its one black swap
    CHECK(swaps == before+1);
    bool black = true;
    for (size_t at = 0; at < shown.size(); at += 4)
        black &= shown[at] == 0 && shown[at+1] == 0 && shown[at+2] == 0 && shown[at+3] == 255;
    CHECK(black);
}

// The bilinear pin's tolerance: the reference filter is exact (double) and rounds once;
// a GL implementation computes the weights in its own fixed point and rounds once, so a
// hardware rasteriser is within ONE output code of the reference (Apple GL: worst 1).
// Mesa's llvmpipe (the software rasteriser of the Linux lane) interpolates with its own
// fixed-point weights: measured on Mesa 25.2.8 / LLVM 20.1.2 (Ubuntu 24.04, aarch64)
// the worst channel is exactly TWO codes off on the cropped source (4,3,16,8), 0 beyond
// -- a precision class, not a sampling or mapping error (the nearest/crop comparisons
// stay exact there). The pin is ±1 on hardware GL and ±2 only when GL_RENDERER names a
// software rasteriser; the measured worst case is printed whenever it exceeds 1 so a
// regression of the mapping cannot hide behind the wider class.
int LinearTolerance() {
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string name = renderer ? renderer : "";
    const bool software = name.find("llvmpipe") != std::string::npos ||
                          name.find("softpipe") != std::string::npos ||
                          name.find("SWR") != std::string::npos;
    return software ? 2 : 1;
}

void CompareLinear(const std::vector<uint8_t>& actual,const render::ScanoutDesc& desc) {
    unsigned differences = 0;
    int worst = 0;
    const int tolerance = LinearTolerance();
    for (int y = 0; y < window_size; ++y) for (int x = 0; x < window_size; ++x) {
        const auto& r = desc.src;
        const auto& d = desc.dst;
        const size_t at = (size_t(y)*window_size+x)*4;
        if (x < d.x || y < d.y || x >= d.x+d.w || y >= d.y+d.h) {
            differences += actual[at] != 0 || actual[at+1] != 0 || actual[at+2] != 0 ||
                actual[at+3] != 255;
            continue;
        }
        const double fx = r.x+(x-d.x+0.5)*r.w/d.w-0.5;
        const double fy = r.y+(desc.flip_y ? r.h-(y-d.y+0.5)*r.h/d.h :
                                                       (y-d.y+0.5)*r.h/d.h)-0.5;
        const int ix = int(std::floor(fx)),iy = int(std::floor(fy));
        const double tx = fx-ix,ty = fy-iy;
        const auto a = Pattern(std::clamp(ix,0,31),std::clamp(iy,0,15));
        const auto b = Pattern(std::clamp(ix+1,0,31),std::clamp(iy,0,15));
        const auto c = Pattern(std::clamp(ix,0,31),std::clamp(iy+1,0,15));
        const auto e = Pattern(std::clamp(ix+1,0,31),std::clamp(iy+1,0,15));
        for (unsigned channel = 0; channel < 4; ++channel) {
            const int expected = int(std::lround((a[channel]*(1-tx)+b[channel]*tx)*(1-ty) +
                                                (c[channel]*(1-tx)+e[channel]*tx)*ty));
            const int delta = std::abs(int(actual[at+channel])-expected);
            worst = std::max(worst,delta);
            differences += delta > tolerance;
        }
    }
    if (differences || worst > 1) std::fprintf(stderr,
        "bilinear: worst |delta|=%d codes, %u channels beyond +/-%d (renderer %s) src=%d,%d,%d,%d flip=%d\n",
        worst,differences,tolerance,reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
        desc.src.x,desc.src.y,desc.src.w,desc.src.h,desc.flip_y);
    CHECK(differences == 0); // within the renderer's rounding class (LinearTolerance)
}

void Filtering(render::GlDevice& backend,std::vector<uint8_t>& shown,unsigned& swaps) {
    render::SurfaceDesc surface;
    surface.width = 32; surface.height = 16; surface.renderable = true;
    const auto image = backend.CreateSurface(surface);
    std::vector<uint8_t> pixels(32*16*4),after(pixels.size());
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) {
        const auto color = Pattern(x,y);
        std::copy(color.begin(),color.end(),pixels.begin()+(y*32+x)*4);
    }
    backend.Upload(image,{0,0,32,16},pixels.data(),32*4);
    const auto texture = backend.Surface(image)->tex;
    for (GLint existing : {GL_NEAREST,GL_LINEAR}) {
        backend.State().ActiveTexture(GL_TEXTURE0);
        backend.State().BindTex2D(texture);
        backend.State().TexParami(GL_TEXTURE_MAG_FILTER,existing);
        backend.State().TexParami(GL_TEXTURE_MIN_FILTER,existing);
        backend.State().TexParami(GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        backend.State().TexParami(GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        for (bool flip : {true,false}) {
            for (const render::Rect source : {render::Rect{0,0,32,16},{4,3,16,8}}) {
              for (const render::Rect destination : {render::Rect{0,0,64,64},{8,12,16,8}}) {
                render::ScanoutDesc desc{image,source,destination,flip};
                for (auto filter : {render::PresentationFilter::Linear,render::PresentationFilter::Nearest,
                                     render::PresentationFilter::Surface}) {
                    desc.filter = filter;
                    const unsigned before = swaps;
                    backend.Present(desc);
                    CHECK(swaps == before+1);
                    if (filter == render::PresentationFilter::Linear ||
                        (filter == render::PresentationFilter::Surface && existing == GL_LINEAR))
                        CompareLinear(shown,desc);
                    else Compare(shown,desc);
                    GLint min = 0,mag = 0,sampler = -1;
                    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&min);
                    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&mag);
                    glGetIntegerv(GL_SAMPLER_BINDING,&sampler); // active texture unit 0
                    CHECK(min == existing && mag == existing && sampler == 0);
                }
              }
            }
        }
    }
    backend.Readback(image,{0,0,32,16},after.data(),32*4);
    CHECK(after == pixels && glGetError() == GL_NO_ERROR);
    const auto before_pixels = WindowPixels();
    const auto before_swaps = swaps;
    bool refused = false;
    try { backend.Present({image,{0,0,32,16},{0,0,64,64},true,
                           static_cast<render::PresentationFilter>(255)}); }
    catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused && swaps == before_swaps && WindowPixels() == before_pixels);
    backend.DestroySurface(image);
}
}

int main() {
    platform::Window window;
    platform::GlContext context;
    if (!window.Create(true,64,64,"Graphics presentation crop") || !context.Create(window))
        return 77;
    render::GlDevice backend;
    std::vector<uint8_t> shown;
    unsigned swaps = 0;
    CHECK(backend.Init([&] { shown = WindowPixels(); ++swaps; window.Swap(); }));
    backend.SetWindowSize(64,64);
    Mapping(backend,shown,swaps);
    Filtering(backend,shown,swaps);
    CHECK(glGetError() == GL_NO_ERROR);
    backend.Shutdown();
    std::printf("Render_Present: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
