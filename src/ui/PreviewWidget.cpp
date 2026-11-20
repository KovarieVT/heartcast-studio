#include "ui/PreviewWidget.h"
#include "engine/Engine.h"

#include <QKeyEvent>
#include <QTimer>
#include <QMouseEvent>
#include <QWindow>
#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#ifdef __linux__
#include <obs-nix-platform.h>
#endif

namespace bc {

// ---------------------------------------------------------------- transform
ItemTransform ItemTransform::read(obs_sceneitem_t *item) {
    ItemTransform t;
    obs_sceneitem_get_pos(item, &t.pos);
    obs_sceneitem_get_scale(item, &t.scale);
    obs_sceneitem_get_bounds(item, &t.bounds);
    t.rot = obs_sceneitem_get_rot(item);
    t.alignment = obs_sceneitem_get_alignment(item);
    t.boundsAlign = obs_sceneitem_get_bounds_alignment(item);
    t.boundsType = obs_sceneitem_get_bounds_type(item);
    obs_sceneitem_get_crop(item, &t.crop);
    return t;
}

void ItemTransform::apply(obs_sceneitem_t *item) const {
    obs_sceneitem_defer_update_begin(item);
    obs_sceneitem_set_pos(item, &pos);
    obs_sceneitem_set_scale(item, &scale);
    obs_sceneitem_set_rot(item, rot);
    obs_sceneitem_set_alignment(item, alignment);
    obs_sceneitem_set_bounds_type(item, boundsType);
    obs_sceneitem_set_bounds(item, &bounds);
    obs_sceneitem_set_bounds_alignment(item, boundsAlign);
    obs_sceneitem_set_crop(item, &crop);
    obs_sceneitem_defer_update_end(item);
}

namespace {
class TransformCommand : public QUndoCommand {
public:
    TransformCommand(const QString &scene, int64_t itemId, const ItemTransform &b, const ItemTransform &a, const QString &text)
        : QUndoCommand(text), m_scene(scene), m_id(itemId), m_before(b), m_after(a) {}
    void undo() override { apply(m_before); }
    void redo() override { if (m_first) { m_first = false; return; } apply(m_after); }
private:
    void apply(const ItemTransform &t) {
        if (obs_scene_t *sc = Engine::instance().scene(m_scene))
            if (obs_sceneitem_t *it = obs_scene_find_sceneitem_by_id(sc, m_id)) t.apply(it);
    }
    QString m_scene; int64_t m_id; ItemTransform m_before, m_after; bool m_first = true;
};
} // namespace

// ---------------------------------------------------------------- widget
PreviewWidget::PreviewWidget(Mode mode, QWidget *parent) : QWidget(parent), m_mode(mode) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_StaticContents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);
    connect(&Engine::instance(), &Engine::sceneItemsChanged, this, [this](const QString &s) {
        if (s != m_scene) return;
        obs_sceneitem_t *sel = selectedItem();
        obs_scene_t *sc = Engine::instance().scene(m_scene);
        if (sel && (!sc || !obs_scene_find_sceneitem_by_id(sc, obs_sceneitem_get_id(sel)))) selectItem(nullptr);
    });
}

PreviewWidget::~PreviewWidget() {
    if (m_display) {
        obs_display_remove_draw_callback(m_display, drawCallback, this);
        obs_display_destroy(m_display);
    }
}

void PreviewWidget::setSceneName(const QString &name) {
    if (name == m_scene) return;
    m_scene = name;
    selectItem(nullptr);
    m_undo.clear();
}

obs_sceneitem_t *PreviewWidget::selectedItem() const {
    QMutexLocker l(&m_selMutex);
    return m_selected;
}

void PreviewWidget::selectItem(obs_sceneitem_t *item) {
    {
        QMutexLocker l(&m_selMutex);
        if (m_selected.Get() == item) return;
        m_selected = item;
    }
    Q_EMIT itemSelected(item);
}

void PreviewWidget::pushTransformUndo(obs_sceneitem_t *item, const ItemTransform &before, const QString &text) {
    m_undo.push(new TransformCommand(m_scene, obs_sceneitem_get_id(item), before, ItemTransform::read(item), text));
}

bool PreviewWidget::event(QEvent *e) {
    if (e->type() == QEvent::PlatformSurface || e->type() == QEvent::WinIdChange) {
        // Surface recreated (e.g. docked/undocked) — rebuild the display.
        if (m_display && isVisible()) {
            obs_display_remove_draw_callback(m_display, drawCallback, this);
            obs_display_destroy(m_display);
            m_display = nullptr;
            createDisplay();
        }
    }
    return QWidget::event(e);
}

void PreviewWidget::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    // Defer until layouts have given us our real size, otherwise the swap chain
    // is created at the default 640×480.
    QTimer::singleShot(0, this, &PreviewWidget::createDisplay);
}

