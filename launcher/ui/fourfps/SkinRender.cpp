// 4fps - render de skin por software (vistas, poses, fondo, capa, sombra)
#include "SkinRender.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <vector>

namespace {
struct V3 {
    double x, y, z;
};

struct Xform {
    V3 pivot{ 0, 0, 0 };
    PartRot rot;
    bool hasParent = false;
    V3 parentPivot{ 0, 0, 0 };
    PartRot parentRot;
};

V3 rotX(V3 p, double deg)
{
    double a = qDegreesToRadians(deg), c = qCos(a), s = qSin(a);
    return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
}
V3 rotY(V3 p, double deg)
{
    double a = qDegreesToRadians(deg), c = qCos(a), s = qSin(a);
    return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
}
V3 rotZ(V3 p, double deg)
{
    double a = qDegreesToRadians(deg), c = qCos(a), s = qSin(a);
    return { p.x * c - p.y * s, p.x * s + p.y * c, p.z };
}

struct Face {
    QPointF tl, tr, bl;
    double depth;
    QImage tex;
    int part;
    bool solid = true;
};

bool mostlyOpaque(const QImage& img)
{
    int opaque = 0;
    for (int y = 0; y < img.height(); y++)
        for (int x = 0; x < img.width(); x++)
            if (qAlpha(img.pixel(x, y)) > 200)
                opaque++;
    return opaque * 10 >= img.width() * img.height() * 9;
}

V3 rotateAround(V3 p, V3 pivot, const PartRot& r)
{
    V3 q = { p.x - pivot.x, p.y - pivot.y, p.z - pivot.z };
    q = rotZ(q, r.z);
    q = rotY(q, r.y);
    q = rotX(q, r.x);
    return { q.x + pivot.x, q.y + pivot.y, q.z + pivot.z };
}

QImage shade(const QImage& src, double f, double gradient = 0.0)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    if (f >= 0.999 && gradient <= 0.0)
        return img;
    const int h = qMax(1, img.height() - 1);
    for (int y = 0; y < img.height(); y++) {
        // degradado vertical: arriba un poco mas claro, abajo un poco mas oscuro
        const double k = f * (1.0 + gradient * (0.5 - double(y) / h));
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); x++) {
            QRgb c = line[x];
            line[x] = qRgba(qMin(255, int(qRed(c) * k)), qMin(255, int(qGreen(c) * k)), qMin(255, int(qBlue(c) * k)), qAlpha(c));
        }
    }
    return img;
}

QHash<QString, QImage>& gameTex()
{
    static QHash<QString, QImage> t;
    return t;
}

struct Ctx {
    const SkinRenderOptions* o;
    QPointF center;
    double scale;
    std::vector<Face> faces;

    QPointF project(V3 p, double& depthOut) const
    {
        V3 v = rotY(p, o->yaw);
        v = rotX(v, o->pitch);
        depthOut = v.z;
        return { center.x() + v.x * scale, center.y() - (v.y - 16.0) * scale };
    }

    V3 viewDir(V3 p) const { return rotX(rotY(p, o->yaw), o->pitch); }

