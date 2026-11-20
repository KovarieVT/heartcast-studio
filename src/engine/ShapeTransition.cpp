#include "engine/ShapeTransition.h"

#include <obs-module.h>
#include <obs.h>
#include <graphics/graphics.h>

#include <QtMath>
#include <cmath>
#include <vector>

namespace bc {

namespace {

// Same structure as OBS's luma wipe (sRGB-correct sampling, softness band), plus a
// glowing edge inside the softness band.
const char *kEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d a_tex;
uniform texture2d b_tex;
uniform texture2d l_tex;
uniform float progress;
uniform bool invert;
uniform float softness;
uniform float4 edge_color;
uniform float edge_strength;

sampler_state textureSampler {
	Filter    = Linear;
	AddressU  = Clamp;
	AddressV  = Clamp;
};

struct VertData {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertData VSDefault(VertData v_in)
{
	VertData vert_out;
	vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv  = v_in.uv;
	return vert_out;
}

float4 PSShapeWipe(VertData v_in) : TARGET
{
	float2 uv = v_in.uv;
	float4 a_color = a_tex.Sample(textureSampler, uv);
	float4 b_color = b_tex.Sample(textureSampler, uv);
	float luma = l_tex.Sample(textureSampler, uv).x;
	if (invert) {
		luma = 1.0 - luma;
	}
	float time = lerp(0.0, 1.0 + softness, progress);
	float4 rgba;
	if (luma <= time - softness) {
		rgba = b_color;
	} else if (luma >= time) {
		rgba = a_color;
	} else {
		float alpha = (time - luma) / softness;
		rgba = lerp(a_color, b_color, alpha);
		float glow = 1.0 - abs(alpha * 2.0 - 1.0);
		rgba.rgb = rgba.rgb + edge_color.rgb * (glow * glow * edge_strength);
	}
	return rgba;
}

technique ShapeWipe
{
	pass
	{
		vertex_shader = VSDefault(v_in);
		pixel_shader  = PSShapeWipe(v_in);
	}
}
)";

constexpr int kPatternW = 960, kPatternH = 540;

struct ShapeWipe {
    obs_source_t *source = nullptr;
    gs_effect_t *effect = nullptr;
    gs_eparam_t *pA = nullptr, *pB = nullptr, *pL = nullptr, *pProgress = nullptr, *pInvert = nullptr,
                *pSoftness = nullptr, *pEdge = nullptr, *pEdgeStrength = nullptr;
    gs_texture_t *luma = nullptr;
    QString shape;
    bool invert = false;
    float softness = 0.12f;
    vec4 edge{};
    float edgeStrength = 0.9f;
};

// ---------------------------------------------------------------- patterns
double hash2(int x, int y, int seed) {
    quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u + quint32(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return double(h ^ (h >> 16)) / 4294967295.0;
}
double valueNoise(double x, double y, int seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const double fx = x - xi, fy = y - yi;
    const double u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const double a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
double fbm(double x, double y, int seed) {
    double s = 0, amp = 0.5;
    for (int o = 0; o < 5; ++o) { s += amp * valueNoise(x, y, seed + o * 17); x *= 2.03; y *= 2.03; amp *= 0.5; }
    return s;   // ~[0, 1)
}

// Inside an equilateral lancet arch of width 1 standing on (0,0), y up.
bool inLancet(double x, double y, double heightScale) {
    if (y < 0 || std::abs(x) > 0.5) return false;
    const double spring = 0.55 * heightScale;   // straight jambs
    if (y <= spring) return true;
    const double cx = x >= 0 ? -0.5 : 0.5;      // arc centred on the opposite springing point
    const double dy = y - spring;
    return (x - cx) * (x - cx) + dy * dy <= 1.0;
}

void normalise(std::vector<double> &v) {
    double lo = 1e9, hi = -1e9;
    for (double x : v) { lo = std::min(lo, x); hi = std::max(hi, x); }
    const double span = hi - lo > 1e-9 ? hi - lo : 1.0;
    for (double &x : v) x = (x - lo) / span;
}

} // namespace

QStringList shapeTransitionShapes() {
    return {"cathedral", "moonrise", "bloodfall", "stainedglass", "thorns", "wingsweep"};
}

QImage shapeTransitionPattern(const QString &shape, int w, int h) {
    std::vector<double> v(size_t(w) * h);
    const double aspect = double(w) / h;
    // stained glass: jittered grid of shard centres
    struct Seed { double x, y, t; };
    std::vector<Seed> seeds;
    if (shape == "stainedglass")
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 12; ++gx)
                seeds.push_back({(gx + 0.15 + 0.7 * hash2(gx, gy, 3)) / 12.0 * aspect, (gy + 0.15 + 0.7 * hash2(gx, gy, 5)) / 7.0, hash2(gx, gy, 9)});
    // blood drips: a handful of narrow streams of random length
    struct Drip { double x, width, len; };
    std::vector<Drip> drips;
    if (shape == "bloodfall")
        for (int i = 0; i < 26; ++i) drips.push_back({hash2(i, 1, 21), 0.006 + 0.014 * hash2(i, 2, 21), 0.08 + 0.30 * hash2(i, 3, 21)});

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            const double nx = (px + 0.5) / w, ny = (py + 0.5) / h;   // 0..1, y down
            double val = 0;
            if (shape == "moonrise") {
                const double dx = (nx - 0.86) * aspect, dy = ny - 0.12;
                const double r = std::sqrt(dx * dx + dy * dy);
                val = r + 0.18 * (fbm(nx * 6, ny * 6, 4) - 0.5);
            } else if (shape == "bloodfall") {
                double d = 0.10 * fbm(nx * 9, 0.5, 11);
                for (const auto &dr : drips) {
                    const double k = (nx - dr.x) / dr.width;
                    d = std::max(d, dr.len * std::exp(-k * k));
                }
                val = ny - d;
            } else if (shape == "stainedglass") {
                const double x = nx * aspect, y = ny;
                double best = 1e9, second = 1e9; const Seed *s = nullptr;
                for (const auto &sd : seeds) {
                    const double dd = (x - sd.x) * (x - sd.x) + (y - sd.y) * (y - sd.y);
                    if (dd < best) { second = best; best = dd; s = &sd; } else if (dd < second) second = dd;
                }
                const double edge = std::sqrt(second) - std::sqrt(best);   // distance to the shard border
                val = 0.82 * s->t + 0.18 * (1.0 - std::min(1.0, edge * 12));
            } else if (shape == "thorns") {
                val = 0.72 * nx + 0.28 * fbm(nx * 7, ny * 7, 31) + 0.05 * std::abs(std::sin(ny * 40 + fbm(nx * 3, ny * 3, 2) * 8));
            } else if (shape == "wingsweep") {
                const double scallop = std::pow(std::abs(std::sin(ny * M_PI * 3.0)), 0.55);
                val = nx + 0.16 * scallop - 0.08 * ny;
            } else {   // cathedral: smallest arch (scaled from the bottom centre) containing the pixel
                const double x = (nx - 0.5) * aspect, y = 1.0 - ny;
                double lo = 0.0, hi = 8.0;
                for (int k = 0; k < 22; ++k) {
                    const double m = (lo + hi) / 2;
                    (inLancet(x / m, y / m, 1.0) ? hi : lo) = m;
                }
                val = hi;
            }
            v[size_t(py) * w + px] = val;
        }
    }
    normalise(v);
    QImage img(w, h, QImage::Format_Grayscale8);
    for (int py = 0; py < h; ++py) {
        uchar *row = img.scanLine(py);
        for (int px = 0; px < w; ++px) row[px] = uchar(std::lround(v[size_t(py) * w + px] * 255.0));
    }
    return img;
}

