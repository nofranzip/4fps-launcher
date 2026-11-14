// 4fps - Cliente PvP: instala el jar del 4fps Client (mod propio, id "fourfps") que corresponde a la version de cada instancia, desde pack4fps/client
#include "FourFpsShell.h"
#include "FourFpsFeatures.h"

#include <QDebug>
#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QUrlQuery>
#include <QCoreApplication>
#include <memory>

#include "Application.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

namespace {
const char* kPvpEnabled = "FourFpsPvpClient";
const char* kPvpFiles = "FourFpsPvpFiles";
const char* kSharedCapesEnabled = "FourFpsSharedCapes";
const char* kSharedCapesFiles = "FourFpsSharedCapeFiles";

void ensureSettings(MinecraftInstance* inst)
{
    auto s = inst->settings();
    if (!s->contains(kPvpEnabled))
        s->registerSetting(kPvpEnabled, true);
    if (!s->contains(kPvpFiles))
        s->registerSetting(kPvpFiles, QStringList());
}

QNetworkRequest apiRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("4fps-launcher/1.0 (basado en Prism Launcher)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    return req;
}

// Trabajo de instalacion: cola de proyectos de Modrinth a revisar
struct PvpJob : std::enable_shared_from_this<PvpJob> {
    MinecraftInstance* inst = nullptr;
    QString mc;
    QStringList loaders;
    QStringList queue;  // slugs o project ids
    QSet<QString> seen;
    QStringList installed;
    QProgressDialog* progress = nullptr;
    std::function<void()> done;
    QString filesKey = kPvpFiles;
    QStringList fallback;                 // proyectos a probar si el primero no tiene version
    std::function<bool()> needFallback;
    QString title = QObject::tr("Preparando 4fps...");
    bool onlyDownloaded = false;  // guardar solo lo que 4fps descargó (para poder quitarlo después sin tocar mods del usuario)
    QStringList downloaded;

    QString modsDir() const { return inst->modsRoot(); }

    bool alreadyPresent(const QString& project) const
    {
        // Fabric API puede venir instalada por el usuario con otro nombre
        if (project == "fabric-api" || project == "P7dR8mSH") {
            for (const auto& f : QDir(modsDir()).entryList({ "*.jar" }))
                if (f.startsWith("fabric-api", Qt::CaseInsensitive))
                    return true;
        }
        // si el usuario ya tiene el mod puesto con otro nombre de archivo, no se duplica
        static const QHash<QString, QString> prefixes = { { "oneconfig", "oneconfig" },
                                                         { "fabric-language-kotlin", "fabric-language-kotlin" },
                                                         { "hitbox", "polyhitbox" },
                                                         { "crosshair", "polycrosshair" } };
        // genérico: "ferrite-core" -> archivo "ferritecore-..."
        if (!project.isEmpty() && project.contains(QRegularExpression("[a-z]")) && project.toLower() == project) {
            const QString norm = QString(project).remove('-');
            for (const auto& f : QDir(modsDir()).entryList({ "*.jar" }))
                if (QString(f).toLower().remove('-').remove('_').startsWith(norm) && !downloaded.contains(f) &&
                    !inst->settings()->get(filesKey).toStringList().contains(f))
                    return true;
        }
        if (prefixes.contains(project)) {
            for (const auto& f : QDir(modsDir()).entryList({ "*.jar" }))
                if (f.startsWith(prefixes.value(project), Qt::CaseInsensitive) && !inst->settings()->get(filesKey).toStringList().contains(f))
                    return true;
        }
        return false;
    }