    Face makeFace(const QImage& tex, V3 w0, V3 w1, V3 w2, QPointF a, QPointF b, QPointF c, double depth, double flatLight, int part) const
    {
        double light = flatLight;
        if (o->shading == 1) {
            // luz direccional (arriba-izquierda-adelante) + ambiente
            V3 e1{ w1.x - w0.x, w1.y - w0.y, w1.z - w0.z }, e2{ w2.x - w0.x, w2.y - w0.y, w2.z - w0.z };
            V3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
            n = viewDir(n);
            const double len = qSqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len > 0) {
                n = { n.x / len, n.y / len, n.z / len };
                const V3 L{ -0.45, 0.70, 0.55 };
                const double diffuse = qMax(0.0, n.x * L.x + n.y * L.y + n.z * L.z);
                light = 0.66 + 0.48 * diffuse;
            }
        }
        return Face{ a, b, c, depth, shade(tex, light, o->shading == 1 ? 0.18 : 0.0), part, o->shading == 1 && mostlyOpaque(tex) };
    }

    // plano con textura (objetos en la mano); se ve por ambos lados
    void quad(const QImage& tex, V3 tl, V3 tr, V3 bl, const Xform& xf, int part)
    {
        if (tex.isNull())
            return;
        auto tf = [&](V3 p) {
            V3 r = rotateAround(p, xf.pivot, xf.rot);
            if (xf.hasParent)
                r = rotateAround(r, xf.parentPivot, xf.parentRot);
            return r;
        };
        V3 br{ tr.x + bl.x - tl.x, tr.y + bl.y - tl.y, tr.z + bl.z - tl.z };
        const QImage mirrored = tex.mirrored(true, false);
        struct Side {
            V3 a, b, c;
            const QImage* t;
        } sides[2] = { { tl, tr, bl, &tex }, { tr, tl, br, &mirrored } };
        for (auto& sd : sides) {
            double d1, d2, d3;
            V3 w0 = tf(sd.a), w1 = tf(sd.b), w2 = tf(sd.c);
            QPointF a = project(w0, d1), b = project(w1, d2), c = project(w2, d3);
            QPointF e1 = b - a, e2 = c - a;
            if (e1.x() * e2.y() - e1.y() * e2.x() <= 0)
                continue;
            faces.push_back(makeFace(*sd.t, w0, w1, w2, a, b, c, (d2 + d3) / 2.0 + 0.05, 0.9, part));
        }
    }

    // caja con textura estilo Minecraft: origen (u,v), tamano (w,h,d) en pixeles de textura
    void box(const QImage& tex, double x0, double y0, double z0, double w, double h, double d, int u, int v, const Xform& xf, int part,
             double inflate, bool swapFrontBack = false, int texScale = 1)
    {
        if (tex.isNull())
            return;
        double X0 = x0 - inflate, X1 = x0 + w + inflate;
        double Y0 = y0 - inflate, Y1 = y0 + h + inflate;
        double Z0 = z0 - inflate, Z1 = z0 + d + inflate;
        auto tf = [&](V3 p) {
            V3 r = rotateAround(p, xf.pivot, xf.rot);
            if (xf.hasParent)
                r = rotateAround(r, xf.parentPivot, xf.parentRot);
            return r;
        };
        const int W = int(w), H = int(h), D = int(d);
        struct FDef {
            QRect src;
            V3 tl, tr, bl;
            double light;
        };
        QRect front(u + D, v + D, W, H), back(u + D + W + D, v + D, W, H);
        if (swapFrontBack)
            std::swap(front, back);
        FDef defs[6] = {
            { front, { X0, Y1, Z1 }, { X1, Y1, Z1 }, { X0, Y0, Z1 }, 0.95 },
            { back, { X1, Y1, Z0 }, { X0, Y1, Z0 }, { X1, Y0, Z0 }, 0.72 },
            { QRect(u, v + D, D, H), { X0, Y1, Z0 }, { X0, Y1, Z1 }, { X0, Y0, Z0 }, 0.80 },
            { QRect(u + D + W, v + D, D, H), { X1, Y1, Z1 }, { X1, Y1, Z0 }, { X1, Y0, Z1 }, 0.80 },
            { QRect(u + D, v, W, D), { X0, Y1, Z0 }, { X1, Y1, Z0 }, { X0, Y1, Z1 }, 1.0 },
            { QRect(u + D + W, v, W, D), { X0, Y0, Z1 }, { X1, Y0, Z1 }, { X0, Y0, Z0 }, 0.6 },
        };
        for (auto& f : defs) {
            if (texScale > 1)
                f.src = QRect(f.src.x() * texScale, f.src.y() * texScale, f.src.width() * texScale, f.src.height() * texScale);
            if (!tex.rect().contains(f.src))
                continue;
            double d1, d2, d3;
            QPointF a = project(tf(f.tl), d1), b = project(tf(f.tr), d2), c = project(tf(f.bl), d3);
            QPointF e1 = b - a, e2 = c - a;
            if (e1.x() * e2.y() - e1.y() * e2.x() <= 0)
                continue;  // cara de espaldas
            faces.push_back(makeFace(tex.copy(f.src), tf(f.tl), tf(f.tr), tf(f.bl), a, b, c, (d2 + d3) / 2.0, f.light, part));
        }
    }
};

}  // namespace

void SkinRenderer::setGameTextures(const QHash<QString, QImage>& tex)
{
    gameTex() = tex;
}

const QHash<QString, QImage>& SkinRenderer::gameTextures()
{
    return gameTex();
}

QStringList SkinRenderer::armorMaterials()
{
    return { "leather", "chainmail", "iron", "gold", "diamond", "netherite" };
}