void PreviewWidget::createDisplay() {
    if (m_display || !Engine::instance().isRunning() || !windowHandle() || !isVisible()) return;
    const qreal dpr = devicePixelRatioF();
    gs_init_data info{};
    info.cx = uint32_t(width() * dpr);
    info.cy = uint32_t(height() * dpr);
    info.format = GS_BGRA;
    info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
    info.window.hwnd = reinterpret_cast<void *>(winId());
#elif defined(__linux__)
    info.window.id = uint32_t(winId());
    info.window.display = obs_get_nix_platform_display();
#endif
    m_display = obs_display_create(&info, 0x0D0A0B);   // #0B0A0D (r in low byte)
    if (m_display) obs_display_add_draw_callback(m_display, drawCallback, this);
}

void PreviewWidget::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (!m_display) return;
    const qreal dpr = devicePixelRatioF();
    obs_display_resize(m_display, uint32_t(width() * dpr), uint32_t(height() * dpr));
#ifdef __linux__
    // Some X11/EGL drivers (software GL, Xvfb) don't grow the window surface on
    // resize; recreate the display once resizing settles. (D3D11 resizes in place.)
    if (!m_recreate) {
        m_recreate = new QTimer(this);
        m_recreate->setSingleShot(true);
        connect(m_recreate, &QTimer::timeout, this, [this] {
            if (!m_display) return;
            obs_display_remove_draw_callback(m_display, drawCallback, this);
            obs_display_destroy(m_display);
            m_display = nullptr;
            createDisplay();
        });
    }
    m_recreate->start(150);
#endif
}

void PreviewWidget::computeViewport(uint32_t cx, uint32_t cy, int &x, int &y, int &w, int &h, float &scale) const {
    obs_video_info ovi{};
    obs_get_video_info(&ovi);
    const float bw = float(ovi.base_width ? ovi.base_width : 1920), bh = float(ovi.base_height ? ovi.base_height : 1080);
    const float pad = 8.f;
    const float sx = (float(cx) - 2 * pad) / bw, sy = (float(cy) - 2 * pad) / bh;
    scale = qMax(0.01f, qMin(sx, sy));
    w = int(bw * scale); h = int(bh * scale);
    x = int((float(cx) - float(w)) / 2); y = int((float(cy) - float(h)) / 2);
}

QPointF PreviewWidget::toCanvas(const QPointF &p) const {
    const qreal dpr = devicePixelRatioF();
    int x, y, w, h; float s;
    computeViewport(uint32_t(width() * dpr), uint32_t(height() * dpr), x, y, w, h, s);
    return {(p.x() * dpr - x) / s, (p.y() * dpr - y) / s};
}

// ---------------------------------------------------------------- render (graphics thread)
static void drawLines(const QList<QPointF> &pts, bool closed, uint32_t rgba) {
    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
    vec4 c; vec4_from_rgba(&c, rgba);
    gs_effect_set_vec4(color, &c);
    while (gs_effect_loop(solid, "Solid")) {
        gs_render_start(true);
        for (const auto &p : pts) gs_vertex2f(float(p.x()), float(p.y()));
        if (closed && !pts.isEmpty()) gs_vertex2f(float(pts.first().x()), float(pts.first().y()));
        gs_render_stop(GS_LINESTRIP);
    }
}

static void drawSquare(const QPointF &c, float half, uint32_t rgba) {
    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    vec4 col; vec4_from_rgba(&col, rgba);
    gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &col);
    while (gs_effect_loop(solid, "Solid")) {
        gs_render_start(true);
        gs_vertex2f(float(c.x()) - half, float(c.y()) - half);
        gs_vertex2f(float(c.x()) + half, float(c.y()) - half);
        gs_vertex2f(float(c.x()) - half, float(c.y()) + half);
        gs_vertex2f(float(c.x()) + half, float(c.y()) + half);
        gs_render_stop(GS_TRISTRIP);
    }
}

static QList<QPointF> itemCorners(obs_sceneitem_t *item) {
    matrix4 m; obs_sceneitem_get_box_transform(item, &m);
    QList<QPointF> out;
    for (auto [u, v] : {std::pair{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}}) {
        vec3 in, o; vec3_set(&in, u, v, 0); vec3_transform(&o, &in, &m);
        out << QPointF(o.x, o.y);
    }
    return out;
}

// colours are ABGR-packed for vec4_from_rgba (r in low byte)
static constexpr uint32_t kSelect = 0xFF3F30E0;   // #E0303F blood-red selection
static constexpr uint32_t kGuide = 0xFF5A93B8;    // #B8935A old-gold guides
static constexpr uint32_t kSafe = 0x66B8AEB4;     // translucent moonsilver