    void next()
    {
        if (queue.isEmpty()) {
            finish();
            return;
        }
        const QString project = queue.takeFirst();
        if (seen.contains(project) || alreadyPresent(project)) {
            next();
            return;
        }
        seen.insert(project);
        if (progress)
            progress->setLabelText(title + QObject::tr("\nBuscando %1").arg(project));
        QUrl url(QStringLiteral("https://api.modrinth.com/v2/project/%1/version").arg(project));
        QUrlQuery q;
        q.addQueryItem("loaders", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(loaders)).toJson(QJsonDocument::Compact)));
        q.addQueryItem("game_versions", QString::fromUtf8(QJsonDocument(QJsonArray{ mc }).toJson(QJsonDocument::Compact)));
        url.setQuery(q);
        auto self = shared_from_this();
        auto* reply = APPLICATION->network()->get(apiRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, reply, [self, reply, project] {
            reply->deleteLater();
            const auto versions = QJsonDocument::fromJson(reply->readAll()).array();
            if (reply->error() != QNetworkReply::NoError || versions.isEmpty()) {
                qWarning() << "4fps PvP: sin version de" << project << "para" << self->mc;
                self->next();
                return;
            }
            const QJsonObject ver = versions.first().toObject();
            for (const auto& d : ver.value("dependencies").toArray()) {
                const auto dep = d.toObject();
                if (dep.value("dependency_type").toString() == "required" && !dep.value("project_id").toString().isEmpty())
                    self->queue << dep.value("project_id").toString();
            }
            QJsonObject file;
            for (const auto& f : ver.value("files").toArray())
                if (f.toObject().value("primary").toBool() || file.isEmpty())
                    file = f.toObject();
            const QString filename = file.value("filename").toString();
            if (filename.isEmpty() || QFile::exists(self->modsDir() + "/" + filename)) {
                if (!filename.isEmpty())
                    self->installed << filename;
                self->next();
                return;
            }
            self->download(QUrl(file.value("url").toString()), filename);
        });
    }

    void download(const QUrl& url, const QString& filename)
    {
        if (progress)
            progress->setLabelText(title + QObject::tr("\nDescargando %1").arg(filename));
        auto self = shared_from_this();
        auto* reply = APPLICATION->network()->get(apiRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, reply, [self, reply, filename] {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QDir().mkpath(self->modsDir());
                QFile out(self->modsDir() + "/" + filename);
                if (out.open(QIODevice::WriteOnly)) {
                    out.write(reply->readAll());
                    out.close();
                    self->installed << filename;
                    self->downloaded << filename;
                }
            } else {
                qWarning() << "4fps PvP: fallo la descarga de" << filename << reply->errorString();
            }
            self->next();
        });
    }

    void finish()
    {
        if (!fallback.isEmpty() && needFallback && needFallback()) {
            queue = fallback;
            fallback.clear();
            next();
            return;
        }
        // borra versiones viejas que instalo 4fps y que ya no se usan
        auto s = inst->settings();
        if (!s->contains(filesKey))
            s->registerSetting(filesKey, QStringList());
        QStringList previous = s->get(filesKey).toStringList();
        if (onlyDownloaded) {
            QStringList keep = previous;
            for (const auto& d : downloaded)
                if (!keep.contains(d))
                    keep << d;
            s->set(filesKey, keep);
        } else if (!installed.isEmpty()) {
            for (const auto& old : previous)
                if (!installed.contains(old))
                    QFile::remove(modsDir() + "/" + old);
            s->set(filesKey, installed);
        }
        if (progress)
            progress->deleteLater();
        if (done)
            done();
    }
};
bool hasCapeMod(MinecraftInstance* inst)
{
    for (const auto& f : QDir(inst->modsRoot()).entryList({ "*.jar", "*.jar.disabled" }))
        if (f.contains("cape", Qt::CaseInsensitive))
            return true;
    return false;
}

std::shared_ptr<PvpJob> makeCapeJob(MinecraftInstance* inst, const QString& mc, const QStringList& loaders)
{
    auto job = std::make_shared<PvpJob>();
    job->inst = inst;
    job->mc = mc;
    job->loaders = loaders;
    job->queue = { "cape-provider", "fabric-api" };
    job->filesKey = QStringLiteral("FourFpsCapeFiles");
    job->title = QObject::tr("Preparando 4fps...");
    // Cape Provider es para 1.21+; en 1.20.x se usa "Client Capes"
    job->fallback = { "client-capes", "fabric-api" };
    job->needFallback = [inst] { return !hasCapeMod(inst); };
    return job;
}
}  // namespace

bool FourFpsShell::pvpSupported(MinecraftInstance* inst)
{
    const QString l = loaderOf(inst);
    return l == "Fabric" || l == "Quilt";
}