QString SkinRenderer::partName(int part)
{
    switch (part) {
        case PartHead:
            return QObject::tr("Cabeza");
        case PartBody:
            return QObject::tr("Torso");
        case PartRightArm:
            return QObject::tr("Brazo derecho");
        case PartLeftArm:
            return QObject::tr("Brazo izquierdo");
        case PartRightLeg:
            return QObject::tr("Pierna derecha");
        case PartLeftLeg:
            return QObject::tr("Pierna izquierda");
        default:
            return QString();
    }
}

void SkinRenderer::applyPreset(int pose, SkinRenderOptions& o)
{
    for (auto& r : o.parts)
        r = PartRot{};
    auto set = [&o](int part, double x, double y, double z) { o.parts[part] = PartRot{ x, y, z }; };
    // x negativo = hacia adelante
    switch (pose) {
        case 0:  // de pie
            set(PartRightArm, 0, 0, -3);
            set(PartLeftArm, 0, 0, 3);
            break;
        case 1:  // caminando
            set(PartRightArm, -30, 0, -3);
            set(PartLeftArm, 30, 0, 3);
            set(PartRightLeg, 30, 0, 0);
            set(PartLeftLeg, -30, 0, 0);
            break;
        case 2:  // saludando
            set(PartHead, 0, 10, 0);
            set(PartRightArm, 0, 0, -160);
            set(PartLeftArm, 0, 0, 3);
            break;
        case 3:  // corriendo
            set(PartBody, -10, 0, 0);
            set(PartHead, 10, 0, 0);
            set(PartRightArm, -65, 0, -5);
            set(PartLeftArm, 65, 0, 5);
            set(PartRightLeg, 50, 0, 0);
            set(PartLeftLeg, -50, 0, 0);
            break;
        case 4:  // sentado
            set(PartRightArm, -25, 0, -3);
            set(PartLeftArm, -25, 0, 3);
            set(PartRightLeg, -90, 6, 0);
            set(PartLeftLeg, -90, -6, 0);
            break;
        case 5:  // pelea
        default:
            set(PartBody, 0, 15, 0);
            set(PartRightArm, -80, 0, -10);
            set(PartLeftArm, -40, 0, 12);
            set(PartRightLeg, -15, 0, -8);
            set(PartLeftLeg, 15, 0, 8);
            break;
    }
}

QImage SkinRenderer::defaultSkin()
{
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const QColor skin("#a1a1aa"), hair("#3a3a40"), shirt("#2c2c31"), pants("#3f3f46"), shoes("#18181b");
    p.fillRect(0, 0, 32, 16, skin);
    p.fillRect(8, 0, 8, 8, hair);
    p.fillRect(0, 8, 32, 2, hair);
    p.fillRect(24, 8, 8, 8, hair);
    p.fillRect(9, 12, 2, 1, Qt::white);
    p.fillRect(10, 12, 1, 1, Qt::black);
    p.fillRect(13, 12, 2, 1, Qt::white);
    p.fillRect(13, 12, 1, 1, Qt::black);
    p.fillRect(11, 14, 2, 1, QColor("#71717a"));
    p.fillRect(16, 16, 24, 16, shirt);
    p.fillRect(40, 16, 16, 16, shirt);
    p.fillRect(40, 28, 16, 4, skin);
    p.fillRect(0, 16, 16, 16, pants);
    p.fillRect(0, 30, 16, 2, shoes);
    p.fillRect(16, 48, 16, 16, pants);
    p.fillRect(16, 62, 16, 2, shoes);
    p.fillRect(32, 48, 16, 16, shirt);
    p.fillRect(32, 60, 16, 4, skin);
    return img;
}

