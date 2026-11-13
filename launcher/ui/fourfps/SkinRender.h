// 4fps - render de skin por software con editor de poses (estilo Blockbench)
#pragma once

#include <QHash>
#include <QImage>
#include <QPolygonF>
#include <QWidget>
#include <vector>

enum SkinPart { PartHead = 0, PartBody, PartRightArm, PartLeftArm, PartRightLeg, PartLeftLeg, PartCount };

struct PartRot {
    double x = 0, y = 0, z = 0;  // grados
};

struct SkinRenderOptions {
    double yaw = 35;
    double pitch = 12;
    double zoom = 1.0;
    int background = 1;  // 0 transparente, 1 gris, 2 negro
    bool slim = false;
    bool showCape = true;
    bool shadow = true;
    PartRot parts[PartCount];
    int selected = -1;  // parte resaltada (solo en la vista)
    int shading = 1;    // 0 plano, 1 suave (estilo shader)
    int armor[4] = { 0, 0, 0, 0 };  // casco, peto, pantalones, botas: 0 nada, 1 cuero, 2 malla, 3 hierro, 4 oro, 5 diamante, 6 netherita
    QString heldRight;  // id del objeto en la mano derecha (texturas del juego)
    QString heldLeft;
};

struct PickFace {
    QPolygonF poly;
    int part;
};

class SkinRenderer {
   public:
    // texturas leidas del Minecraft instalado del jugador: "item/<id>", "armor/<material>/1|2"
    static void setGameTextures(const QHash<QString, QImage>& tex);
    static const QHash<QString, QImage>& gameTextures();
    static QStringList armorMaterials();  // ids en el orden de SkinRenderOptions::armor
    static QImage defaultSkin();
    static QString partName(int part);
    static void applyPreset(int pose, SkinRenderOptions& o);  // 0..5
    static void render(QPainter& p, const QRectF& area, const QImage& skin, const QImage& cape, const SkinRenderOptions& o,
                       std::vector<PickFace>* pick = nullptr);
    static QImage renderImage(int size, const QImage& skin, const QImage& cape, const SkinRenderOptions& o);
};

class SkinRenderView : public QWidget {
    Q_OBJECT
   public:
    explicit SkinRenderView(QWidget* parent = nullptr);
    SkinRenderOptions options;
    QImage skin;
    QImage cape;
    void setSkin(const QImage& s, const QImage& c);
    void selectPart(int part);

   signals:
    void poseEdited();
    void partSelected(int part);

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

   private:
    QPoint m_last;
    bool m_rotatingPart = false;
    std::vector<PickFace> m_pick;
};
