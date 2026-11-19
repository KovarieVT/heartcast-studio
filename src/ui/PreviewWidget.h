#pragma once
#include <QMutex>
#include <QTimer>
#include <QPointF>
#include <QUndoStack>
#include <QWidget>
#include <obs.hpp>

namespace bc {

struct ItemTransform {
    vec2 pos{}, scale{}, bounds{};
    float rot = 0;
    uint32_t alignment = 0, boundsAlign = 0;
    obs_bounds_type boundsType = OBS_BOUNDS_NONE;
    obs_sceneitem_crop crop{};
    static ItemTransform read(obs_sceneitem_t *item);
    void apply(obs_sceneitem_t *item) const;
};

// Native libobs render surface. In "Edit" mode it renders the preview scene and
// supports click-select, drag-move, corner-resize, edge/center snapping with
// guides, safe-area overlays, arrow-key nudging and undo/redo.
// In "Program" mode it shows exactly what is being streamed/recorded.
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    enum Mode { Edit, Program };
    explicit PreviewWidget(Mode mode, QWidget *parent = nullptr);
    ~PreviewWidget() override;

    void setSceneName(const QString &name);
    QString sceneName() const { return m_scene; }
    obs_sceneitem_t *selectedItem() const;
    void selectItem(obs_sceneitem_t *item);
    QUndoStack *undoStack() { return &m_undo; }

    void setSafeAreas(bool on) { m_safeAreas = on; }
    void setSnapping(bool on) { m_snap = on; }
    bool safeAreas() const { return m_safeAreas; }
    bool snapping() const { return m_snap; }

    // Pushes an undoable transform change (used by the transform dialog too).
    void pushTransformUndo(obs_sceneitem_t *item, const ItemTransform &before, const QString &text);

    QPaintEngine *paintEngine() const override { return nullptr; }

Q_SIGNALS:
    void itemSelected(obs_sceneitem_t *item);
    void itemDoubleClicked(obs_sceneitem_t *item);

protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override {}
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *) override;

private:
    static void drawCallback(void *data, uint32_t cx, uint32_t cy);
    void createDisplay();
    void computeViewport(uint32_t cx, uint32_t cy, int &x, int &y, int &w, int &h, float &scale) const;
    QPointF toCanvas(const QPointF &widgetPos) const;
    obs_sceneitem_t *hitTest(const QPointF &canvasPt) const;
    int hitHandle(const QPointF &canvasPt) const;  // -1 none, 0..3 corners TL TR BR BL
    void drawOverlays(float scale);

    Mode m_mode;
    QString m_scene;
    obs_display_t *m_display = nullptr;
    QTimer *m_recreate = nullptr;
    QUndoStack m_undo;

    mutable QMutex m_selMutex;
    OBSSceneItem m_selected;

    // drag state
    enum class Drag { None, Move, Resize } m_drag = Drag::None;
    int m_handle = -1;
    QPointF m_dragStart;
    ItemTransform m_before;
    vec3 m_fixedCorner{};
    float m_startW = 0, m_startH = 0;
    QList<QLineF> m_guides;   // canvas-space snap guides (guarded by m_selMutex)
    bool m_safeAreas = false, m_snap = true;
};

} // namespace bc