void SkinRenderer::render(QPainter& p, const QRectF& area, const QImage& skinIn, const QImage& cape, const SkinRenderOptions& o,
                          std::vector<PickFace>* pick)
{
    if (o.background == 1)
        p.fillRect(area, QColor("#232327"));
    else if (o.background == 2)
        p.fillRect(area, QColor("#0d0d0e"));

    QImage skin = skinIn.isNull() ? defaultSkin() : skinIn.convertToFormat(QImage::Format_ARGB32);
    const bool legacy = skin.height() == 32;
    Ctx c;
    c.o = &o;
    c.scale = area.height() / 46.0 * o.zoom;
    c.center = QPointF(area.center().x(), area.center().y() + 2 * c.scale);
    const V3 bodyPivot{ 0, 12, 0 };
    auto child = [&](V3 pivot, int part) {
        Xform x;
        x.pivot = pivot;
        x.rot = o.parts[part];
        x.hasParent = true;
        x.parentPivot = bodyPivot;
        x.parentRot = o.parts[PartBody];
        return x;
    };
    auto root = [&](V3 pivot, int part) {
        Xform x;
        x.pivot = pivot;
        x.rot = o.parts[part];
        return x;
    };
    const Xform head = child({ 0, 24, 0 }, PartHead);
    const Xform body = root(bodyPivot, PartBody);
    const Xform rArm = child({ -5, 22, 0 }, PartRightArm);
    const Xform lArm = child({ 5, 22, 0 }, PartLeftArm);
    const Xform rLeg = root({ -2, 12, 0 }, PartRightLeg);
    const Xform lLeg = root({ 2, 12, 0 }, PartLeftLeg);
    const double aw = o.slim ? 3 : 4;

    // sombra en el piso
    if (o.shadow) {
        double dd;
        QPointF g = c.project({ 0, 0, 0 }, dd);
        double rw = 11 * c.scale;
        double rh = qMax(2.0, 11 * c.scale * qSin(qDegreesToRadians(qAbs(o.pitch))) * 0.9 + 3);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        p.drawEllipse(g, rw, rh);
        p.restore();
    }

    // cabeza
    c.box(skin, -4, 24, -4, 8, 8, 8, 0, 0, head, PartHead, 0);
    c.box(skin, -4, 24, -4, 8, 8, 8, 32, 0, head, PartHead, 0.5);
    // torso
    c.box(skin, -4, 12, -2, 8, 12, 4, 16, 16, body, PartBody, 0);
    if (!legacy)
        c.box(skin, -4, 12, -2, 8, 12, 4, 16, 32, body, PartBody, 0.25);
    // brazos (el derecho queda a la izquierda de la pantalla)
    c.box(skin, -4 - aw, 12, -2, aw, 12, 4, 40, 16, rArm, PartRightArm, 0);
    if (!legacy)
        c.box(skin, -4 - aw, 12, -2, aw, 12, 4, 40, 32, rArm, PartRightArm, 0.25);
    c.box(skin, 4, 12, -2, aw, 12, 4, legacy ? 40 : 32, legacy ? 16 : 48, lArm, PartLeftArm, 0);
    if (!legacy)
        c.box(skin, 4, 12, -2, aw, 12, 4, 48, 48, lArm, PartLeftArm, 0.25);
    // piernas
    c.box(skin, -4, 0, -2, 4, 12, 4, 0, 16, rLeg, PartRightLeg, 0);
    if (!legacy)
        c.box(skin, -4, 0, -2, 4, 12, 4, 0, 32, rLeg, PartRightLeg, 0.25);
    c.box(skin, 0, 0, -2, 4, 12, 4, legacy ? 0 : 16, legacy ? 16 : 48, lLeg, PartLeftLeg, 0);
    if (!legacy)
        c.box(skin, 0, 0, -2, 4, 12, 4, 0, 48, lLeg, PartLeftLeg, 0.25);
    // armaduras (texturas del juego del jugador)
    const QStringList mats = SkinRenderer::armorMaterials();
    auto armorTex = [&](int slot, int layer) -> QImage {
        const int m = o.armor[slot];
        if (m <= 0 || m > mats.size())
            return {};
        QImage t = gameTex().value(QStringLiteral("armor/%1/%2").arg(mats[m - 1]).arg(layer));
        if (!t.isNull() && mats[m - 1] == "leather") {
            // cuero sin teñir: color cafe por defecto
            t = t.convertToFormat(QImage::Format_ARGB32);
            const QColor dye(0xA0, 0x65, 0x40);
            for (int y = 0; y < t.height(); y++) {
                auto* line = reinterpret_cast<QRgb*>(t.scanLine(y));
                for (int x = 0; x < t.width(); x++)
                    line[x] = qRgba(qRed(line[x]) * dye.red() / 255, qGreen(line[x]) * dye.green() / 255, qBlue(line[x]) * dye.blue() / 255,
                                    qAlpha(line[x]));
            }
        }
        return t;
    };
    auto aScale = [](const QImage& t) { return qMax(1, t.width() / 64); };
    if (QImage t = armorTex(0, 1); !t.isNull())
        c.box(t, -4, 24, -4, 8, 8, 8, 0, 0, head, PartHead, 1.0, false, aScale(t));
    if (QImage t = armorTex(1, 1); !t.isNull()) {
        c.box(t, -4, 12, -2, 8, 12, 4, 16, 16, body, PartBody, 1.0, false, aScale(t));
        c.box(t, -8, 12, -2, 4, 12, 4, 40, 16, rArm, PartRightArm, 1.0, false, aScale(t));
        c.box(t, 4, 12, -2, 4, 12, 4, 40, 16, lArm, PartLeftArm, 1.0, false, aScale(t));
    }
    if (QImage t = armorTex(2, 2); !t.isNull()) {
        c.box(t, -4, 12, -2, 8, 12, 4, 16, 16, body, PartBody, 0.5, false, aScale(t));
        c.box(t, -4, 0, -2, 4, 12, 4, 0, 16, rLeg, PartRightLeg, 0.5, false, aScale(t));
        c.box(t, 0, 0, -2, 4, 12, 4, 0, 16, lLeg, PartLeftLeg, 0.5, false, aScale(t));
    }
    if (QImage t = armorTex(3, 1); !t.isNull()) {
        c.box(t, -4, 0, -2, 4, 12, 4, 0, 16, rLeg, PartRightLeg, 1.0, false, aScale(t));
        c.box(t, 0, 0, -2, 4, 12, 4, 0, 16, lLeg, PartLeftLeg, 1.0, false, aScale(t));
    }
    // objetos en la mano: el mango queda en la mano y la hoja apunta hacia adelante
    auto held = [&](const QString& id, double cx, const Xform& arm, int part) {
        if (id.isEmpty())
            return;
        QImage t = gameTex().value("item/" + id);
        if (t.isNull())
            return;
        // tamano parecido al del juego y mango (pixel 3,13 del sprite) dentro del puno
        const double S = 8.0;
        const V3 grip{ cx, 13.9, 0.0 };
        V3 tl{ cx, grip.y + S * 13.0 / 16.0, grip.z - S * 3.0 / 16.0 };
        V3 tr{ tl.x, tl.y, tl.z + S }, bl{ tl.x, tl.y - S, tl.z };
        // hoja hacia adelante y apenas hacia arriba, como en tercera persona
        const PartRot tilt{ 32, 0, 0 };
        c.quad(t.convertToFormat(QImage::Format_ARGB32), rotateAround(tl, grip, tilt), rotateAround(tr, grip, tilt), rotateAround(bl, grip, tilt), arm, part);
    };
    held(o.heldRight, -4 - aw / 2.0, rArm, PartRightArm);
    held(o.heldLeft, 4 + aw / 2.0, lArm, PartLeftArm);

    // capa: cuelga del torso y se abre segun el movimiento de las piernas
    if (o.showCape && !cape.isNull()) {
        QImage cp = cape.convertToFormat(QImage::Format_ARGB32);
        // capas animadas: tira vertical de cuadros 2:1, se usa el primero
        if (cp.width() >= 64 && cp.height() > cp.width() / 2)
            cp = cp.copy(0, 0, cp.width(), cp.width() / 2);
        // capas HD: se usa la textura completa escalando las coordenadas
        int capeScale = qMax(1, cp.width() / 64);
        const double swing = qMax(qAbs(o.parts[PartRightLeg].x), qAbs(o.parts[PartLeftLeg].x));
        Xform capeX;
        capeX.pivot = { 0, 24, -2.6 };
        capeX.rot = PartRot{ 6 + qMin(swing, 90.0) * 0.5, 0, 0 };
        capeX.hasParent = true;
        capeX.parentPivot = bodyPivot;
        capeX.parentRot = o.parts[PartBody];
        c.box(cp, -5, 8, -3.1, 10, 16, 1, 0, 0, capeX, -1, 0, true, capeScale);
    }

    std::sort(c.faces.begin(), c.faces.end(), [](const Face& a, const Face& b) { return a.depth < b.depth; });
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const auto& f : c.faces) {
        const double w = f.tex.width(), h = f.tex.height();
        QTransform t((f.tr.x() - f.tl.x()) / w, (f.tr.y() - f.tl.y()) / w, (f.bl.x() - f.tl.x()) / h, (f.bl.y() - f.tl.y()) / h, f.tl.x(),
                     f.tl.y());
        p.setTransform(t);
        p.drawImage(QRectF(-0.04, -0.04, w + 0.08, h + 0.08), f.tex, QRectF(0, 0, w, h));
        if (f.solid && f.tex.width() > 1) {
            // brillo suave en los bordes de la cara (se tapa con las caras que vienen delante)
            p.setTransform(QTransform());
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 30), qMax(1.0, c.scale * 0.05)));
            QPolygonF poly;
            poly << f.tl << f.tr << (f.tr + f.bl - f.tl) << f.bl;
            p.drawPolygon(poly);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }
    p.restore();

    for (const auto& f : c.faces) {
        QPolygonF poly;
        poly << f.tl << f.tr << (f.tr + f.bl - f.tl) << f.bl;
        if (pick && f.part >= 0)
            pick->push_back({ poly, f.part });
        if (o.selected >= 0 && f.part == o.selected) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
            p.setBrush(QColor(255, 255, 255, 28));
            p.drawPolygon(poly);
            p.restore();
        }
    }
}

