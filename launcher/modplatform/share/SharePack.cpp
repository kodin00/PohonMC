#include "SharePack.h"

#include <zlib.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QtEndian>

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/modrinth/ModrinthInstanceCreationTask.h"
#include "modplatform/modrinth/ModrinthPackExportTask.h"

namespace {
constexpr int MaxJson = 4 * 1024 * 1024;
constexpr int MaxCode = 6 * 1024 * 1024;
const QString Prefix = QStringLiteral("POHON1:");
bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}
bool safeText(const QJsonValue& v, int max = 256)
{
    return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= max &&
           !v.toString().contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
}
}  // namespace

bool SharePack::validate(const QJsonObject& obj, QString* error)
{
    if (obj.value("formatVersion") != 1 || obj.value("game") != "minecraft" || !safeText(obj.value("name")))
        return fail(error, QObject::tr("Invalid share pack header."));
    if (!obj.value("dependencies").isObject())
        return fail(error, QObject::tr("Missing game dependencies."));
    auto deps = obj.value("dependencies").toObject();
    const QSet<QString> allowedDeps{ "minecraft", "fabric-loader", "quilt-loader", "forge", "neoforge" };
    const QRegularExpression versionPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$"));
    if (!safeText(deps.value("minecraft")) || deps.size() > 2)
        return fail(error, QObject::tr("Invalid Minecraft or loader version."));
    for (auto it = deps.begin(); it != deps.end(); ++it)
        if (!allowedDeps.contains(it.key()) || !safeText(it.value()) || !versionPattern.match(it.value().toString()).hasMatch() ||
            it.value().toString().contains(".."))
            return fail(error, QObject::tr("Invalid game dependency."));
    if (!obj.value("files").isArray())
        return fail(error, QObject::tr("Missing mod list."));
    auto files = obj.value("files").toArray();
    if (files.size() > 5000)
        return fail(error, QObject::tr("Too many mods in share code."));
    QSet<QString> paths;
    const QRegularExpression sha512(QStringLiteral("^[a-fA-F0-9]{128}$"));
    const QRegularExpression filenamePattern(QStringLiteral("^[^<>:\"/\\\\|?*\\x00-\\x1f]+\\.(jar|zip|litemod)(\\.disabled)?$"),
                                             QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\.|$)"),
                                      QRegularExpression::CaseInsensitiveOption);
    const QSet<QString> hosts{ "cdn.modrinth.com", "edge.forgecdn.net", "mediafilez.forgecdn.net", "media.forgecdn.net" };
    for (const auto& v : files) {
        if (!v.isObject())
            return fail(error, QObject::tr("Invalid mod entry."));
        const auto f = v.toObject();
        QString path = f.value("path").toString();
        const auto filename = path.mid(5);
        if (!safeText(f.value("path"), 255) || !path.startsWith("mods/") || !filenamePattern.match(filename).hasMatch() ||
            filename.startsWith('.') || filename.contains("..") || reserved.match(filename).hasMatch() ||
            FS::RemoveInvalidPathChars(path) != path || paths.contains(path.toCaseFolded()))
            return fail(error, QObject::tr("Invalid or duplicate mod path: %1").arg(path));
        paths.insert(path.toCaseFolded());
        if (!sha512.match(f.value("hashes").toObject().value("sha512").toString()).hasMatch())
            return fail(error, QObject::tr("Missing or invalid SHA-512 for %1.").arg(path));
        auto downloads = f.value("downloads").toArray();
        if (downloads.size() != 1 || !downloads.first().isString())
            return fail(error, QObject::tr("Invalid download for %1.").arg(path));
        QUrl url(downloads.first().toString(), QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != "https" || !hosts.contains(url.host().toLower()) || !url.userInfo().isEmpty() ||
            url.port() != -1 || url.hasFragment() || url.path().isEmpty())
            return fail(error, QObject::tr("Untrusted download URL for %1.").arg(path));
        auto env = f.value("env").toObject();
        if (!env.isEmpty() &&
            (env.value("client") != "required" || (env.value("server") != "required" && env.value("server") != "unsupported")))
            return fail(error, QObject::tr("Invalid mod environment for %1.").arg(path));
        auto server = f.value("pohonServerRequirement");
        if (!server.isUndefined() && server != "required" && server != "optional" && server != "unknown")
            return fail(error, QObject::tr("Invalid server requirement for %1.").arg(path));
        for (auto key : { "name", "version" })
            if (f.contains(key) && !safeText(f.value(key)))
                return fail(error, QObject::tr("Invalid mod information for %1.").arg(path));
    }
    return true;
}