void PreviewWidget::drawOverlays(float scale) {
    obs_video_info ovi{}; obs_get_video_info(&ovi);
    const float cw = float(ovi.base_width), ch = float(ovi.base_height);
    if (m_safeAreas) {
        for (float f : {0.05f, 0.10f})
            drawLines({{cw * f, ch * f}, {cw * (1 - f), ch * f}, {cw * (1 - f), ch * (1 - f)}, {cw * f, ch * (1 - f)}}, true, kSafe);
        drawLines({{cw / 2, ch / 2 - 20}, {cw / 2, ch / 2 + 20}}, false, kSafe);
        drawLines({{cw / 2 - 20, ch / 2}, {cw / 2 + 20, ch / 2}}, false, kSafe);
    }
    QMutexLocker l(&m_selMutex);
    if (m_selected) {
        const auto pts = itemCorners(m_selected);
        drawLines(pts, true, kSelect);
        const float half = 5.f / scale;
        for (const auto &p : pts) drawSquare(p, half, kSelect);
    }
    for (const auto &g : m_guides) drawLines({g.p1(), g.p2()}, false, kGuide);
}

void PreviewWidget::drawCallback(void *data, uint32_t cx, uint32_t cy) {
    auto *self = static_cast<PreviewWidget *>(data);
    int x, y, w, h; float scale;
    self->computeViewport(cx, cy, x, y, w, h, scale);
    obs_video_info ovi{}; obs_get_video_info(&ovi);

    gs_viewport_push();
    gs_projection_push();
    gs_ortho(0.f, float(ovi.base_width), 0.f, float(ovi.base_height), -100.f, 100.f);
    gs_set_viewport(x, y, w, h);

    if (self->m_mode == Program) {
        obs_render_main_texture();
    } else {
        OBSSourceAutoRelease src = obs_get_source_by_name(self->m_scene.toUtf8().constData());
        if (src) obs_source_video_render(src);
        self->drawOverlays(scale);
    }
    gs_projection_pop();
    gs_viewport_pop();
}

// ---------------------------------------------------------------- interaction
obs_sceneitem_t *PreviewWidget::hitTest(const QPointF &pt) const {
    obs_scene_t *sc = Engine::instance().scene(m_scene);
    if (!sc) return nullptr;
    struct Ctx { QPointF pt; obs_sceneitem_t *hit = nullptr; } ctx{pt};
    obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *item, void *p) {
        auto *c = static_cast<Ctx *>(p);
        if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item)) return true;
        matrix4 m, inv; obs_sceneitem_get_box_transform(item, &m);
        if (!matrix4_inv(&inv, &m)) return true;
        vec3 in, o; vec3_set(&in, float(c->pt.x()), float(c->pt.y()), 0); vec3_transform(&o, &in, &inv);
        if (o.x >= 0 && o.x <= 1 && o.y >= 0 && o.y <= 1) c->hit = item;   // later = higher in z-order
        return true;
    }, &ctx);
    return ctx.hit;
}

int PreviewWidget::hitHandle(const QPointF &pt) const {
    obs_sceneitem_t *sel = selectedItem();
    if (!sel) return -1;
    const qreal dpr = devicePixelRatioF();
    int x, y, w, h; float s;
    computeViewport(uint32_t(width() * dpr), uint32_t(height() * dpr), x, y, w, h, s);
    const double tol = 10.0 / s;
    const auto pts = itemCorners(sel);
    for (int i = 0; i < pts.size(); ++i)
        if (QLineF(pts[i], pt).length() <= tol) return i;
    return -1;
}