QImage SkinRenderer::renderImage(int size, const QImage& skin, const QImage& cape, const SkinRenderOptions& o)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    SkinRenderOptions clean = o;
    clean.selected = -1;  // sin resaltado en la exportacion
    render(p, QRectF(0, 0, size, size), skin, cape, clean);
    p.end();
    return img;
}

SkinRenderView::SkinRenderView(QWidget* parent) : QWidget(parent)
{
    SkinRenderer::applyPreset(1, options);
    setMinimumSize(360, 360);
    setCursor(Qt::OpenHandCursor);
}

void SkinRenderView::setSkin(const QImage& s, const QImage& c)
{
    skin = s;
    cape = c;
    update();
}

void SkinRenderView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (options.background == 0) {
        // cuadriculado para mostrar transparencia
        const int cs = 12;
        for (int y = 0; y < height(); y += cs)
            for (int x = 0; x < width(); x += cs)
                p.fillRect(x, y, cs, cs, ((x / cs + y / cs) % 2) ? QColor("#1b1b1e") : QColor("#232327"));
    }
    m_pick.clear();
    SkinRenderer::render(p, QRectF(rect()), skin, cape, options, &m_pick);
}

void SkinRenderView::selectPart(int part)
{
    options.selected = part;
    update();
}

void SkinRenderView::mousePressEvent(QMouseEvent* e)
{
    m_last = e->pos();
    m_rotatingPart = false;
    if (e->button() != Qt::LeftButton)
        return;
    // la cara de mas arriba que contiene el punto (se dibujan de atras hacia adelante)
    int hit = -1;
    for (auto it = m_pick.rbegin(); it != m_pick.rend(); ++it)
        if (it->poly.containsPoint(e->position(), Qt::OddEvenFill)) {
            hit = it->part;
            break;
        }
    if (hit >= 0) {
        m_rotatingPart = true;
        if (hit != options.selected) {
            options.selected = hit;
            emit partSelected(hit);
        }
        setCursor(Qt::SizeAllCursor);
    } else {
        setCursor(Qt::ClosedHandCursor);
    }
    update();
}

void SkinRenderView::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton)))
        return;
    QPoint d = e->pos() - m_last;
    m_last = e->pos();
    if (m_rotatingPart && options.selected >= 0 && (e->buttons() & Qt::LeftButton)) {
        PartRot& r = options.parts[options.selected];
        if (e->modifiers() & Qt::ShiftModifier)
            r.z = qBound(-180.0, r.z + d.x() * 0.8, 180.0);
        else
            r.y = qBound(-180.0, r.y + d.x() * 0.8, 180.0);
        r.x = qBound(-180.0, r.x + d.y() * 0.8, 180.0);
        emit poseEdited();
    } else {
        options.yaw += d.x() * 0.6;
        options.pitch = qBound(-60.0, options.pitch + d.y() * 0.4, 60.0);
    }
    update();
}

void SkinRenderView::mouseReleaseEvent(QMouseEvent*)
{
    m_rotatingPart = false;
    setCursor(Qt::OpenHandCursor);
}

void SkinRenderView::wheelEvent(QWheelEvent* e)
{
    options.zoom = qBound(0.4, options.zoom * (e->angleDelta().y() > 0 ? 1.1 : 0.9), 3.0);
    update();
}