namespace {

const char *swName(void *) { return "KovaLive Shape Wipe"; }

void swUpdate(void *data, obs_data_t *settings) {
    auto *s = static_cast<ShapeWipe *>(data);
    const QString shape = QString::fromUtf8(obs_data_get_string(settings, "shape"));
    s->invert = obs_data_get_bool(settings, "invert");
    s->softness = float(qBound(0.01, obs_data_get_double(settings, "softness"), 0.6));
    const uint32_t c = uint32_t(obs_data_get_int(settings, "edge_color"));   // 0xAABBGGRR
    auto lin = [](uint32_t v) { const float f = float(v & 0xFF) / 255.f; return std::pow(f, 2.2f); };
    vec4_set(&s->edge, lin(c), lin(c >> 8), lin(c >> 16), 1.f);
    s->edgeStrength = obs_data_get_bool(settings, "edge_glow") ? float(obs_data_get_double(settings, "edge_strength")) : 0.f;

    if (shape == s->shape && s->luma) return;
    s->shape = shape;
    const QImage img = shapeTransitionPattern(shape, kPatternW, kPatternH);
    std::vector<uint8_t> px(size_t(kPatternW) * kPatternH);
    for (int y = 0; y < kPatternH; ++y) memcpy(px.data() + size_t(y) * kPatternW, img.constScanLine(y), size_t(kPatternW));
    const uint8_t *planes[] = {px.data()};
    obs_enter_graphics();
    if (s->luma) gs_texture_destroy(s->luma);
    s->luma = gs_texture_create(kPatternW, kPatternH, GS_R8, 1, planes, 0);
    obs_leave_graphics();
}

void *swCreate(obs_data_t *settings, obs_source_t *source) {
    char *err = nullptr;
    obs_enter_graphics();
    gs_effect_t *effect = gs_effect_create(kEffect, "kova_shape_wipe.effect", &err);
    obs_leave_graphics();
    if (!effect) {
        blog(LOG_ERROR, "[KovaLive] shape wipe effect failed to compile: %s", err ? err : "(no message)");
        bfree(err);
        return nullptr;
    }
    bfree(err);
    auto *s = new ShapeWipe;
    s->source = source;
    s->effect = effect;
    s->pA = gs_effect_get_param_by_name(effect, "a_tex");
    s->pB = gs_effect_get_param_by_name(effect, "b_tex");
    s->pL = gs_effect_get_param_by_name(effect, "l_tex");
    s->pProgress = gs_effect_get_param_by_name(effect, "progress");
    s->pInvert = gs_effect_get_param_by_name(effect, "invert");
    s->pSoftness = gs_effect_get_param_by_name(effect, "softness");
    s->pEdge = gs_effect_get_param_by_name(effect, "edge_color");
    s->pEdgeStrength = gs_effect_get_param_by_name(effect, "edge_strength");
    swUpdate(s, settings);
    return s;
}

void swDestroy(void *data) {
    auto *s = static_cast<ShapeWipe *>(data);
    obs_enter_graphics();
    if (s->luma) gs_texture_destroy(s->luma);
    gs_effect_destroy(s->effect);
    obs_leave_graphics();
    delete s;
}

void swCallback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy) {
    auto *s = static_cast<ShapeWipe *>(data);
    const bool previous = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(true);
    gs_effect_set_texture_srgb(s->pA, a);
    gs_effect_set_texture_srgb(s->pB, b);
    gs_effect_set_texture(s->pL, s->luma);
    gs_effect_set_float(s->pProgress, t);
    gs_effect_set_bool(s->pInvert, s->invert);
    gs_effect_set_float(s->pSoftness, s->softness);
    gs_effect_set_vec4(s->pEdge, &s->edge);
    gs_effect_set_float(s->pEdgeStrength, s->edgeStrength);
    while (gs_effect_loop(s->effect, "ShapeWipe")) gs_draw_sprite(nullptr, 0, cx, cy);
    gs_enable_framebuffer_srgb(previous);
}