bool FourFpsShell::pvpEnabled(MinecraftInstance* inst)
{
    if (!inst)
        return false;
    ensureSettings(inst);
    return inst->settings()->get(kPvpEnabled).toBool();
}

QStringList FourFpsShell::pvpFiles(MinecraftInstance* inst)
{
    if (!inst)
        return {};
    ensureSettings(inst);
    return inst->settings()->get(kPvpFiles).toStringList();
}

bool FourFpsShell::sharedCapesSupported(MinecraftInstance* inst)
{
    // MinecraftCapes existe para Fabric, Quilt, Forge y NeoForge
    const QString l = loaderOf(inst);
    return l == "Fabric" || l == "Quilt" || l == "Forge" || l == "NeoForge";
}

bool FourFpsShell::sharedCapesEnabled(MinecraftInstance* inst)
{
    if (!inst)
        return false;
    auto s = inst->settings();
    if (!s->contains(kSharedCapesEnabled))
        s->registerSetting(kSharedCapesEnabled, true);
    return s->get(kSharedCapesEnabled).toBool();
}

void FourFpsShell::setSharedCapesEnabled(MinecraftInstance* inst, bool on)
{
    if (!inst)
        return;
    sharedCapesEnabled(inst);
    auto s = inst->settings();
    if (!s->contains(kSharedCapesFiles))
        s->registerSetting(kSharedCapesFiles, QStringList());
    s->set(kSharedCapesEnabled, on);
    if (!on) {
        // quita solo lo que instaló 4fps (no toca los mods del usuario)
        for (const auto& f : s->get(kSharedCapesFiles).toStringList())
            QFile::remove(inst->modsRoot() + "/" + f);
        s->set(kSharedCapesFiles, QStringList());
    }
}

void FourFpsShell::setPvpEnabled(MinecraftInstance* inst, bool on)
{
    if (!inst)
        return;
    ensureSettings(inst);
    inst->settings()->set(kPvpEnabled, on);
    if (!on) {
        // quita lo que instalo 4fps (no toca los mods del usuario)
        for (const auto& f : inst->settings()->get(kPvpFiles).toStringList())
            QFile::remove(inst->modsRoot() + "/" + f);
        inst->settings()->set(kPvpFiles, QStringList());
    }
}

void FourFpsShell::launchSelected()
{
    auto* inst = m_hooks.selected ? m_hooks.selected() : nullptr;
    if (!inst)
        return;
    if (!inst->isRunning() && !fps::confirmLaunch(this, inst))
        return;
    m_currentServer.clear();
    if (!inst->isRunning())
        watchLaunch(inst);
    if (inst->isRunning() || (!pvpSupported(inst) && !sharedCapesSupported(inst))) {
        m_hooks.launch();
        return;
    }
    // el mod de capas propias de 4fps es solo para Fabric/Quilt; en Forge/NeoForge se salta a las capas compartidas
    if (!pvpSupported(inst)) {
        launchAfterCapes(inst);
        return;
    }
    // el mod de capas de 4fps viene siempre en las instancias Fabric
    if (!inst->settings()->contains("FourFpsCapeTried"))
        inst->settings()->registerSetting("FourFpsCapeTried", QString());
    if (!hasCapeMod(inst) && inst->settings()->get("FourFpsCapeTried").toString() != mcVersionOf(inst)) {
        auto capeJob = makeCapeJob(inst, mcVersionOf(inst), loaderOf(inst) == "Quilt" ? QStringList{ "quilt", "fabric" } : QStringList{ "fabric" });
        capeJob->progress = new QProgressDialog(capeJob->title, QString(), 0, 0, this);
        capeJob->progress->setWindowTitle(QStringLiteral("4fps"));
        capeJob->progress->setCancelButton(nullptr);
        capeJob->progress->setMinimumDuration(400);
        capeJob->progress->setWindowModality(Qt::WindowModal);
        QPointer<FourFpsShell> self(this);
        capeJob->done = [self, inst, capeJob] {
            if (!self)
                return;
            if (!hasCapeMod(inst))
                inst->settings()->set("FourFpsCapeTried", capeJob->mc);  // no hay version para este Minecraft
            self->m_modsInstance = nullptr;
            self->launchAfterCapes(inst);
        };
        capeJob->next();
        return;
    }
    launchAfterCapes(inst);
}

