// 4fps - pantallas Versiones, Mods, Archivos, Servers, Skins y Render
#include "FourFpsShell.h"
#include "FourFpsFeatures.h"
#include <QRegularExpression>
#include <QPointer>
#include <QDnsLookup>
#include <QTcpSocket>
#include "SkinRender.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <memory>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>
#include <sstream>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "tasks/ConcurrentTask.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "archive/ArchiveReader.h"

using namespace fps;

namespace {
// lista que no cambia con la rueda del mouse (para paneles con scroll)
class NoWheelComboBox : public QComboBox {
   public:
    using QComboBox::QComboBox;

   protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

QWidget* pageShell(QWidget* parent, QVBoxLayout*& v)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("fpsPage"));
    v = new QVBoxLayout(page);
    v->setContentsMargins(32, 28, 32, 24);
    v->setSpacing(18);
    return page;
}

void clearLayout(QLayout* l)
{
    while (auto* item = l->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        else if (auto* sub = item->layout())
            clearLayout(sub);
        delete item;
    }
}

QPushButton* chip(const QString& text, QWidget* parent)
{
    auto* c = new QPushButton(text, parent);
    c->setObjectName(QStringLiteral("fpsChip"));
    c->setCheckable(true);
    c->setCursor(Qt::PointingHandCursor);
    return c;
}

QScrollArea* scrollFor(QWidget* parent, QWidget*& host)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("fpsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("fpsGridHost"));
    scroll->setWidget(host);
    return scroll;
}

QString monthYear(const QDateTime& t)
{
    if (!t.isValid())
        return QString();
    QString s = QLocale(QLocale::Spanish).toString(t.date(), "MMMM yyyy");
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

QString loaderUid(const QString& loader)
{
    if (loader == "Fabric")
        return "net.fabricmc.fabric-loader";
    if (loader == "Forge")
        return "net.minecraftforge";
    if (loader == "NeoForge")
        return "net.neoforged";
    if (loader == "Quilt")
        return "org.quiltmc.quilt-loader";
    return {};
}

QFrame* placeholderTile(QWidget* parent, const QString& icon, int w, int h)
{
    auto* f = makeCard(parent, QStringLiteral("fpsTile"));
    f->setFixedSize(w, h);
    auto* l = new QVBoxLayout(f);
    auto* i = new QLabel(f);
    i->setAlignment(Qt::AlignCenter);
    i->setPixmap(QIcon::fromTheme(icon).pixmap(28, 28));
    l->addWidget(i);
    return f;
}
}  // namespace

// ============================================================ refresco general
void FourFpsShell::onPageShown(int i)
{
    switch (i) {
        case 2:
            refreshVersions();
            break;
        case 3:
            refreshMods();
            break;
        case 4:
            refreshFiles();
            break;
        case 5:
            refreshServers();
            break;
        case 6:
            refreshSkins();
            break;
        case 8:
            refreshProfile();
            break;
        case 9:
            if (m_galleryPage)
                fps::refreshGalleryPage(m_galleryPage);
            break;
        default:
            break;
    }
}

// =================================================================== Versiones
QWidget* FourFpsShell::buildVersionsPage()
{
    QVBoxLayout* v;
    auto* page = pageShell(this, v);
    auto* refresh = makeButton(tr("Actualizar lista"), "refresh", false, page);
    connect(refresh, &QPushButton::clicked, this, [this] {
        auto vl = APPLICATION->metadataIndex()->get("net.minecraft");
        auto task = vl->getLoadTask(true);
        if (task) {
            connect(task.get(), &Task::finished, this, &FourFpsShell::refreshVersions);
            task->start();
        }
    });
    v->addWidget(header(tr("Versiones"), tr("Elige la versión de Minecraft y el cargador de mods para tu instancia."), { refresh }));

    auto* cols = new QHBoxLayout();
    cols->setSpacing(20);
    auto* left = makeCard(page);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(16, 16, 16, 16);
    lv->setSpacing(12);
    auto* fr = new QHBoxLayout();
    fr->setSpacing(8);
    m_versionSearch = new QLineEdit(left);
    m_versionSearch->setObjectName(QStringLiteral("fpsSearch"));
    m_versionSearch->setPlaceholderText(tr("Buscar versión..."));
    connect(m_versionSearch, &QLineEdit::textChanged, this, &FourFpsShell::refreshVersions);
    fr->addWidget(m_versionSearch, 1);
    auto* types = new QButtonGroup(this);
    const QList<QPair<QString, QString>> typeDefs = { { "Release", "release" }, { "Snapshot", "snapshot" }, { "Beta", "old_beta" }, { "Alpha", "old_alpha" } };
    for (const auto& t : typeDefs) {
        auto* c = chip(t.first, left);
        c->setChecked(t.second == m_versionType);
        types->addButton(c);
        const QString key = t.second;
        connect(c, &QPushButton::clicked, this, [this, key] {
            m_versionType = key;
            refreshVersions();
        });
        fr->addWidget(c);
    }
    lv->addLayout(fr);
    m_versionTree = new QTreeWidget(left);
    m_versionTree->setObjectName(QStringLiteral("fpsTable"));
    m_versionTree->setColumnCount(3);
    m_versionTree->setHeaderLabels({ tr("VERSIÓN"), tr("TIPO"), tr("SALIDA") });
    m_versionTree->setRootIsDecorated(false);
    m_versionTree->setIndentation(0);
    m_versionTree->header()->setStretchLastSection(true);
    m_versionTree->setColumnWidth(0, 130);
    m_versionTree->setColumnWidth(1, 120);
    connect(m_versionTree, &QTreeWidget::currentItemChanged, this, [this] {
        if (auto* it = m_versionTree->currentItem()) {
            const QString loaderSuffix = m_loader == "Vanilla" ? QString() : m_loader + " ";
            if (m_newName && (m_newName->text().isEmpty() || m_newName->property("auto").toBool())) {
                m_newName->setText(loaderSuffix + it->text(0));
                m_newName->setProperty("auto", true);
            }
        }
        refreshLoaderVersions();
    });
    lv->addWidget(m_versionTree, 1);
    cols->addWidget(left, 1);

    auto* right = makeCard(page);
    right->setFixedWidth(372);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(20, 20, 20, 20);
    rv->setSpacing(10);
    rv->addWidget(makeLabel(tr("NOMBRE DE LA INSTANCIA"), QStringLiteral("fpsCaption"), right));
    m_newName = new QLineEdit(right);
    m_newName->setObjectName(QStringLiteral("fpsInput"));
    connect(m_newName, &QLineEdit::textEdited, this, [this] { m_newName->setProperty("auto", false); });
    rv->addWidget(m_newName);
    rv->addSpacing(6);
    rv->addWidget(makeLabel(tr("CARGADOR DE MODS"), QStringLiteral("fpsCaption"), right));
    auto* lg = new QGridLayout();
    lg->setSpacing(8);
    auto* loaders = new QButtonGroup(this);
    const QStringList loaderNames = { "Vanilla", "Fabric", "Forge", "NeoForge", "Quilt" };
    for (int i = 0; i < loaderNames.size(); i++) {
        auto* b = new QPushButton(loaderNames[i], right);
        b->setObjectName(QStringLiteral("fpsSegment"));
        b->setCheckable(true);
        b->setChecked(loaderNames[i] == m_loader);
        b->setCursor(Qt::PointingHandCursor);
        loaders->addButton(b);
        const QString name = loaderNames[i];
        connect(b, &QPushButton::clicked, this, [this, name] {
            m_loader = name;
            if (auto* it = m_versionTree->currentItem())
                if (m_newName->property("auto").toBool() || m_newName->text().isEmpty()) {
                    m_newName->setText((name == "Vanilla" ? QString() : name + " ") + it->text(0));
                    m_newName->setProperty("auto", true);
                }
            refreshLoaderVersions();
        });
        lg->addWidget(b, i / 3, i % 3);
    }
    rv->addLayout(lg);
    rv->addSpacing(6);
    rv->addWidget(makeLabel(tr("VERSIÓN DEL CARGADOR"), QStringLiteral("fpsCaption"), right));
    m_loaderVersion = new QComboBox(right);
    m_loaderVersion->setObjectName(QStringLiteral("fpsSelect"));
    rv->addWidget(m_loaderVersion);
    rv->addSpacing(6);
    auto* ramRow = new QHBoxLayout();
    ramRow->addWidget(makeLabel(tr("MEMORIA RAM"), QStringLiteral("fpsCaption"), right));
    ramRow->addStretch();
    m_ramLabel = makeLabel("4 GB", QStringLiteral("fpsStatValue"), right);
    ramRow->addWidget(m_ramLabel);
    rv->addLayout(ramRow);
    m_ram = new QSlider(Qt::Horizontal, right);
    m_ram->setRange(1, 16);
    m_ram->setValue(4);
    connect(m_ram, &QSlider::valueChanged, this, [this](int gb) { m_ramLabel->setText(QStringLiteral("%1 GB").arg(gb)); });
    rv->addWidget(m_ram);
    auto* ramScale = new QHBoxLayout();
    ramScale->addWidget(makeLabel("1 GB", QStringLiteral("fpsHint"), right));
    ramScale->addStretch();
    ramScale->addWidget(makeLabel("16 GB", QStringLiteral("fpsHint"), right));
    rv->addLayout(ramScale);
    rv->addStretch(1);
    auto* create = makeButton(tr("Crear instancia"), "new", true, right);
    create->setMinimumHeight(44);
    connect(create, &QPushButton::clicked, this, [this] {
        auto* it = m_versionTree->currentItem();
        if (!it) {
            QMessageBox::information(this, tr("Versiones"), tr("Primero elige una versión de Minecraft de la lista."));
            return;
        }
        auto vl = APPLICATION->metadataIndex()->get("net.minecraft");
        Meta::Version::Ptr mc;
        for (auto& ver : vl->versions())
            if (ver->descriptor() == it->data(0, Qt::UserRole).toString())
                mc = ver;
        if (!mc)
            return;
        InstanceTask* task = nullptr;
        const QString uid = loaderUid(m_loader);
        if (uid.isEmpty()) {
            task = new VanillaCreationTask(mc);
        } else {
            const QString lv = m_loaderVersion->currentData().toString();
            Meta::Version::Ptr loaderVer;
            for (auto& ver : APPLICATION->metadataIndex()->get(uid)->versions())
                if (ver->descriptor() == lv)
                    loaderVer = ver;
            if (!loaderVer) {
                QMessageBox::information(this, tr("Versiones"), tr("Elige una versión del cargador (puede tardar unos segundos en cargar)."));
                return;
            }
            task = new VanillaCreationTask(mc, uid, loaderVer);
        }
        QString name = m_newName->text().trimmed();
        if (name.isEmpty())
            name = it->text(0);
        task->setName(name);
        task->setIcon("default");
        m_pendingName = name;
        m_pendingRam = m_ram->value() * 1024;
        m_hooks.createInstance(task);
        // aplica la RAM elegida a la instancia nueva
        auto* list = APPLICATION->instances();
        for (int i = list->count() - 1; i >= 0; i--) {
            auto* inst = list->at(i);
            if (inst->name() == m_pendingName) {
                inst->settings()->set("OverrideMemory", true);
                inst->settings()->set("MaxMemAlloc", m_pendingRam);
                m_hooks.select(inst);
                break;
            }
        }
        showPage(1);
    });
    rv->addWidget(create);
    cols->addWidget(right);
    v->addLayout(cols, 1);
    return page;
}