QString SharePack::encode(const QJsonObject& manifest, QString* error)
{
    if (!validate(manifest, error))
        return {};
    auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (json.size() > MaxJson) {
        fail(error, QObject::tr("Share pack is too large."));
        return {};
    }
    return Prefix + QString::fromLatin1(qCompress(json, 9).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool SharePack::decode(const QString& code, QJsonObject* manifest, QString* error)
{
    const QString trimmed = code.trimmed();
    if (!manifest || !trimmed.startsWith(Prefix) || trimmed.size() > MaxCode)
        return fail(error, QObject::tr("Invalid or unsupported share code."));
    auto encoded = trimmed.mid(Prefix.size()).toLatin1();
    static const QRegularExpression chars(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!chars.match(QString::fromLatin1(encoded)).hasMatch())
        return fail(error, QObject::tr("Invalid share code encoding."));
    auto compressed = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
    if (compressed.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != encoded)
        return fail(error, QObject::tr("Invalid share code encoding."));
    if (compressed.size() < 4 || qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(compressed.constData())) > MaxJson)
        return fail(error, QObject::tr("Invalid or oversized share code."));
    auto expected = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(compressed.constData()));
    QByteArray json(static_cast<int>(expected), Qt::Uninitialized);
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(compressed.data() + 4);
    stream.avail_in = compressed.size() - 4;
    stream.next_out = reinterpret_cast<Bytef*>(json.data());
    stream.avail_out = expected;
    if (inflateInit(&stream) != Z_OK)
        return fail(error, QObject::tr("Could not decompress share code."));
    int result = inflate(&stream, Z_FINISH);
    bool validStream = result == Z_STREAM_END && stream.total_out == expected && stream.avail_in == 0;
    inflateEnd(&stream);
    if (!validStream)
        return fail(error, QObject::tr("Invalid compressed share code."));
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() || !validate(doc.object(), error))
        return fail(error, error && !error->isEmpty() ? *error : QObject::tr("Invalid share code contents."));
    *manifest = doc.object();
    return true;
}