void FourFpsShell::launchAfterCapes(MinecraftInstance* inst)
{
    // MinecraftCapes: para ver (y que vean) las capas de otros jugadores de 4fps. Se instala como mod visible.
    // Funciona en Fabric, Quilt, Forge y NeoForge.
    if (sharedCapesSupported(inst) && sharedCapesEnabled(inst)) {
        const QString l = loaderOf(inst);
        QStringList capeLoaders = l == "Quilt"      ? QStringList{ "quilt", "fabric" }
                                  : l == "Forge"    ? QStringList{ "forge" }
                                  : l == "NeoForge" ? QStringList{ "neoforge" }
                                                    : QStringList{ "fabric" };
        auto job = std::make_shared<PvpJob>();
        job->inst = inst;
        job->mc = mcVersionOf(inst);
        job->loaders = capeLoaders;
        job->queue = { "minecraftcapes" };  // sus dependencias (si pide Fabric API) se agregan solas
        job->filesKey = QStringLiteral("FourFpsSharedCapeFiles");
        job->title = tr("Preparando capas de 4fps...");
        job->onlyDownloaded = true;  // al apagar el interruptor se quita sin tocar otros mods
        job->progress = new QProgressDialog(job->title, QString(), 0, 0, this);
        job->progress->setWindowTitle(QStringLiteral("4fps"));
        job->progress->setCancelButton(nullptr);
        job->progress->setMinimumDuration(400);
        job->progress->setWindowModality(Qt::WindowModal);
        QPointer<FourFpsShell> self(this);
        job->done = [self, inst] {
            if (!self)
                return;
            self->m_modsInstance = nullptr;  // fuerza a releer la lista de mods
            self->launchAfterSharedCapes(inst);
        };
        job->next();
        return;
    }
    launchAfterSharedCapes(inst);
}

void FourFpsShell::launchAfterSharedCapes(MinecraftInstance* inst)
{
    // el cliente PvP es solo Fabric/Quilt; en Forge/NeoForge no se instala nada
    if (!pvpSupported(inst) || !pvpEnabled(inst)) {
        m_hooks.launch();
        return;
    }
    installClient(inst);  // copia el jar del 4fps Client de esta versión (local, sin red)
    if (m_modsInstance)
        m_modsInstance = nullptr;  // fuerza a releer la lista de mods
    m_hooks.launch();
}

// jar del 4fps Client que corresponde a esta instancia, de pack4fps/client.
// Los jars se llaman 4fps-client-<version del mod>+<version de Minecraft>[-<loader>].jar
QString FourFpsShell::clientJarFor(MinecraftInstance* inst)
{
    const QString mc = mcVersionOf(inst);
    if (mc.isEmpty())
        return {};
    const QString loader = loaderOf(inst).toLower();  // "fabric", "quilt", "forge"...
    const QString dir = QCoreApplication::applicationDirPath() + "/pack4fps/client";
    QString fabricMatch;
    for (const auto& fi : QDir(dir).entryInfoList({ "4fps-client-*.jar" }, QDir::Files, QDir::Name)) {
        QString tag = fi.completeBaseName().section('+', 1);  // lo que viene después del '+'
        if (tag.isEmpty())
            continue;
        const QString ver = tag.section('-', 0, 0);            // versión de Minecraft
        const QString suffix = tag.section('-', 1).toLower();  // loader opcional ("forge")
        if (ver != mc)
            continue;
        if (!suffix.isEmpty()) {
            // jar específico de un loader (p. ej. Forge 1.8.9)
            if (suffix == loader || (suffix == "forge" && loader == "neoforge"))
                return fi.absoluteFilePath();
            continue;
        }
        fabricMatch = fi.absoluteFilePath();  // jar Fabric/Quilt (sin sufijo de loader)
    }
    // Fabric y Quilt usan el jar sin sufijo; Forge/NeoForge solo si hay jar propio (ya habría salido arriba)
    if (loader == "fabric" || loader == "quilt")
        return fabricMatch;
    return {};
}