void FourFpsShell::refreshVersions()
{
    if (!m_versionTree)
        return;
    auto vl = APPLICATION->metadataIndex()->get("net.minecraft");
    if (!vl->isLoaded()) {
        if (!m_versionsLoading) {
            m_versionsLoading = true;
            auto task = vl->getLoadTask();
            if (task) {
                connect(task.get(), &Task::finished, this, [this] {
                    m_versionsLoading = false;
                    refreshVersions();
                });
                task->start();
            }
        }
        m_versionTree->clear();
        auto* it = new QTreeWidgetItem(m_versionTree, { tr("Cargando versiones..."), "", "" });
        it->setFlags(Qt::NoItemFlags);
        it->setFirstColumnSpanned(true);
        return;
    }
    const QString prev = m_versionTree->currentItem() ? m_versionTree->currentItem()->data(0, Qt::UserRole).toString() : QString();
    m_versionTree->blockSignals(true);
    m_versionTree->clear();
    const QString q = m_versionSearch->text().trimmed();
    QTreeWidgetItem* select = nullptr;
    for (auto& ver : vl->versions()) {
        if (ver->type() != m_versionType)
            continue;
        if (!q.isEmpty() && !ver->descriptor().contains(q, Qt::CaseInsensitive))
            continue;
        QString type = ver->type() == "release" ? "Release" : ver->type() == "snapshot" ? "Snapshot" : ver->type() == "old_beta" ? "Beta" : "Alpha";
        auto* it = new QTreeWidgetItem(m_versionTree, { ver->descriptor(), type, monthYear(ver->time()) });
        it->setData(0, Qt::UserRole, ver->descriptor());
        if (ver->descriptor() == prev || (!select && prev.isEmpty()))
            select = it;
    }
    m_versionTree->blockSignals(false);
    if (select)
        m_versionTree->setCurrentItem(select);
}

void FourFpsShell::refreshLoaderVersions()
{
    if (!m_loaderVersion)
        return;
    m_loaderVersion->clear();
    const QString uid = loaderUid(m_loader);
    auto* it = m_versionTree->currentItem();
    if (uid.isEmpty() || !it) {
        m_loaderVersion->addItem(uid.isEmpty() ? tr("No necesita cargador") : tr("Elige una versión"));
        m_loaderVersion->setEnabled(false);
        return;
    }
    m_loaderVersion->setEnabled(true);
    auto vl = APPLICATION->metadataIndex()->get(uid);
    if (!vl->isLoaded()) {
        m_loaderVersion->addItem(tr("Cargando..."));
        auto task = vl->getLoadTask();
        if (task) {
            connect(task.get(), &Task::finished, this, &FourFpsShell::refreshLoaderVersions);
            task->start();
        }
        return;
    }
    const QString mc = it->data(0, Qt::UserRole).toString();
    const bool needsMatch = (m_loader == "Forge" || m_loader == "NeoForge");
    int recommendedIdx = -1;
    for (auto& ver : vl->versions()) {
        if (needsMatch) {
            bool ok = false;
            for (const auto& req : ver->requiredSet())
                if (req.uid == "net.minecraft" && req.equalsVersion == mc)
                    ok = true;
            if (!ok)
                continue;
        }
        QString label = ver->descriptor();
        if (ver->isRecommended()) {
            label += tr(" (recomendada)");
            if (recommendedIdx < 0)
                recommendedIdx = m_loaderVersion->count();
        }
        m_loaderVersion->addItem(label, ver->descriptor());
    }
    if (m_loaderVersion->count() == 0) {
        m_loaderVersion->addItem(tr("No hay versiones para %1").arg(mc));
        m_loaderVersion->setEnabled(false);
    } else {
        m_loaderVersion->setCurrentIndex(recommendedIdx >= 0 ? recommendedIdx : 0);
    }
}