void PreviewWidget::mousePressEvent(QMouseEvent *e) {
    if (m_mode != Edit || e->button() != Qt::LeftButton) return;
    setFocus();
    const QPointF cp = toCanvas(e->position());
    m_handle = hitHandle(cp);
    obs_sceneitem_t *item = m_handle >= 0 ? selectedItem() : hitTest(cp);
    selectItem(item);
    if (!item) return;
    m_before = ItemTransform::read(item);
    m_dragStart = cp;
    if (m_handle >= 0 && !obs_sceneitem_locked(item)) {
        m_drag = Drag::Resize;
        const auto pts = itemCorners(item);
        const QPointF fixed = pts[(m_handle + 2) % 4];
        vec3_set(&m_fixedCorner, float(fixed.x()), float(fixed.y()), 0);
        m_startW = float(QLineF(pts[0], pts[1]).length());
        m_startH = float(QLineF(pts[1], pts[2]).length());
    } else {
        m_drag = Drag::Move;
    }
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_mode != Edit) return;
    const QPointF cp = toCanvas(e->position());
    if (m_drag == Drag::None) {
        setCursor(hitHandle(cp) >= 0 ? Qt::SizeFDiagCursor : hitTest(cp) ? Qt::SizeAllCursor : Qt::ArrowCursor);
        return;
    }
    obs_sceneitem_t *item = selectedItem();
    if (!item) return;
    obs_video_info ovi{}; obs_get_video_info(&ovi);
    const float cw = float(ovi.base_width), ch = float(ovi.base_height);
    QList<QLineF> guides;

    if (m_drag == Drag::Move) {
        QPointF delta = cp - m_dragStart;
        vec2 pos = m_before.pos;
        pos.x += float(delta.x()); pos.y += float(delta.y());
        obs_sceneitem_set_pos(item, &pos);
        if (m_snap && !(e->modifiers() & Qt::AltModifier)) {
            const auto pts = itemCorners(item);
            double l = 1e9, r = -1e9, t = 1e9, b = -1e9;
            for (auto &p : pts) { l = qMin(l, p.x()); r = qMax(r, p.x()); t = qMin(t, p.y()); b = qMax(b, p.y()); }
            const double tol = 12.0 * cw / qMax(1, width());
            double dx = 0, dy = 0;
            auto trySnap = [&](double v, double target, double &d, bool vertical) {
                if (d == 0 && std::abs(v - target) < tol) {
                    d = target - v;
                    guides << (vertical ? QLineF(target, 0, target, ch) : QLineF(0, target, cw, target));
                }
            };
            for (double tx : {0.0, double(cw) / 2, double(cw)}) { trySnap(l, tx, dx, true); trySnap(r, tx, dx, true); trySnap((l + r) / 2, tx, dx, true); }
            for (double ty : {0.0, double(ch) / 2, double(ch)}) { trySnap(t, ty, dy, false); trySnap(b, ty, dy, false); trySnap((t + b) / 2, ty, dy, false); }
            pos.x += float(dx); pos.y += float(dy);
            obs_sceneitem_set_pos(item, &pos);
        }
    } else if (m_drag == Drag::Resize && m_startW > 0 && m_startH > 0) {
        const double nw = std::abs(cp.x() - m_fixedCorner.x), nh = std::abs(cp.y() - m_fixedCorner.y);
        double fx = nw / m_startW, fy = nh / m_startH;
        if (!(e->modifiers() & Qt::ShiftModifier)) fx = fy = qMax(fx, fy);   // keep aspect unless Shift
        fx = qMax(0.01, fx); fy = qMax(0.01, fy);
        if (m_before.boundsType != OBS_BOUNDS_NONE) {
            vec2 b{float(m_before.bounds.x * fx), float(m_before.bounds.y * fy)};
            obs_sceneitem_set_bounds(item, &b);
        } else {
            vec2 sc{float(m_before.scale.x * fx), float(m_before.scale.y * fy)};
            obs_sceneitem_set_scale(item, &sc);
        }
        obs_sceneitem_force_update_transform(item);
        // keep the opposite corner pinned
        const auto pts = itemCorners(item);
        const QPointF nowFixed = pts[(m_handle + 2) % 4];
        vec2 pos; obs_sceneitem_get_pos(item, &pos);
        pos.x += m_fixedCorner.x - float(nowFixed.x());
        pos.y += m_fixedCorner.y - float(nowFixed.y());
        obs_sceneitem_set_pos(item, &pos);
    }
    QMutexLocker l(&m_selMutex);
    m_guides = guides;
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *) {
    if (m_drag != Drag::None) {
        if (obs_sceneitem_t *item = selectedItem())
            pushTransformUndo(item, m_before, m_drag == Drag::Move ? tr("Move source") : tr("Resize source"));
    }
    m_drag = Drag::None;
    QMutexLocker l(&m_selMutex);
    m_guides.clear();
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    if (m_mode != Edit) return;
    if (obs_sceneitem_t *item = hitTest(toCanvas(e->position()))) Q_EMIT itemDoubleClicked(item);
}

void PreviewWidget::keyPressEvent(QKeyEvent *e) {
    obs_sceneitem_t *item = selectedItem();
    if (m_mode != Edit || !item || obs_sceneitem_locked(item)) return QWidget::keyPressEvent(e);
    const float step = (e->modifiers() & Qt::ShiftModifier) ? 10.f : 1.f;
    vec2 d{0, 0};
    switch (e->key()) {
    case Qt::Key_Left: d.x = -step; break;
    case Qt::Key_Right: d.x = step; break;
    case Qt::Key_Up: d.y = -step; break;
    case Qt::Key_Down: d.y = step; break;
    default: return QWidget::keyPressEvent(e);
    }
    const ItemTransform before = ItemTransform::read(item);
    vec2 pos = before.pos; pos.x += d.x; pos.y += d.y;
    obs_sceneitem_set_pos(item, &pos);
    pushTransformUndo(item, before, tr("Nudge source"));
}

} // namespace bc