void FourFpsShell::installClient(MinecraftInstance* inst)
{
    if (!inst)
        return;
    ensureSettings(inst);
    auto* s = inst->settings();
    // quita lo que 4fps haya puesto antes (OneConfig viejo o un jar anterior del cliente), sin tocar los mods del usuario
    for (const auto& f : s->get(kPvpFiles).toStringList())
        QFile::remove(inst->modsRoot() + "/" + f);
    s->set(kPvpFiles, QStringList());
    // quita cualquier jar del 4fps Client que haya quedado suelto (versiones viejas o de otra conversación)
    // y el antiguo cliente PvP (OneConfig/PolyHitbox/PolyCrosshair), para que nunca convivan dos a la vez.
    // No toca fabric-api ni fabric-language-kotlin (otros mods del usuario pueden necesitarlos).
    const QStringList viejos = { "4fps-client*.jar", "OneConfig*.jar", "polyhitbox*.jar", "polycrosshair*.jar" };
    for (const auto& f : QDir(inst->modsRoot()).entryList(viejos, QDir::Files, QDir::Name))
        QFile::remove(inst->modsRoot() + "/" + f);

    const QString jar = clientJarFor(inst);
    if (jar.isEmpty())
        return;  // todavía no hay cliente para esta versión de Minecraft: no instala nada
    QDir().mkpath(inst->modsRoot());
    const QString name = QFileInfo(jar).fileName();
    const QString dest = inst->modsRoot() + "/" + name;
    QFile::remove(dest);
    if (QFile::copy(jar, dest))
        s->set(kPvpFiles, QStringList{ name });
}

// Capa solo para 4fps: instala "Cape Provider" (Fabric, solo cliente) y deja la capa como proveedor local.
// La capa la ve solo quien juega con 4fps; no se sube a Mojang.
void FourFpsShell::useCapeInGame(const QImage& source, const QString& name)
{
    auto* inst = m_hooks.selected ? m_hooks.selected() : nullptr;
    if (!inst) {
        QMessageBox::information(this, tr("Capas"), tr("Primero elige una instancia en Instancias (o abajo, junto a JUGAR)."));
        return;
    }
    if (!pvpSupported(inst)) {
        QMessageBox::information(this, tr("Capas"),
                                 tr("La instancia \"%1\" no usa Fabric. Las capas en el juego necesitan una instancia Fabric "
                                    "(créala en Versiones eligiendo Fabric).")
                                     .arg(inst->name()));
        return;
    }
    auto account = APPLICATION->accounts()->defaultAccount();
    if (!account || account->profileName().isEmpty()) {
        QMessageBox::information(this, tr("Capas"), tr("Primero inicia sesión con tu cuenta en Cuenta."));
        return;
    }
    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Capas"), tr("No se pudo leer la imagen de la capa."));
        return;
    }
    // capas animadas (tira de cuadros): se usa el primer cuadro
    if (img.height() > img.width() / 2 * 1.5)
        img = img.copy(0, 0, img.width(), img.width() / 2);

    const QString dir = inst->gameRoot() + "/config/cape-provider/simple-custom/4fps";
    QDir().mkpath(dir);
    if (!img.save(dir + "/cape.png", "PNG")) {
        QMessageBox::warning(this, tr("Capas"), tr("No se pudo guardar la capa en la instancia."));
        return;
    }
    {
        QFile owners(dir + "/owners.txt");
        if (owners.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            owners.write("# 4fps: la capa se muestra solo en estos jugadores\n");
            QString id = account->profileId();
            if (id.size() == 32)
                id = id.left(8) + "-" + id.mid(8, 4) + "-" + id.mid(12, 4) + "-" + id.mid(16, 4) + "-" + id.mid(20);
            if (id.size() == 36)
                owners.write(id.toUtf8() + "\n");
            owners.write(account->profileName().toUtf8() + "\n");
        }
        // para "Client Capes" (1.20.x): carpeta Capes con solo la capa de 4fps
        const QString capesDir = inst->gameRoot() + "/Capes";
        QDir().mkpath(capesDir);
        img.save(capesDir + "/4fps.png", "PNG");
        QFile n(dir + "/name.txt");
        if (n.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            n.write("4fps\n");
    }

    if (hasCapeMod(inst)) {
        QMessageBox::information(this, tr("Capas"),
                                 tr("Listo: \"%1\" quedó puesta en la instancia \"%2\".\n\n"
                                    "Abre el juego con JUGAR y la verás en tu personaje (F5). "
                                    "Solo se ve en 4fps: los demás jugadores no la ven.\n\n"
                                    "Si el juego ya estaba abierto, ciérralo y vuelve a abrirlo.")
                                     .arg(name, inst->name()));
        return;
    }
    auto job = makeCapeJob(inst, mcVersionOf(inst), loaderOf(inst) == "Quilt" ? QStringList{ "quilt", "fabric" } : QStringList{ "fabric" });
    job->progress = new QProgressDialog(job->title, QString(), 0, 0, this);
    job->progress->setWindowTitle(QStringLiteral("4fps"));
    job->progress->setCancelButton(nullptr);
    job->progress->setMinimumDuration(300);
    job->progress->setWindowModality(Qt::WindowModal);
    QPointer<FourFpsShell> self(this);
    const QString instName = inst->name();
    job->done = [self, job, instName, name] {
        if (!self)
            return;
        self->m_modsInstance = nullptr;
        bool ok = false;
        ok = hasCapeMod(job->inst);
        if (!ok) {
            QMessageBox::warning(self, QObject::tr("Capas"),
                                 QObject::tr("No se encontró el mod de capas para la versión %1. Prueba con otra versión de Fabric (1.21 o más nueva).")
                                     .arg(job->mc));
            return;
        }
        QMessageBox::information(self, QObject::tr("Capas"),
                                 QObject::tr("Listo: \"%1\" quedó puesta en la instancia \"%2\".\n\n"
                                             "Abre el juego con JUGAR y la verás en tu personaje (F5). "
                                             "Solo se ve en 4fps: los demás jugadores no la ven.\n\n"
                                             "Si tu cuenta ya tiene una capa oficial puesta, quítala para que se vea esta.")
                                     .arg(name, instName));
    };
    job->next();
}