// ======================================================================== Mods
QWidget* FourFpsShell::buildModsPage()
{
    QVBoxLayout* v;
    auto* page = pageShell(this, v);
    auto* folder = makeButton(tr("Carpeta de mods"), "viewfolder", false, page);
    auto* addJar = makeButton(tr("Añadir .jar"), "new", true, page);
    connect(folder, &QPushButton::clicked, this, [this] {
        if (auto* inst = m_hooks.selected())
            DesktopServices::openPath(inst->modsRoot(), true);
    });
    connect(addJar, &QPushButton::clicked, this, [this] {
        auto* inst = m_hooks.selected();
        if (!inst)
            return;
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Añadir mods"), QString(), tr("Mods (*.jar *.zip *.disabled)"));
        for (const auto& f : files)
            inst->loaderModList()->installResource(f);
        QTimer::singleShot(300, this, &FourFpsShell::refreshMods);
    });
    auto* h = header(tr("Mods"), QString(), { folder, addJar });
    m_modsSubtitle = h->findChild<QLabel*>(QStringLiteral("fpsSubtitle"));
    v->addWidget(h);

    auto* fr = new QHBoxLayout();
    fr->setSpacing(8);
    m_modsSearch = new QLineEdit(page);
    m_modsSearch->setObjectName(QStringLiteral("fpsSearch"));
    m_modsSearch->setPlaceholderText(tr("Buscar en tus mods..."));
    connect(m_modsSearch, &QLineEdit::textChanged, this, &FourFpsShell::refreshMods);
    fr->addWidget(m_modsSearch, 1);
    auto openDownloader = [this](const QString& provider) {
        auto* inst = m_hooks.selected();
        if (!inst) {
            QMessageBox::information(this, tr("Mods"), tr("Primero selecciona una instancia."));
            return;
        }
        if (!inst->getPackProfile()->getModLoaders().has_value()) {
            QMessageBox::information(this, tr("Mods"), tr("Esta instancia es Vanilla. Crea una con Fabric, Forge, NeoForge o Quilt para instalar mods."));
            return;
        }
        auto* model = inst->loaderModList();
        auto* dlg = ResourceDownload::ResourceDownloadDialog::createMod(this, model, inst);
        dlg->selectPage(provider);
        if (dlg->exec()) {
            ConcurrentTask tasks(tr("Descargando mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
            for (auto& t : dlg->getTasks())
                tasks.addTask(t);
            ProgressDialog pd(this);
            pd.execWithTask(&tasks);
            model->update();
        }
        dlg->deleteLater();
        QTimer::singleShot(500, this, &FourFpsShell::refreshMods);
    };
    auto* group = new QButtonGroup(this);
    auto* modrinth = chip("Modrinth", page);
    auto* curse = chip("CurseForge", page);
    m_modsInstalledChip = chip(tr("Instalados"), page);
    m_modsInstalledChip->setChecked(true);
    m_modsInstalledChip->setMinimumWidth(120);
    group->addButton(modrinth);
    group->addButton(curse);
    group->addButton(m_modsInstalledChip);
    connect(modrinth, &QPushButton::clicked, this, [openDownloader, this] {
        m_modsInstalledChip->setChecked(true);
        openDownloader("modrinth");
    });
    connect(curse, &QPushButton::clicked, this, [openDownloader, this] {
        m_modsInstalledChip->setChecked(true);
        openDownloader("curseforge");
    });
    fr->addWidget(modrinth);
    fr->addWidget(curse);
    fr->addWidget(m_modsInstalledChip);
    auto* groupsBtn = chip(tr("Grupos"), page);
    groupsBtn->setCheckable(false);
    groupsBtn->setToolTip(tr("Grupos de mods que se actualizan juntos (ej: Renderizado: Sodium, Iris, Voxy...)"));
    connect(groupsBtn, &QPushButton::clicked, this, [this] {
        if (!fps::enabled(fps::cfg::ModGroups)) {
            QMessageBox::information(this, tr("Grupos de mods"), tr("Los grupos están apagados. Actívalos en Ajustes → Funciones 4fps."));
            return;
        }
        fps::editModGroups(this, m_modsInstance ? m_modsInstance : m_hooks.selected());
    });
    fr->addWidget(groupsBtn);
    v->addLayout(fr);

    QWidget* host;
    auto* scroll = scrollFor(page, host);
    m_modsGrid = new QGridLayout(host);
    m_modsGrid->setContentsMargins(0, 0, 0, 0);
    m_modsGrid->setSpacing(12);
    m_modsGrid->setAlignment(Qt::AlignTop);
    v->addWidget(scroll, 1);
    return page;
}

void FourFpsShell::refreshMods()
{
    if (!m_modsGrid)
        return;
    clearLayout(m_modsGrid);
    auto* inst = m_hooks.selected();
    if (!inst) {
        m_modsSubtitle->setText(tr("Selecciona una instancia para ver sus mods."));
        return;
    }
    auto* model = inst->loaderModList();
    if (m_modsInstance != inst) {
        m_modsInstance = inst;
        connect(model, &ResourceFolderModel::updateFinished, this, &FourFpsShell::refreshMods, Qt::UniqueConnection);
        // los datos de cada mod (nombre, icono) se leen en segundo plano: refresca cuando lleguen
        connect(model, &QAbstractItemModel::dataChanged, this, [this, model] {
            if (m_modsRefreshQueued || m_modsInstance != m_hooks.selected())
                return;
            m_modsRefreshQueued = true;
            QTimer::singleShot(400, this, [this] {
                m_modsRefreshQueued = false;
                if (m_stack->currentIndex() == 3)
                    refreshMods();
            });
        });
        model->update();
    }
    const QString loader = loaderOf(inst);
    const QString mc = mcVersionOf(inst);
    m_modsSubtitle->setText(tr("Busca e instala mods para la instancia %1.").arg(inst->name()));
    const auto resources = model->allResources();
    m_modsInstalledChip->setText(tr("Instalados (%1)").arg(resources.size()));
    const QString q = m_modsSearch->text().trimmed();
    int n = 0;
    for (auto* res : resources) {
        auto* mod = dynamic_cast<Mod*>(res);
        const QString name = res->name();
        if (!q.isEmpty() && !name.contains(q, Qt::CaseInsensitive))
            continue;
        auto* card = makeCard(m_modsGrid->parentWidget());
        card->setMinimumHeight(96);
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(14, 12, 14, 12);
        h->setSpacing(14);
        auto* icon = new QLabel(card);
        icon->setObjectName(QStringLiteral("fpsAvatar"));
        icon->setFixedSize(52, 52);
        icon->setAlignment(Qt::AlignCenter);
        QPixmap modIcon = mod ? mod->icon(QSize(48, 48), Qt::KeepAspectRatio) : QPixmap();
        icon->setPixmap(modIcon.isNull() ? QIcon::fromTheme("loadermods").pixmap(24, 24) : modIcon);
        h->addWidget(icon, 0, Qt::AlignTop);
        auto* t = new QVBoxLayout();
        t->setSpacing(2);
        t->addWidget(makeLabel(name, QStringLiteral("fpsRowTitle"), card));
        QString desc = mod ? mod->description() : QString();
        if (desc.isEmpty())
            desc = res->fileinfo().fileName();
        auto* d = makeLabel(desc.left(110), QStringLiteral("fpsMuted"), card);
        d->setWordWrap(true);
        t->addWidget(d);
        QString meta = QStringLiteral("%1 · %2%3").arg(loader, mc, mod && !mod->version().isEmpty() ? " · v" + mod->version() : QString());
        if (pvpFiles(inst).contains(res->fileinfo().fileName()))
            meta += tr("  ·  Incluido por Cliente PvP 4fps");
        t->addWidget(makeLabel(meta, QStringLiteral("fpsHint"), card));
        h->addLayout(t, 1);
        const bool enabled = res->enabled();
        auto* toggle = makeButton(enabled ? QStringLiteral("✓  ") + tr("Instalado") : tr("Desactivado"), QString(), !enabled, card);
        toggle->setToolTip(enabled ? tr("Clic para desactivar") : tr("Clic para activar"));
        const QString file = res->fileinfo().fileName();
        connect(toggle, &QPushButton::clicked, this, [this, model, file, enabled] {
            const auto all = model->allResources();
            for (int i = 0; i < all.size(); i++)
                if (all[i]->fileinfo().fileName() == file) {
                    model->setResourceEnabled({ model->index(i) }, enabled ? EnableAction::DISABLE : EnableAction::ENABLE);
                    break;
                }
            QTimer::singleShot(200, this, &FourFpsShell::refreshMods);
        });
        auto* del = makeButton(QString(), "delete", false, card);
        del->setObjectName(QStringLiteral("fpsIconButtonFlat"));
        del->setFixedSize(30, 30);
        connect(del, &QPushButton::clicked, this, [this, model, file, name] {
            if (QMessageBox::question(this, tr("Eliminar mod"), tr("¿Eliminar %1?").arg(name)) != QMessageBox::Yes)
                return;
            model->uninstallResource(file);
            QTimer::singleShot(200, this, &FourFpsShell::refreshMods);
        });
        auto* bv = new QVBoxLayout();
        bv->addWidget(toggle);
        bv->addWidget(del, 0, Qt::AlignRight);
        h->addLayout(bv);
        m_modsGrid->addWidget(card, n / 2, n % 2);
        n++;
    }
    if (n == 0) {
        auto* empty = makeLabel(resources.isEmpty() ? tr("Esta instancia no tiene mods. Usa Modrinth o CurseForge para buscar e instalar.")
                                                    : tr("Ningún mod coincide con la búsqueda."),
                                QStringLiteral("fpsMuted"), m_modsGrid->parentWidget());
        m_modsGrid->addWidget(empty, 0, 0);
    }
}

// ==================================================================== Archivos
QWidget* FourFpsShell::buildFilesPage()
{
    QVBoxLayout* v;
    auto* page = pageShell(this, v);
    auto folderFor = [this](int tab) -> QString {
        auto* inst = m_hooks.selected();
        if (!inst)
            return {};
        const QString root = inst->gameRoot();
        switch (tab) {
            case 0:
                return root + "/screenshots";
            case 1:
                return root + "/saves";
            case 2:
                return inst->resourcePacksDir();
            case 3:
                return inst->shaderPacksDir();
            default:
                return root + "/logs";
        }
    };
    auto* open = makeButton(tr("Abrir carpeta"), "viewfolder", false, page);
    auto* imp = makeButton(tr("Importar"), "export", true, page);
    connect(open, &QPushButton::clicked, this, [this, folderFor] {
        const QString f = folderFor(m_filesTab);
        if (!f.isEmpty())
            DesktopServices::openPath(f, true);
    });
    connect(imp, &QPushButton::clicked, this, [this, folderFor] {
        const QString dir = folderFor(m_filesTab);
        if (dir.isEmpty())
            return;
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Importar archivos"));
        QDir().mkpath(dir);
        for (const auto& f : files)
            QFile::copy(f, dir + "/" + QFileInfo(f).fileName());
        refreshFiles();
    });
    v->addWidget(header(tr("Archivos"), tr("Capturas, mundos y packs de la instancia actual."), { open, imp }));

    auto* tabs = new QHBoxLayout();
    tabs->setSpacing(4);
    auto* tg = new QButtonGroup(this);
    const QStringList names = { tr("Capturas"), tr("Mundos"), tr("Resource packs"), tr("Shader packs"), tr("Logs") };
    for (int i = 0; i < names.size(); i++) {
        auto* b = new QPushButton(names[i], page);
        b->setObjectName(QStringLiteral("fpsTab"));
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setCursor(Qt::PointingHandCursor);
        tg->addButton(b);
        connect(b, &QPushButton::clicked, this, [this, i] {
            m_filesTab = i;
            m_selectedFile.clear();
            refreshFiles();
        });
        tabs->addWidget(b);
        m_filesTabs << b;
    }
    tabs->addStretch();
    auto* tabsHost = new QFrame(page);
    tabsHost->setObjectName(QStringLiteral("fpsTabBar"));
    tabsHost->setLayout(tabs);
    tabs->setContentsMargins(0, 0, 0, 0);
    v->addWidget(tabsHost);

    auto* body = new QHBoxLayout();
    body->setSpacing(20);
    QWidget* host;
    auto* scroll = scrollFor(page, host);
    m_filesGrid = new QGridLayout(host);
    m_filesGrid->setContentsMargins(0, 0, 0, 0);
    m_filesGrid->setSpacing(14);
    m_filesGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body->addWidget(scroll, 1);

    auto* detail = makeCard(page);
    detail->setFixedWidth(264);
    m_fileDetail = detail;
    auto* dv = new QVBoxLayout(detail);
    dv->setContentsMargins(16, 16, 16, 16);
    dv->setSpacing(8);
    m_filePreview = new QLabel(detail);
    m_filePreview->setObjectName(QStringLiteral("fpsAvatar"));
    m_filePreview->setFixedHeight(130);
    m_filePreview->setAlignment(Qt::AlignCenter);
    dv->addWidget(m_filePreview);
    m_fileName = makeLabel(QString(), QStringLiteral("fpsRowTitle"), detail);
    m_fileName->setWordWrap(true);
    dv->addWidget(m_fileName);
    m_fileInfo = makeLabel(QString(), QStringLiteral("fpsMuted"), detail);
    m_fileInfo->setWordWrap(true);
    dv->addWidget(m_fileInfo);
    m_fileCopy = makeButton(tr("Copiar imagen"), QString(), true, detail);
    connect(m_fileCopy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setImage(QImage(m_selectedFile)); });
    dv->addWidget(m_fileCopy);
    auto* show = makeButton(tr("Mostrar en carpeta"), QString(), false, detail);
    connect(show, &QPushButton::clicked, this, [this] { DesktopServices::openPath(QFileInfo(m_selectedFile).absolutePath(), true); });
    dv->addWidget(show);
    auto* del = makeButton(tr("Eliminar"), "delete", false, detail);
    connect(del, &QPushButton::clicked, this, [this] {
        if (m_selectedFile.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Eliminar"), tr("¿Mover %1 a la papelera?").arg(QFileInfo(m_selectedFile).fileName())) != QMessageBox::Yes)
            return;
        QFileInfo fi(m_selectedFile);
        if (!QFile::moveToTrash(m_selectedFile) && fi.isDir())
            QDir(m_selectedFile).removeRecursively();
        m_selectedFile.clear();
        refreshFiles();
    });
    dv->addWidget(del);
    dv->addStretch(1);
    body->addWidget(detail, 0, Qt::AlignTop);
    v->addLayout(body, 1);

    m_pages[4] = page;
    return page;
}

void FourFpsShell::refreshFiles()
{
    if (!m_filesGrid)
        return;
    clearLayout(m_filesGrid);
    auto* inst = m_hooks.selected();
    if (!inst) {
        m_filesGrid->addWidget(makeLabel(tr("Selecciona una instancia para ver sus archivos."), QStringLiteral("fpsMuted"), m_filesGrid->parentWidget()), 0, 0);
        m_fileDetail->setVisible(false);
        return;
    }
    QString dir;
    switch (m_filesTab) {
        case 0:
            dir = inst->gameRoot() + "/screenshots";
            break;
        case 1:
            dir = inst->gameRoot() + "/saves";
            break;
        case 2:
            dir = inst->resourcePacksDir();
            break;
        case 3:
            dir = inst->shaderPacksDir();
            break;
        default:
            dir = inst->gameRoot() + "/logs";
    }
    QDir d(dir);
    QFileInfoList entries;
    if (m_filesTab == 0)
        entries = d.entryInfoList({ "*.png", "*.jpg" }, QDir::Files, QDir::Time);
    else if (m_filesTab == 1)
        entries = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    else
        entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    if (entries.size() > 48)
        entries = entries.mid(0, 48);
    if (entries.isEmpty()) {
        m_filesGrid->addWidget(makeLabel(tr("No hay archivos aquí todavía."), QStringLiteral("fpsMuted"), m_filesGrid->parentWidget()), 0, 0);
        m_fileDetail->setVisible(false);
        return;
    }
    if (m_selectedFile.isEmpty())
        m_selectedFile = entries.first().absoluteFilePath();
    const QString icon = m_filesTab == 1 ? "worlds" : m_filesTab == 2 ? "resourcepacks" : m_filesTab == 3 ? "shaderpacks" : "log";
    int n = 0;
    for (const auto& fi : entries) {
        auto* tile = new QPushButton(m_filesGrid->parentWidget());
        tile->setObjectName(QStringLiteral("fpsFileTile"));
        tile->setCheckable(true);
        tile->setChecked(fi.absoluteFilePath() == m_selectedFile);
        tile->setFixedSize(150, 116);
        tile->setCursor(Qt::PointingHandCursor);
        if (m_filesTab == 0) {
            QImage img(fi.absoluteFilePath());
            tile->setIcon(QPixmap::fromImage(img.scaled(146, 112, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
            tile->setIconSize(QSize(146, 112));
        } else {
            tile->setIcon(QIcon::fromTheme(icon));
            tile->setIconSize(QSize(30, 30));
        }
        const QString path = fi.absoluteFilePath();
        connect(tile, &QPushButton::clicked, this, [this, path] {
            m_selectedFile = path;
            refreshFiles();
        });
        auto* cell = new QWidget(m_filesGrid->parentWidget());
        auto* cv = new QVBoxLayout(cell);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(6);
        cv->addWidget(tile);
        auto* lbl = makeLabel(QFontMetrics(font()).elidedText(fi.fileName(), Qt::ElideMiddle, 150), QStringLiteral("fpsMuted"), cell);
        cv->addWidget(lbl);
        m_filesGrid->addWidget(cell, n / 4, n % 4);
        n++;
    }
    // detalle
    QFileInfo sel(m_selectedFile);
    m_fileDetail->setVisible(true);
    m_fileName->setText(sel.fileName());
    QString info = tr("Instancia: %1").arg(inst->name());
    if (m_filesTab == 0) {
        QImage img(m_selectedFile);
        m_filePreview->setPixmap(QPixmap::fromImage(img.scaled(230, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        info += "\n" + tr("Resolución: %1 × %2").arg(img.width()).arg(img.height());
    } else {
        m_filePreview->setPixmap(QIcon::fromTheme(icon).pixmap(40, 40));
        if (sel.isFile())
            info += "\n" + tr("Tamaño: %1 KB").arg(sel.size() / 1024);
    }
    info += "\n" + tr("Modificado: %1").arg(QLocale(QLocale::Spanish).toString(sel.lastModified(), QLocale::ShortFormat));
    m_fileInfo->setText(info);
    m_fileCopy->setVisible(m_filesTab == 0);
}

// ===================================================================== Servers
namespace {
struct ServerEntry {
    QString name, ip;
    QByteArray icon;  // PNG que guarda el juego en servers.dat (base64)
};

QString serverIconCache(const QString& host)
{
    const QString d = APPLICATION->dataRoot() + "/server-icons";
    QDir().mkpath(d);
    QString safe = host.toLower();
    safe.replace(QRegularExpression("[^a-z0-9._-]"), "_");
    return d + "/" + safe + ".png";
}


QList<ServerEntry> readServers(const QString& file)
{
    QList<ServerEntry> out;
    try {
        auto res = FS::read(file);
        if (!res)
            return out;
        const auto& input = res.value();
        std::istringstream in(std::string(input.constData(), input.size()));
        auto pair = nbt::io::read_compound(in);
        if (!pair.second || !pair.second->has_key("servers", nbt::tag_type::List))
            return out;
        auto& list = pair.second->at("servers").as<nbt::tag_list>();
        for (auto& tag : list) {
            auto& c = tag.as<nbt::tag_compound>();
            ServerEntry e;
            if (c.has_key("name", nbt::tag_type::String))
                e.name = QString::fromStdString(c["name"].as<nbt::tag_string>().get());
            if (c.has_key("ip", nbt::tag_type::String))
                e.ip = QString::fromStdString(c["ip"].as<nbt::tag_string>().get());
            if (c.has_key("icon", nbt::tag_type::String))
                e.icon = QByteArray::fromBase64(QByteArray::fromStdString(c["icon"].as<nbt::tag_string>().get()));
            out << e;
        }
    } catch (...) {
    }
    return out;
}

bool addServer(const QString& file, const QString& name, const QString& ip)
{
    try {
        std::unique_ptr<nbt::tag_compound> root;
        if (auto res = FS::read(file)) {
            const auto& input = res.value();
            std::istringstream in(std::string(input.constData(), input.size()));
            root = nbt::io::read_compound(in).second;
        }
        if (!root)
            root = std::make_unique<nbt::tag_compound>();
        if (!root->has_key("servers", nbt::tag_type::List))
            root->insert("servers", nbt::tag_list());
        auto& list = root->at("servers").as<nbt::tag_list>();
        nbt::tag_compound s;
        s.insert("name", nbt::tag_string(name.toStdString()));
        s.insert("ip", nbt::tag_string(ip.toStdString()));
        list.push_back(std::move(s));
        FS::ensureFilePathExists(file);
        std::ostringstream os;
        nbt::io::write_tag("", *root, os);
        QByteArray val(os.str().data(), int(os.str().size()));
        return FS::write(file, val).has_value();
    } catch (...) {
        return false;
    }
}
}  // namespace

void FourFpsShell::scanServerBadges()
{
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++)
        for (const auto& s : readServers(list->at(i)->gameRoot() + "/servers.dat"))
            awardForServer(s.ip);
}

QWidget* FourFpsShell::buildServersPage()
{
    QVBoxLayout* v;
    auto* page = pageShell(this, v);
    auto* refresh = makeButton(tr("Actualizar"), "refresh", false, page);
    auto* add = makeButton(tr("Añadir server"), "new", true, page);
    connect(refresh, &QPushButton::clicked, this, &FourFpsShell::refreshServers);
    connect(add, &QPushButton::clicked, this, [this] {
        auto* inst = m_hooks.selected();
        if (!inst) {
            QMessageBox::information(this, tr("Servers"), tr("Primero selecciona una instancia."));
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Añadir server"));
        auto* form = new QFormLayout(&dlg);
        auto* name = new QLineEdit(&dlg);
        auto* ip = new QLineEdit(&dlg);
        ip->setPlaceholderText("play.miserver.cl");
        form->addRow(tr("Nombre"), name);
        form->addRow(tr("Dirección"), ip);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted || ip->text().trimmed().isEmpty())
            return;
        if (inst->isRunning()) {
            QMessageBox::warning(this, tr("Servers"), tr("Cierra Minecraft antes de añadir servers."));
            return;
        }
        addServer(inst->gameRoot() + "/servers.dat", name->text().trimmed().isEmpty() ? ip->text().trimmed() : name->text().trimmed(),
                  ip->text().trimmed());
        awardForServer(ip->text().trimmed());
        refreshServers();
    });
    v->addWidget(header(tr("Servers"), tr("Tus servidores guardados. Entra directo con la instancia correcta."), { refresh, add }));

    auto* cols = new QHBoxLayout();
    cols->setSpacing(20);
    QWidget* host;
    auto* scroll = scrollFor(page, host);
    m_serversList = new QVBoxLayout(host);
    m_serversList->setContentsMargins(0, 0, 0, 0);
    m_serversList->setSpacing(12);
    m_serversList->setAlignment(Qt::AlignTop);
    cols->addWidget(scroll, 1);

    auto* direct = makeCard(page);
    direct->setFixedWidth(308);
    auto* dv = new QVBoxLayout(direct);
    dv->setContentsMargins(18, 18, 18, 18);
    dv->setSpacing(10);
    dv->addWidget(makeLabel(tr("CONEXIÓN DIRECTA"), QStringLiteral("fpsCaption"), direct));
    dv->addWidget(makeLabel(tr("Dirección del servidor"), QStringLiteral("fpsLabel"), direct));
    m_directAddress = new QLineEdit(direct);
    m_directAddress->setObjectName(QStringLiteral("fpsInput"));
    m_directAddress->setPlaceholderText("ej: play.miserver.cl");
    dv->addWidget(m_directAddress);
    dv->addWidget(makeLabel(tr("Instancia"), QStringLiteral("fpsLabel"), direct));
    m_directInstance = new QComboBox(direct);
    m_directInstance->setObjectName(QStringLiteral("fpsSelect"));
    dv->addWidget(m_directInstance);
    auto* connectBtn = makeButton(tr("Conectar"), QString(), true, direct);
    connectBtn->setMinimumHeight(40);
    connect(connectBtn, &QPushButton::clicked, this, [this] {
        const QString ip = m_directAddress->text().trimmed();
        auto* inst = APPLICATION->instances()->getInstanceById(m_directInstance->currentData().toString());
        if (ip.isEmpty() || !inst)
            return;
        awardForServer(ip);
        if (inst->isRunning() || !fps::confirmLaunch(this, inst))
            return;
        m_currentServer = ip;
        watchLaunch(inst);
        APPLICATION->launch(inst, LaunchMode::Normal, std::make_shared<MinecraftTarget>(MinecraftTarget::parse(ip, false)));
    });
    dv->addWidget(connectBtn);
    cols->addWidget(direct, 0, Qt::AlignTop);
    v->addLayout(cols, 1);
    return page;
}

void FourFpsShell::refreshServers()
{
    if (!m_serversList)
        return;
    clearLayout(m_serversList);
    auto* list = APPLICATION->instances();
    auto* sel = m_hooks.selected();
    m_directInstance->clear();
    for (int i = 0; i < list->count(); i++)
        m_directInstance->addItem(list->at(i)->name(), list->at(i)->id());
    if (sel)
        m_directInstance->setCurrentIndex(m_directInstance->findData(sel->id()));
    if (!sel) {
        m_serversList->addWidget(makeLabel(tr("Selecciona una instancia para ver sus servers."), QStringLiteral("fpsMuted"), m_serversList->parentWidget()));
        return;
    }
    const auto servers = readServers(sel->gameRoot() + "/servers.dat");
    for (const auto& s : servers)
        awardForServer(s.ip);  // servers añadidos desde el juego también cuentan
    if (servers.isEmpty())
        m_serversList->addWidget(makeLabel(tr("%1 no tiene servers guardados. Usa \"Añadir server\".").arg(sel->name()), QStringLiteral("fpsMuted"),
                                           m_serversList->parentWidget()));
    for (const auto& s : servers) {
        auto* row = makeCard(m_serversList->parentWidget());
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(16, 14, 16, 14);
        h->setSpacing(14);
        auto* icon = new QLabel(row);
        icon->setObjectName(QStringLiteral("fpsAvatar"));
        icon->setFixedSize(52, 52);
        icon->setAlignment(Qt::AlignCenter);
        // logo: el que guardó el juego, el de caché, y si no, se pide al server
        QPixmap logo;
        const QString cache = serverIconCache(s.ip);
        if (!s.icon.isEmpty())
            logo.loadFromData(s.icon, "PNG");
        if (logo.isNull())
            logo.load(cache);
        auto setLogo = [icon](const QPixmap& pm) {
            icon->setPixmap(pm.scaled(52, 52, Qt::KeepAspectRatio, Qt::FastTransformation));
        };
        if (!logo.isNull())
            setLogo(logo);
        else
            icon->setPixmap(QIcon::fromTheme("server").pixmap(24, 24));
        h->addWidget(icon);
        auto* t = new QVBoxLayout();
        t->setSpacing(1);
        t->addWidget(makeLabel(fps::streamSafe(s.name, tr("Servidor")), QStringLiteral("fpsRowTitle"), row));
        auto* meta = makeLabel(QStringLiteral("%1 · %2").arg(loaderOf(sel), mcVersionOf(sel)), QStringLiteral("fpsMuted"), row);
        t->addWidget(meta);
        t->addWidget(makeLabel(fps::streamSafe(s.ip), QStringLiteral("fpsHint"), row));
        {
            QPointer<QLabel> pIcon(icon), pMeta(meta);
            const QString base = meta->text();
            const bool hadLogo = !logo.isNull();
            fps::pingServer(row, s.ip, [pIcon, pMeta, base, cache, hadLogo](fps::PingResult r) {
                if (pMeta)
                    pMeta->setText(r.playersText().isEmpty() ? base + QStringLiteral(" · ") + QObject::tr("sin respuesta")
                                                       : base + QStringLiteral(" · ") + QObject::tr("%1 en línea").arg(r.playersText()));
                if (!r.icon.isNull()) {
                    r.icon.save(cache, "PNG");
                    if (pIcon && !hadLogo)
                        pIcon->setPixmap(r.icon.scaled(52, 52, Qt::KeepAspectRatio, Qt::FastTransformation));
                }
            });
        }
        h->addLayout(t, 1);
        auto* join = makeButton(tr("Unirse"), QString(), true, row);
        const QString ip = s.ip;
        connect(join, &QPushButton::clicked, this, [this, sel, ip] {
            awardForServer(ip);
            if (sel->isRunning() || !fps::confirmLaunch(this, sel))
                return;
            m_currentServer = ip;
            watchLaunch(sel);
            APPLICATION->launch(sel, LaunchMode::Normal, std::make_shared<MinecraftTarget>(MinecraftTarget::parse(ip, false)));
        });
        h->addWidget(join);
        auto* cfg = makeButton(QString(), "settings", false, row);
        cfg->setObjectName(QStringLiteral("fpsIconButton"));
        cfg->setFixedSize(34, 34);
        cfg->setToolTip(tr("Editar servers"));
        connect(cfg, &QPushButton::clicked, this, [this] { m_hooks.openPage("servers"); });
        h->addWidget(cfg);
        m_serversList->addWidget(row);
    }
}

// ======================================================================= Skins
QWidget* FourFpsShell::buildSkinsPage()
{
    QVBoxLayout* v;
    auto* page = pageShell(this, v);
    auto* render = makeButton(tr("Hacer render"), "screenshots", true, page);
    connect(render, &QPushButton::clicked, this, [this] { showPage(7); });
    auto* h = header(tr("Skins"), QString(), { render });
    m_skinsSubtitle = h->findChild<QLabel*>(QStringLiteral("fpsSubtitle"));
    v->addWidget(h);
    // pestanas: Ropa y accesorios / Mi skin
    auto* tabs = new QHBoxLayout();
    tabs->setSpacing(4);
    auto* tg = new QButtonGroup(this);
    m_skinsStack = new QStackedWidget(page);
    const QStringList names = { tr("Ropa y accesorios"), tr("Capas"), tr("Mi skin") };
    for (int i = 0; i < names.size(); i++) {
        auto* b = new QPushButton(names[i], page);
        b->setObjectName(QStringLiteral("fpsTab"));
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setCursor(Qt::PointingHandCursor);
        tg->addButton(b);
        connect(b, &QPushButton::clicked, this, [this, i] {
            m_skinsStack->setCurrentIndex(i);
            if (i == 0)
                refreshWardrobe();
            else if (i == 1)
                refreshCapes();
            else
                refreshSkins();
        });
        tabs->addWidget(b);
    }
    tabs->addStretch();
    auto* tabsHost = new QFrame(page);
    tabsHost->setObjectName(QStringLiteral("fpsTabBar"));
    tabsHost->setLayout(tabs);
    tabs->setContentsMargins(0, 0, 0, 0);
    v->addWidget(tabsHost);
    m_skinsStack->addWidget(buildWardrobe(m_skinsStack));
    m_skinsStack->addWidget(buildCapesPanel(m_skinsStack));
    auto* host = new QWidget(m_skinsStack);
    m_skinsHost = new QVBoxLayout(host);
    m_skinsHost->setContentsMargins(0, 0, 0, 0);
    m_skinsStack->addWidget(host);
    v->addWidget(m_skinsStack, 1);
    return page;
}

void FourFpsShell::refreshSkins()
{
    if (!m_skinsHost)
        return;
    if (m_skinsStack && m_skinsStack->currentIndex() == 0) {
        refreshWardrobe();
        return;
    }
    if (m_skinsStack && m_skinsStack->currentIndex() == 1) {
        refreshCapes();
        return;
    }
    auto account = APPLICATION->accounts()->defaultAccount();
    const QString id = account ? account->internalId() : QString();
    if (m_skinDialog && id == m_skinAccountId && m_skinDialog->isVisible())
        return;
    clearLayout(m_skinsHost);
    m_skinDialog = nullptr;
    m_skinAccountId = id;
    if (!account || account->accountType() != AccountType::MSA) {
        m_skinsSubtitle->setText(tr("Cambia tu skin y tu capa."));
        auto* card = makeCard(m_skinsHost->parentWidget());
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(24, 24, 24, 24);
        auto* l = makeLabel(tr("Para cambiar tu skin necesitas una cuenta premium (Microsoft) activa.\nIgual puedes usar \"Hacer render\" con cualquier skin PNG."),
                            QStringLiteral("fpsMuted"), card);
        l->setWordWrap(true);
        cv->addWidget(l);
        auto* go = makeButton(tr("Ir a Cuenta"), "accounts", true, card);
        connect(go, &QPushButton::clicked, this, [this] { showPage(0); });
        cv->addWidget(go, 0, Qt::AlignLeft);
        m_skinsHost->addWidget(card, 0, Qt::AlignTop);
        return;
    }
    m_skinsSubtitle->setText(tr("Cambia tu skin y tu capa. Se aplica a la cuenta %1.").arg(account->profileName()));
    auto* dlg = new SkinManageDialog(m_skinsHost->parentWidget(), account);
    dlg->setWindowFlags(Qt::Widget);
    dlg->setObjectName(QStringLiteral("fpsSkinPanel"));
    connect(dlg, &QDialog::finished, this, [this] {
        m_skinAccountId.clear();
        QTimer::singleShot(0, this, &FourFpsShell::refreshSkins);
    });
    m_skinsHost->addWidget(dlg);
    dlg->show();
    m_skinDialog = dlg;
}


// ---------------------------------------------------------------- texturas del juego
namespace {
const QList<QPair<QString, QString>>& itemList()
{
    static const QList<QPair<QString, QString>> items = {
        { "diamond_sword", QObject::tr("Espada de diamante") },
        { "netherite_sword", QObject::tr("Espada de netherita") },
        { "iron_sword", QObject::tr("Espada de hierro") },
        { "golden_sword", QObject::tr("Espada de oro") },
        { "stone_sword", QObject::tr("Espada de piedra") },
        { "wooden_sword", QObject::tr("Espada de madera") },
        { "mace", QObject::tr("Mazo") },
        { "diamond_axe", QObject::tr("Hacha de diamante") },
        { "netherite_axe", QObject::tr("Hacha de netherita") },
        { "diamond_pickaxe", QObject::tr("Pico de diamante") },
        { "netherite_pickaxe", QObject::tr("Pico de netherita") },
        { "bow", QObject::tr("Arco") },
        { "crossbow_standby", QObject::tr("Ballesta") },
        { "trident", QObject::tr("Tridente") },
        { "fishing_rod", QObject::tr("Caña de pescar") },
        { "totem_of_undying", QObject::tr("Tótem de la inmortalidad") },
        { "golden_apple", QObject::tr("Manzana dorada") },
        { "ender_pearl", QObject::tr("Perla de ender") },
        { "fire_charge", QObject::tr("Carga ígnea") },
        { "snowball", QObject::tr("Bola de nieve") },
        { "blaze_rod", QObject::tr("Vara de blaze") },
        { "nether_star", QObject::tr("Estrella del Nether") },
        { "diamond", QObject::tr("Diamante") },
        { "emerald", QObject::tr("Esmeralda") },
        { "netherite_ingot", QObject::tr("Lingote de netherita") },
        { "cooked_beef", QObject::tr("Filete") },
    };
    return items;
}

// lee armaduras y objetos del .jar del Minecraft que el jugador ya descargo con el launcher
bool loadGameTextures(QString* jarUsed)
{
    if (!SkinRenderer::gameTextures().isEmpty())
        return true;
    QFileInfoList jars;
    QDir root(APPLICATION->dataRoot() + "/libraries/com/mojang/minecraft");
    for (const auto& dir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        for (const auto& jar : QDir(dir.absoluteFilePath()).entryInfoList({ "*client*.jar" }, QDir::Files))
            jars << jar;
    std::sort(jars.begin(), jars.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
    if (jars.isEmpty())
        return false;

    QHash<QString, QString> wanted;  // ruta dentro del jar -> clave
    for (const auto& m : SkinRenderer::armorMaterials()) {
        wanted.insert("assets/minecraft/textures/entity/equipment/humanoid/" + m + ".png", "armor/" + m + "/1");
        wanted.insert("assets/minecraft/textures/entity/equipment/humanoid_leggings/" + m + ".png", "armor/" + m + "/2");
        wanted.insert("assets/minecraft/textures/models/armor/" + m + "_layer_1.png", "armor/" + m + "/1");
        wanted.insert("assets/minecraft/textures/models/armor/" + m + "_layer_2.png", "armor/" + m + "/2");
    }
    for (const auto& it : itemList())
        wanted.insert("assets/minecraft/textures/item/" + it.first + ".png", "item/" + it.first);

    QHash<QString, QImage> found;
    MMCZip::ArchiveReader reader(jars.first().absoluteFilePath());
    auto result = reader.parse([&wanted, &found](MMCZip::ArchiveReader::File* file) -> Result<bool> {
        const QString name = file->filename();
        auto it = wanted.find(name);
        if (it != wanted.end()) {
            auto data = file->readAll();
            if (data) {
                QImage img;
                if (img.loadFromData(data.value())) {
                    // capas animadas o tiras: primer cuadro cuadrado
                    if (it.value().startsWith("item/") && img.height() > img.width())
                        img = img.copy(0, 0, img.width(), img.width());
                    found.insert(it.value(), img);
                }
            }
        }
        return false;
    });
    if (found.isEmpty())
        return false;
    SkinRenderer::setGameTextures(found);
    if (jarUsed)
        *jarUsed = jars.first().fileName();
    return true;
}
}  // namespace

// ====================================================================== Render
QWidget* FourFpsShell::buildRenderPage()
{
    QVBoxLayout* v;
    auto* page = pageShell(this, v);
    const QString rendersDir = APPLICATION->dataRoot() + "/renders";
    const QString posesFile = APPLICATION->dataRoot() + "/poses.json";

    auto* top = new QHBoxLayout();
    auto* back = makeButton(QStringLiteral("<"), QString(), false, page);
    back->setObjectName(QStringLiteral("fpsIconButton"));
    back->setFixedSize(40, 40);
    connect(back, &QPushButton::clicked, this, [this] { showPage(6); });
    top->addWidget(back, 0, Qt::AlignVCenter);
    top->addSpacing(10);
    auto* mine = makeButton(tr("Mis renders"), "viewfolder", false, page);
    connect(mine, &QPushButton::clicked, this, [rendersDir] { DesktopServices::openPath(rendersDir, true); });
    auto* h = header(tr("Render de skin"), tr("Skin de tu cuenta · modelo clásico"), { mine });
    m_renderSubtitle = h->findChild<QLabel*>(QStringLiteral("fpsSubtitle"));
    top->addWidget(h, 1);
    v->addLayout(top);

    auto* cols = new QHBoxLayout();
    cols->setSpacing(20);
    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(12);
    auto* viewCard = makeCard(page);
    auto* vv = new QVBoxLayout(viewCard);
    vv->setContentsMargins(0, 0, 0, 10);
    m_render = new SkinRenderView(viewCard);
    vv->addWidget(m_render, 1);
    vv->addWidget(makeLabel(tr("Clic en una parte del cuerpo y arrastra para rotarla (Shift = eje Z) · arrastra el fondo para girar la cámara · rueda = zoom"),
                            QStringLiteral("fpsHint"), viewCard),
                  0, Qt::AlignHCenter);
    leftCol->addWidget(viewCard, 1);

    // ---- panel derecho con scroll
    auto* panelScroll = new QScrollArea(page);
    panelScroll->setObjectName(QStringLiteral("fpsScroll"));
    panelScroll->setWidgetResizable(true);
    panelScroll->setFrameShape(QFrame::NoFrame);
    panelScroll->setFixedWidth(350);
    panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* panel = makeCard(panelScroll);
    panelScroll->setWidget(panel);
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(18, 18, 18, 18);
    pv->setSpacing(8);

    // editor de huesos (se declara antes para que las poses lo actualicen)
    auto* boneCombo = new NoWheelComboBox(panel);
    boneCombo->setObjectName(QStringLiteral("fpsSelect"));
    for (int i = 0; i < PartCount; i++)
        boneCombo->addItem(SkinRenderer::partName(i));
    auto axis = std::make_shared<std::array<QSlider*, 3>>();
    auto axisValue = std::make_shared<std::array<QLabel*, 3>>();
    auto syncSliders = [this, boneCombo, axis, axisValue]() {
        if (!(*axis)[0])
            return;
        const int part = boneCombo->currentIndex();
        const PartRot& r = m_render->options.parts[part];
        const double vals[3] = { r.x, r.y, r.z };
        for (int i = 0; i < 3; i++) {
            (*axis)[i]->blockSignals(true);
            (*axis)[i]->setValue(int(vals[i]));
            (*axis)[i]->blockSignals(false);
            (*axisValue)[i]->setText(QStringLiteral("%1°").arg(int(vals[i])));
        }
    };

    // poses de ejemplo
    auto* poses = new QHBoxLayout();
    poses->setSpacing(10);
    auto* pg = new QButtonGroup(this);
    const QStringList poseNames = { tr("De pie"), tr("Caminando"), tr("Saludando"), tr("Corriendo"), tr("Sentado"), tr("Pelea") };
    auto syncPtr = std::make_shared<std::function<void()>>();
    for (int i = 0; i < poseNames.size(); i++) {
        auto* b = new QPushButton(poseNames[i], page);
        b->setObjectName(QStringLiteral("fpsPoseTile"));
        b->setCheckable(true);
        b->setChecked(i == 1);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumHeight(70);
        SkinRenderOptions o;
        SkinRenderer::applyPreset(i, o);
        o.background = 0;
        o.shadow = false;
        o.yaw = 25;
        b->setIcon(QPixmap::fromImage(SkinRenderer::renderImage(48, QImage(), QImage(), o)));
        b->setIconSize(QSize(40, 40));
        pg->addButton(b);
        connect(b, &QPushButton::clicked, this, [this, i, syncPtr] {
            SkinRenderer::applyPreset(i, m_render->options);
            m_render->update();
            (*syncPtr)();
        });
        poses->addWidget(b);
    }
    leftCol->addLayout(poses);
    cols->addLayout(leftCol, 1);

    auto segRow = [this, panel, pv](const QString& caption, const QStringList& labels, int checked, std::function<void(int)> fn) {
        pv->addWidget(makeLabel(caption, QStringLiteral("fpsCaption"), panel));
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* g = new QButtonGroup(panel);
        for (int i = 0; i < labels.size(); i++) {
            auto* b = new QPushButton(labels[i], panel);
            b->setObjectName(QStringLiteral("fpsSegment"));
            b->setCheckable(true);
            b->setChecked(i == checked);
            b->setCursor(Qt::PointingHandCursor);
            g->addButton(b);
            connect(b, &QPushButton::clicked, this, [fn, i] { fn(i); });
            row->addWidget(b);
        }
        pv->addLayout(row);
        pv->addSpacing(6);
    };

    // ---- POSE PERSONALIZADA
    pv->addWidget(makeLabel(tr("POSE PERSONALIZADA"), QStringLiteral("fpsCaption"), panel));
    pv->addWidget(makeLabel(tr("Hueso"), QStringLiteral("fpsLabel"), panel));
    pv->addWidget(boneCombo);
    const char* axisNames[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        auto* name = makeLabel(QString::fromLatin1(axisNames[i]), QStringLiteral("fpsAxis"), panel);
        name->setFixedWidth(16);
        row->addWidget(name);
        (*axis)[i] = new QSlider(Qt::Horizontal, panel);
        (*axis)[i]->setRange(-180, 180);
        row->addWidget((*axis)[i], 1);
        (*axisValue)[i] = makeLabel(QStringLiteral("0°"), QStringLiteral("fpsStatValue"), panel);
        (*axisValue)[i]->setFixedWidth(42);
        (*axisValue)[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget((*axisValue)[i]);
        pv->addLayout(row);
        const int ax = i;
        QLabel* valueLabel = (*axisValue)[i];
        connect((*axis)[i], &QSlider::valueChanged, this, [this, boneCombo, ax, valueLabel](int val) {
            PartRot& r = m_render->options.parts[boneCombo->currentIndex()];
            (ax == 0 ? r.x : ax == 1 ? r.y : r.z) = val;
            valueLabel->setText(QStringLiteral("%1°").arg(val));
            m_render->update();
        });
    }
    *syncPtr = syncSliders;
    connect(boneCombo, &QComboBox::currentIndexChanged, this, [this, syncPtr](int idx) {
        m_render->selectPart(idx);
        (*syncPtr)();
    });
    connect(m_render, &SkinRenderView::partSelected, this, [boneCombo](int part) { boneCombo->setCurrentIndex(part); });
    connect(m_render, &SkinRenderView::poseEdited, this, [syncPtr] { (*syncPtr)(); });

    auto* resetRow = new QHBoxLayout();
    resetRow->setSpacing(6);
    auto* resetBone = makeButton(tr("Reiniciar hueso"), QString(), false, panel);
    auto* resetAll = makeButton(tr("Reiniciar pose"), QString(), false, panel);
    connect(resetBone, &QPushButton::clicked, this, [this, boneCombo, syncPtr] {
        m_render->options.parts[boneCombo->currentIndex()] = PartRot{};
        m_render->update();
        (*syncPtr)();
    });
    connect(resetAll, &QPushButton::clicked, this, [this, syncPtr] {
        for (auto& r : m_render->options.parts)
            r = PartRot{};
        m_render->update();
        (*syncPtr)();
    });
    resetRow->addWidget(resetBone);
    resetRow->addWidget(resetAll);
    pv->addLayout(resetRow);

    // mis poses guardadas
    auto* myPoses = new NoWheelComboBox(panel);
    myPoses->setObjectName(QStringLiteral("fpsSelect"));
    auto loadPoses = [posesFile]() {
        QFile f(posesFile);
        if (!f.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(f.readAll()).object();
    };
    auto fillPoses = [myPoses, loadPoses] {
        myPoses->clear();
        myPoses->addItem(QObject::tr("Mis poses..."));
        for (const auto& k : loadPoses().keys())
            myPoses->addItem(k);
    };
    fillPoses();
    connect(myPoses, &QComboBox::activated, this, [this, myPoses, loadPoses, syncPtr](int idx) {
        if (idx <= 0)
            return;
        const QJsonArray arr = loadPoses().value(myPoses->itemText(idx)).toArray();
        for (int i = 0; i < PartCount && i * 3 + 2 < arr.size(); i++)
            m_render->options.parts[i] = PartRot{ arr[i * 3].toDouble(), arr[i * 3 + 1].toDouble(), arr[i * 3 + 2].toDouble() };
        m_render->update();
        (*syncPtr)();
    });
    auto* savePose = makeButton(tr("Guardar pose"), QString(), false, panel);
    connect(savePose, &QPushButton::clicked, this, [this, posesFile, loadPoses, fillPoses] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Guardar pose"), tr("Nombre de la pose:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        QJsonObject all = loadPoses();
        QJsonArray arr;
        for (const auto& r : m_render->options.parts)
            arr << r.x << r.y << r.z;
        all.insert(name, arr);
        QFile f(posesFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(all).toJson());
        fillPoses();
    });
    auto* poseRow = new QHBoxLayout();
    poseRow->setSpacing(6);
    poseRow->addWidget(myPoses, 1);
    poseRow->addWidget(savePose);
    pv->addLayout(poseRow);
    pv->addSpacing(10);

    static int s_size = 1024;
    segRow(tr("VISTA"), { tr("Frente"), "3/4", tr("Perfil"), tr("Espalda") }, 1, [this](int i) {
        const double yaws[] = { 0, 35, 90, 180 };
        m_render->options.yaw = yaws[i];
        m_render->options.pitch = i == 0 || i == 3 ? 5 : 12;
        m_render->update();
    });
    segRow(tr("FONDO"), { tr("Transparente"), tr("Gris"), tr("Negro") }, 1, [this](int i) {
        m_render->options.background = i;
        m_render->update();
    });
    segRow(tr("TAMAÑO"), { "512", "1024", "2048" }, 1, [](int i) { s_size = i == 0 ? 512 : i == 1 ? 1024 : 2048; });
    segRow(tr("MODELO"), { tr("Clásico"), tr("Slim") }, 0, [this](int i) {
        m_render->options.slim = i == 1;
        m_render->update();
    });
    pv->addWidget(makeLabel(tr("EXTRAS"), QStringLiteral("fpsCaption"), panel));
    auto* capeBox = new QCheckBox(tr("Mostrar capa"), panel);
    capeBox->setChecked(true);
    connect(capeBox, &QCheckBox::toggled, this, [this](bool b) {
        m_render->options.showCape = b;
        m_render->update();
    });
    pv->addWidget(capeBox);
    auto* shadowBox = new QCheckBox(tr("Sombra en el piso"), panel);
    shadowBox->setChecked(true);
    connect(shadowBox, &QCheckBox::toggled, this, [this](bool b) {
        m_render->options.shadow = b;
        m_render->update();
    });
    pv->addWidget(shadowBox);
    pv->addSpacing(6);
    segRow(tr("SOMBREADO"), { tr("Suave (shader)"), tr("Plano") }, 0, [this](int i) {
        m_render->options.shading = i == 0 ? 1 : 0;
        m_render->update();
    });

    // armaduras y objetos (texturas del Minecraft instalado)
    auto ensureGame = [this]() {
        if (!SkinRenderer::gameTextures().isEmpty())
            return true;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = loadGameTextures(nullptr);
        QApplication::restoreOverrideCursor();
        if (!ok)
            QMessageBox::information(this, tr("Armaduras y objetos"),
                                     tr("Para usar armaduras y objetos, primero juega Minecraft al menos una vez con 4fps: "
                                        "las texturas se leen de tu juego descargado."));
        return ok;
    };
    pv->addWidget(makeLabel(tr("ARMADURA"), QStringLiteral("fpsCaption"), panel));
    const QStringList matNames = { tr("Nada"), tr("Cuero"), tr("Malla"), tr("Hierro"), tr("Oro"), tr("Diamante"), tr("Netherita") };
    const QStringList slotNames = { tr("Casco"), tr("Peto"), tr("Pantalones"), tr("Botas") };
    auto* armorGrid = new QGridLayout();
    armorGrid->setSpacing(6);
    for (int i = 0; i < 4; i++) {
        armorGrid->addWidget(makeLabel(slotNames[i], QStringLiteral("fpsLabel"), panel), i, 0);
        auto* cb = new NoWheelComboBox(panel);
        cb->setObjectName(QStringLiteral("fpsSelect"));
        cb->addItems(matNames);
        connect(cb, &QComboBox::activated, this, [this, cb, i, ensureGame](int idx) {
            if (idx > 0 && !ensureGame()) {
                cb->setCurrentIndex(0);
                return;
            }
            m_render->options.armor[i] = idx;
            m_render->update();
        });
        armorGrid->addWidget(cb, i, 1);
    }
    auto* fullSet = new NoWheelComboBox(panel);
    fullSet->setObjectName(QStringLiteral("fpsSelect"));
    fullSet->addItem(tr("Set completo..."));
    fullSet->addItems(matNames.mid(1));
    pv->addLayout(armorGrid);
    connect(fullSet, &QComboBox::activated, this, [this, fullSet, armorGrid, ensureGame](int idx) {
        if (idx <= 0 || !ensureGame()) {
            fullSet->setCurrentIndex(0);
            return;
        }
        for (int i = 0; i < 4; i++) {
            m_render->options.armor[i] = idx;
            if (auto* cb = qobject_cast<QComboBox*>(armorGrid->itemAtPosition(i, 1)->widget()))
                cb->setCurrentIndex(idx);
        }
        fullSet->setCurrentIndex(0);
        m_render->update();
    });
    pv->addWidget(fullSet);
    pv->addSpacing(6);
    pv->addWidget(makeLabel(tr("OBJETOS EN LA MANO"), QStringLiteral("fpsCaption"), panel));
    auto* handsGrid = new QGridLayout();
    handsGrid->setSpacing(6);
    for (int hand = 0; hand < 2; hand++) {
        handsGrid->addWidget(makeLabel(hand == 0 ? tr("Derecha") : tr("Izquierda"), QStringLiteral("fpsLabel"), panel), hand, 0);
        auto* cb = new NoWheelComboBox(panel);
        cb->setObjectName(QStringLiteral("fpsSelect"));
        cb->addItem(tr("Nada"), QString());
        for (const auto& it : itemList())
            cb->addItem(it.second, it.first);
        connect(cb, &QComboBox::activated, this, [this, cb, hand, ensureGame](int idx) {
            if (idx > 0 && !ensureGame()) {
                cb->setCurrentIndex(0);
                return;
            }
            const QString id = cb->itemData(idx).toString();
            (hand == 0 ? m_render->options.heldRight : m_render->options.heldLeft) = id;
            m_render->update();
        });
        handsGrid->addWidget(cb, hand, 1);
    }
    pv->addLayout(handsGrid);
    pv->addSpacing(6);
    auto* loadSkin = makeButton(tr("Usar otra skin (PNG)"), "export", false, panel);
    connect(loadSkin, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Elegir skin"), QString(), tr("Skin (*.png)"));
        if (f.isEmpty())
            return;
        QImage img(f);
        if (img.width() != 64 || (img.height() != 64 && img.height() != 32)) {
            QMessageBox::warning(this, tr("Skin"), tr("La skin debe ser un PNG de 64×64 (o 64×32)."));
            return;
        }
        m_render->setSkin(img, m_render->cape);
        m_renderSubtitle->setText(tr("Skin: %1").arg(QFileInfo(f).completeBaseName()));
    });
    pv->addWidget(loadSkin);
    auto* loadCape = makeButton(tr("Usar otra capa (PNG)"), "export", false, panel);
    connect(loadCape, &QPushButton::clicked, this, [this, capeBox] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Elegir capa"), QString(), tr("Capa (*.png)"));
        if (f.isEmpty())
            return;
        QImage img(f);
        if (img.isNull() || img.width() < 64 || img.width() % 64 != 0 || img.height() < img.width() / 2) {
            QMessageBox::warning(this, tr("Capa"), tr("La capa debe ser un PNG en formato de capa (64×32, o más grande en la misma proporción)."));
            return;
        }
        m_render->setSkin(m_render->skin, img);
        capeBox->setChecked(true);
    });
    pv->addWidget(loadCape);
    pv->addSpacing(8);

    auto* gen = makeButton(tr("Generar render"), "screenshots", true, panel);
    gen->setMinimumHeight(42);
    auto* save = makeButton(tr("Guardar PNG"), "export", false, panel);
    save->setMinimumHeight(38);
    auto* noBg = makeButton(tr("Exportar sin fondo (PNG)"), "export", false, panel);
    noBg->setMinimumHeight(38);
    auto doRender = [this](bool transparent) {
        SkinRenderOptions o = m_render->options;
        if (transparent)
            o.background = 0;
        return SkinRenderer::renderImage(s_size, m_render->skin, m_render->cape, o);
    };
    connect(gen, &QPushButton::clicked, this, [this, doRender, rendersDir] {
        QDir().mkpath(rendersDir);
        const QString file = rendersDir + "/render_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH.mm.ss") + ".png";
        if (doRender(false).save(file))
            QMessageBox::information(this, tr("Render listo"), tr("Guardado en Mis renders:\n%1").arg(QFileInfo(file).fileName()));
    });
    connect(save, &QPushButton::clicked, this, [this, doRender] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Guardar render"), "render.png", tr("PNG (*.png)"));
        if (!f.isEmpty())
            doRender(false).save(f, "PNG");
    });
    connect(noBg, &QPushButton::clicked, this, [this, doRender] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Exportar sin fondo"), "render_sin_fondo.png", tr("PNG (*.png)"));
        if (!f.isEmpty())
            doRender(true).save(f, "PNG");
    });
    pv->addWidget(gen);
    pv->addWidget(save);
    pv->addWidget(noBg);
    pv->addStretch(1);
    cols->addWidget(panelScroll);
    v->addLayout(cols, 1);

    QTimer::singleShot(0, this, [syncPtr] { (*syncPtr)(); });

    // skin de la cuenta activa
    auto loadAccountSkin = [this] {
        auto account = APPLICATION->accounts()->defaultAccount();
        QImage skin, cape;
        if (account) {
            auto* data = account->accountData();
            skin.loadFromData(data->minecraftProfile.skin.data);
            const QString capeId = data->minecraftProfile.currentCape;
            if (data->minecraftProfile.capes.contains(capeId))
                cape.loadFromData(data->minecraftProfile.capes[capeId].data);
            if (cape.isNull())
                cape = m_capeTextures.value(capeId);
            m_render->options.slim = data->minecraftProfile.skin.variant.compare("slim", Qt::CaseInsensitive) == 0;
            m_renderSubtitle->setText(tr("Skin: %1 · modelo %2").arg(account->profileName(), m_render->options.slim ? tr("slim") : tr("clásico")));
        }
        m_render->setSkin(skin, cape);
    };
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, this, loadAccountSkin);
    QTimer::singleShot(0, this, loadAccountSkin);
    return page;
}