SharePackExportTask::SharePackExportTask(MinecraftInstance* instance) : m_instance(instance) {}
bool SharePackExportTask::abort()
{
    return m_task && m_task->abort();
}
void SharePackExportTask::executeTask()
{
    if (!m_instance) {
        emitFailed(tr("No instance to share."));
        return;
    }
    const QDir mods(m_instance->modsRoot());
    QStringList unsupported;
    QFileInfoList candidates;
    for (const auto& info : mods.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        const auto relative = info.fileName();
        if (relative == ".index" || relative == ".pohon-server-requirements.json")
            continue;
        if (info.isSymLink() || !info.isFile() || relative.startsWith('.'))
            unsupported << relative;
        else
            candidates << info;
    }
    if (!unsupported.isEmpty()) {
        emitFailed(tr("Cannot share these files in the mods folder: %1").arg(unsupported.join(", ")));
        return;
    }
    QMap<QString, QPair<QString, QString>> pinnedDownloads;
    QFile previous(QDir(m_instance->instanceRoot()).filePath("mrpack/modrinth.index.json"));
    if (previous.open(QIODevice::ReadOnly) && previous.size() <= MaxJson) {
        const auto manifest = QJsonDocument::fromJson(previous.readAll()).object();
        if (SharePack::validate(manifest)) {
            for (const auto& value : manifest.value("files").toArray()) {
                const auto file = value.toObject();
                pinnedDownloads.insert(file.value("path").toString(), { file.value("hashes").toObject().value("sha512").toString(),
                                                                        file.value("downloads").toArray().first().toString() });
            }
            // Toggling a mod only changes its suffix; the hash is still checked before reuse.
            const auto original = pinnedDownloads;
            for (auto it = original.cbegin(); it != original.cend(); ++it) {
                const auto alternate = it.key().endsWith(".disabled") ? it.key().chopped(9) : it.key() + ".disabled";
                if (!pinnedDownloads.contains(alternate))
                    pinnedDownloads.insert(alternate, it.value());
            }
        }
    }
    auto task = makeShared<ModrinthPackExportTask>(m_instance->name(), "1", "", false, m_instance, QString(), MMCZip::FilterFileFunction{},
                                                   QStringList{ "edge.forgecdn.net", "mediafilez.forgecdn.net", "media.forgecdn.net" },
                                                   pinnedDownloads, candidates);
    m_task = task;
    propagateFromOther(task.get());
    connect(task.get(), &Task::abortStatusChanged, this, &Task::setAbortable);
    connect(task.get(), &Task::failed, this, &SharePackExportTask::emitFailed);
    connect(task.get(), &Task::aborted, this, &SharePackExportTask::emitAborted);
    connect(task.get(), &Task::succeeded, this, [this, exporter = task.get()] {
        auto doc = QJsonDocument::fromJson(exporter->manifestIndex());
        m_manifest = doc.object();
        auto files = m_manifest.value("files").toArray();
        const QDir mods(m_instance->modsRoot());
        const auto allMods = m_instance->loaderModList()->allMods();
        for (int i = 0; i < files.size(); ++i) {
            auto file = files[i].toObject();
            const auto filename = QFileInfo(file.value("path").toString()).fileName();
            file["pohonServerRequirement"] = ModFolderModel::serverRequirement(mods, filename);
            for (const auto* mod : allMods) {
                if (mod->fileinfo().fileName() == filename) {
                    if (!mod->name().trimmed().isEmpty() && mod->name().size() <= 256)
                        file["name"] = mod->name();
                    if (!mod->version().trimmed().isEmpty() && mod->version().size() <= 256)
                        file["version"] = mod->version();
                    break;
                }
            }
            files[i] = file;
        }
        m_manifest["files"] = files;
        QString error;
        m_code = SharePack::encode(m_manifest, &error);
        if (m_code.isEmpty()) {
            emitFailed(error);
            return;
        }
        emitSucceeded();
    });
    task->start();
}

SharePackImportTask::SharePackImportTask(QJsonObject manifest, QWidget* parent) : m_manifest(std::move(manifest)), m_parent(parent) {}
bool SharePackImportTask::abort()
{
    return m_task && m_task->abort();
}
void SharePackImportTask::executeTask()
{
    QString error;
    if (!SharePack::validate(m_manifest, &error)) {
        emitFailed(error);
        return;
    }
    QFile index(QDir(m_stagingPath).filePath("modrinth.index.json"));
    const auto indexData = QJsonDocument(m_manifest).toJson(QJsonDocument::Compact);
    if (!index.open(QIODevice::WriteOnly | QIODevice::Truncate) || index.write(indexData) != indexData.size()) {
        emitFailed(tr("Could not write share pack index."));
        return;
    }
    index.close();
    const QDir mods(QDir(m_stagingPath).filePath("minecraft/mods"));
    QMap<QString, QString> requirements;
    for (const auto& value : m_manifest.value("files").toArray()) {
        const auto file = value.toObject();
        const auto requirement = file.value("pohonServerRequirement").toString("unknown");
        if (requirement != "unknown")
            requirements.insert(QFileInfo(file.value("path").toString()).fileName(), requirement);
    }
    if (!requirements.isEmpty() && !ModFolderModel::setServerRequirements(mods, requirements)) {
        emitFailed(tr("Could not save server requirements."));
        return;
    }
    auto task = makeShared<ModrinthCreationTask>(m_stagingPath, m_globalSettings, m_parent, QString());
    task->setName(*this);
    task->setIcon(m_instIcon);
    task->setGroup(m_instGroup);
    m_task = task;
    propagateFromOther(task.get());
    connect(task.get(), &Task::abortStatusChanged, this, &Task::setAbortable);
    connect(task.get(), &Task::succeeded, this, &SharePackImportTask::emitSucceeded);
    connect(task.get(), &Task::failed, this, &SharePackImportTask::emitFailed);
    connect(task.get(), &Task::aborted, this, &SharePackImportTask::emitAborted);
    task->start();
}