// ---------------------------------------------------------------- Modo potato: mods de rendimiento
void FourFpsShell::installPotatoMods(MinecraftInstance* inst)
{
    if (!inst || !pvpSupported(inst))
        return;
    auto job = std::make_shared<PvpJob>();
    job->inst = inst;
    job->mc = mcVersionOf(inst);
    job->loaders = loaderOf(inst) == "Quilt" ? QStringList{ "quilt", "fabric" } : QStringList{ "fabric" };
    job->queue = { "sodium", "lithium", "ferrite-core", "entityculling", "immediatelyfast", "modernfix", "fabric-api" };
    job->filesKey = QStringLiteral("FourFpsPotatoFiles");
    job->onlyDownloaded = true;
    job->title = tr("Preparando Modo potato...");
    job->progress = new QProgressDialog(job->title, QString(), 0, 0, this);
    job->progress->setWindowTitle(QStringLiteral("4fps"));
    job->progress->setCancelButton(nullptr);
    job->progress->setMinimumDuration(300);
    job->progress->setWindowModality(Qt::WindowModal);
    QPointer<FourFpsShell> self(this);
    job->done = [self, job] {
        if (!self)
            return;
        self->m_modsInstance = nullptr;
        if (!job->downloaded.isEmpty())
            QMessageBox::information(self, QObject::tr("Modo potato"),
                                     QObject::tr("Instalé mods de rendimiento:\n\n%1\n\nSe quitan solos al volver al modo normal.").arg(job->downloaded.join('\n')));
    };
    job->next();
}

void FourFpsShell::removePotatoMods(MinecraftInstance* inst)
{
    if (!inst)
        return;
    auto* s = inst->settings();
    if (!s->contains("FourFpsPotatoFiles"))
        s->registerSetting("FourFpsPotatoFiles", QStringList());
    for (const auto& f : s->get("FourFpsPotatoFiles").toStringList()) {
        QFile::remove(inst->modsRoot() + "/" + f);
        QFile::remove(inst->modsRoot() + "/" + f + ".disabled");
    }
    s->set("FourFpsPotatoFiles", QStringList());
}