void swRender(void *data, gs_effect_t *) {
    auto *s = static_cast<ShapeWipe *>(data);
    obs_transition_video_render(s->source, swCallback);
}

float mixA(void *, float t) { return 1.0f - t; }
float mixB(void *, float t) { return t; }

bool swAudio(void *data, uint64_t *ts_out, obs_source_audio_mix *audio, uint32_t mixers, size_t channels, size_t sample_rate) {
    auto *s = static_cast<ShapeWipe *>(data);
    return obs_transition_audio_render(s->source, ts_out, audio, mixers, channels, sample_rate, mixA, mixB);
}

gs_color_space swColorSpace(void *data, size_t, const gs_color_space *) {
    return obs_transition_video_get_color_space(static_cast<ShapeWipe *>(data)->source);
}

void swDefaults(obs_data_t *st) {
    obs_data_set_default_string(st, "shape", "cathedral");
    obs_data_set_default_double(st, "softness", 0.12);
    obs_data_set_default_bool(st, "invert", false);
    obs_data_set_default_bool(st, "edge_glow", true);
    obs_data_set_default_int(st, "edge_color", 0xFF321B9E);   // crimson, 0xAABBGGRR
    obs_data_set_default_double(st, "edge_strength", 0.9);
}

obs_properties_t *swProperties(void *) {
    obs_properties_t *p = obs_properties_create();
    obs_property_t *list = obs_properties_add_list(p, "shape", "Shape", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list, "Cathedral Arch", "cathedral");
    obs_property_list_add_string(list, "Moonrise", "moonrise");
    obs_property_list_add_string(list, "Bloodfall", "bloodfall");
    obs_property_list_add_string(list, "Stained Glass", "stainedglass");
    obs_property_list_add_string(list, "Creeping Thorns", "thorns");
    obs_property_list_add_string(list, "Wing Sweep", "wingsweep");
    obs_properties_add_float_slider(p, "softness", "Edge softness", 0.02, 0.5, 0.01);
    obs_properties_add_bool(p, "edge_glow", "Glowing edge");
    obs_properties_add_color(p, "edge_color", "Glow colour");
    obs_properties_add_float_slider(p, "edge_strength", "Glow strength", 0.0, 2.0, 0.05);
    obs_properties_add_bool(p, "invert", "Reverse direction");
    return p;
}

obs_source_info g_info{};
bool g_registered = false;

} // namespace

void registerShapeTransition() {
    if (g_registered) return;
    g_info.id = kShapeTransitionId;
    g_info.type = OBS_SOURCE_TYPE_TRANSITION;
    g_info.get_name = swName;
    g_info.create = swCreate;
    g_info.destroy = swDestroy;
    g_info.update = swUpdate;
    g_info.video_render = swRender;
    g_info.audio_render = swAudio;
    g_info.get_defaults = swDefaults;
    g_info.get_properties = swProperties;
    g_info.video_get_color_space = swColorSpace;
    obs_register_source(&g_info);
    g_registered = true;
}

void unregisterShapeTransition() { g_registered = false; }

} // namespace bc
