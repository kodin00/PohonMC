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

// Everything validate() requires of a single mod path, except uniqueness.
bool validModPath(const QJsonValue& value)
{
    static const QRegularExpression filenamePattern(
        QStringLiteral("^[^<>:\"/\\\\|?*\\x00-\\x1f]+\\.(jar|zip|litemod|nilmod)(\\.disabled)?$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\.|$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    const auto path = value.toString();
    const auto filename = path.mid(5);
    return safeText(value, 255) && path.startsWith("mods/") && filenamePattern.match(filename).hasMatch() && !filename.startsWith('.') &&
           !filename.contains("..") && !reserved.match(filename).hasMatch() && FS::RemoveInvalidPathChars(path) == path;
}

bool hasSupportedDownloadUrl(const QJsonObject& file)
{
    static const QSet<QString> hosts{ "cdn.modrinth.com", "edge.forgecdn.net", "mediafilez.forgecdn.net", "media.forgecdn.net" };
    const auto downloads = file.value("downloads").toArray();
    if (downloads.size() != 1 || !downloads.first().isString())
        return false;

    const QUrl url(downloads.first().toString(), QUrl::StrictMode);
    return url.isValid() && url.scheme() == "https" && hosts.contains(url.host().toLower()) && url.userInfo().isEmpty() &&
           url.port() == -1 && !url.hasFragment() && !url.path().isEmpty();
}
}  // namespace

bool SharePack::requiresManualDownload(const QJsonObject& file)
{
    return file.value("pohonManualDownload") == true;
}

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
    for (const auto& v : files) {
        if (!v.isObject())
            return fail(error, QObject::tr("Invalid mod entry."));
        const auto f = v.toObject();
        const auto path = f.value("path").toString();
        const auto filename = path.mid(5);
        const bool manual = requiresManualDownload(f);
        if (f.contains("pohonManualDownload") && !manual)
            return fail(error, QObject::tr("Invalid manual download marker for %1.").arg(path));
        if (!validModPath(f.value("path")) || paths.contains(path.toCaseFolded()))
            return fail(error, QObject::tr("Invalid or duplicate mod path: %1").arg(path));
        paths.insert(path.toCaseFolded());

        if (manual) {
            if (f.contains("downloads") || f.contains("hashes"))
                return fail(error, QObject::tr("Unexpected download information for manual mod %1.").arg(path));
        } else {
            if (!ModrinthPackExportTask::isSupportedArchive(filename, Qt::CaseInsensitive))
                return fail(error, QObject::tr("Unsupported mod archive for automatic download: %1").arg(path));
            if (!sha512.match(f.value("hashes").toObject().value("sha512").toString()).hasMatch())
                return fail(error, QObject::tr("Missing or invalid SHA-512 for %1.").arg(path));
            if (!hasSupportedDownloadUrl(f))
                return fail(error, QObject::tr("Untrusted download URL for %1.").arg(path));
        }
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

QString SharePack::sanitizeText(const QString& text, int max)
{
    static const QRegularExpression controls(QStringLiteral("[\\x00-\\x1f\\x7f]"));
    QString out = text;
    out.remove(controls);
    out = out.trimmed();
    if (out.size() > max) {
        out.truncate(max);
        if (out.back().isHighSurrogate())
            out.chop(1);
        out = out.trimmed();
    }
    return out;
}

QJsonObject SharePack::sanitizeForExport(const QJsonObject& manifest, QStringList* skippedPaths)
{
    static const QRegularExpression sha512(QStringLiteral("^[a-fA-F0-9]{128}$"));
    auto result = manifest;
    auto name = sanitizeText(manifest.value("name").toString());
    result["name"] = name.isEmpty() ? QStringLiteral("Modpack") : name;

    QJsonArray files;
    QSet<QString> seen;
    for (const auto& value : manifest.value("files").toArray()) {
        auto file = value.toObject();
        const auto path = file.value("path").toString();
        // A path is the only identity a mod has in the manifest, so an unusable one cannot be shared, not even manually.
        if (!validModPath(file.value("path")) || seen.contains(path.toCaseFolded())) {
            if (skippedPaths)
                skippedPaths->append(sanitizeText(path));
            continue;
        }
        seen.insert(path.toCaseFolded());

        bool manual = requiresManualDownload(file);
        if (!manual &&
            !(ModrinthPackExportTask::isSupportedArchive(path.mid(5), Qt::CaseInsensitive) &&
              sha512.match(file.value("hashes").toObject().value("sha512").toString()).hasMatch() && hasSupportedDownloadUrl(file)))
            manual = true;
        if (manual) {
            file.remove("downloads");
            file.remove("hashes");
            file["pohonManualDownload"] = true;
        } else {
            file.remove("pohonManualDownload");
        }
        const auto env = file.value("env").toObject();
        if (file.contains("env") && (!env.isEmpty() && (env.value("client") != "required" ||
                                                        (env.value("server") != "required" && env.value("server") != "unsupported"))))
            file.remove("env");
        const auto server = file.value("pohonServerRequirement");
        if (!server.isUndefined() && server != "required" && server != "optional" && server != "unknown")
            file.remove("pohonServerRequirement");
        for (auto key : { "name", "version" }) {
            if (!file.contains(key))
                continue;
            const auto text = sanitizeText(file.value(key).toString());
            if (text.isEmpty())
                file.remove(key);
            else
                file[key] = text;
        }
        files.append(file);
    }
    result["files"] = files;
    return result;
}

QJsonObject SharePack::requiredOnly(const QJsonObject& manifest)
{
    QJsonArray files;
    QHash<QString, int> indexByPath;  // case-folded normalised path -> index in `files`
    QList<bool> wasEnabled;
    for (const auto& value : manifest.value("files").toArray()) {
        auto file = value.toObject();
        if (file.value("pohonServerRequirement").toString() != "required")
            continue;
        auto path = file.value("path").toString();
        const bool disabled = path.endsWith(".disabled", Qt::CaseInsensitive);
        if (disabled) {
            path.chop(9);
            file.insert("path", path);
        }
        const auto key = path.toCaseFolded();
        const auto existing = indexByPath.constFind(key);
        if (existing == indexByPath.cend()) {
            indexByPath.insert(key, files.size());
            wasEnabled.append(!disabled);
            files.append(file);
        } else if (!disabled && !wasEnabled[*existing]) {
            files.replace(*existing, file);
            wasEnabled[*existing] = true;
        }
    }
    auto result = manifest;
    result.insert("files", files);
    return result;
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
    // Chat apps and mail clients wrap or pad codes. No whitespace or invisible format character
    // (zero-width space, BOM, ...) belongs to the alphabet, so dropping them is unambiguous.
    // The raw cap only bounds the work done here; the real cap applies after stripping.
    QString trimmed;
    if (code.size() <= MaxCode * 2) {
        trimmed.reserve(code.size());
        for (const QChar c : code)
            if (!c.isSpace() && c.category() != QChar::Other_Format)
                trimmed.append(c);
    }
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
    QStringList paths;
    QFileInfoList candidates;
    for (const auto& info : mods.entryInfoList(QDir::Files | QDir::System)) {
        const auto name = info.fileName();
        const bool nilMod = name.endsWith(".nilmod", Qt::CaseInsensitive) || name.endsWith(".nilmod.disabled", Qt::CaseInsensitive);
        if (!nilMod && !ModrinthPackExportTask::isSupportedArchive(name, Qt::CaseInsensitive))
            continue;
        paths << ("mods/" + name);
        if (!nilMod && info.isFile() && !info.isSymLink())
            candidates << info;
    }
    QMap<QString, QPair<QString, QString>> pinnedDownloads;
    QFile previous(QDir(m_instance->instanceRoot()).filePath("mrpack/modrinth.index.json"));
    if (previous.open(QIODevice::ReadOnly) && previous.size() <= MaxJson) {
        const auto manifest = QJsonDocument::fromJson(previous.readAll()).object();
        if (SharePack::validate(manifest)) {
            for (const auto& value : manifest.value("files").toArray()) {
                const auto file = value.toObject();
                if (SharePack::requiresManualDownload(file))
                    continue;
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
    connect(task.get(), &Task::succeeded, this, [this, exporter = task.get(), paths = std::move(paths)] {
        m_manifest = QJsonDocument::fromJson(exporter->manifestIndex()).object();
        QJsonArray files;
        QSet<QString> downloadablePaths;
        const auto resolvedFiles = m_manifest.value("files").toArray();
        for (const auto& value : resolvedFiles) {
            const auto file = value.toObject();
            if (hasSupportedDownloadUrl(file)) {
                files.append(file);
                downloadablePaths.insert(file.value("path").toString());
            }
        }
        for (const auto& path : paths) {
            if (!downloadablePaths.contains(path))
                files.append(QJsonObject{ { "path", path }, { "pohonManualDownload", true } });
        }
        const QDir mods(m_instance->modsRoot());
        const auto allMods = m_instance->loaderModList()->allMods();
        for (int i = 0; i < files.size(); ++i) {
            auto file = files[i].toObject();
            const auto filename = QFileInfo(file.value("path").toString()).fileName();
            file["pohonServerRequirement"] = ModFolderModel::serverRequirement(mods, filename);
            for (const auto* mod : allMods) {
                if (mod->fileinfo().fileName() == filename) {
                    // Raw metadata may hold control characters or be overlong; sanitizeForExport() cleans both.
                    file["name"] = mod->name();
                    file["version"] = mod->version();
                    break;
                }
            }
            files[i] = file;
        }
        m_manifest["files"] = files;
        QStringList skipped;
        m_manifest = SharePack::sanitizeForExport(m_manifest, &skipped);
        for (const auto& path : skipped)
            logWarning(tr("%1 cannot be shared because its file name is not supported, so it was left out of the share code.").arg(path));
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
    QJsonArray downloads;
    QMap<QString, QString> requirements;
    for (const auto& value : m_manifest.value("files").toArray()) {
        const auto file = value.toObject();
        const auto fileName = QFileInfo(file.value("path").toString()).fileName();
        const auto requirement = file.value("pohonServerRequirement").toString("unknown");
        if (requirement != "unknown")
            requirements.insert(fileName, requirement);
        if (SharePack::requiresManualDownload(file)) {
            logWarning(
                tr("%1 is missing. Ask your friend to share the file manually, then add it to this instance’s Mods folder.").arg(fileName));
        } else {
            downloads.append(file);
        }
    }
    auto downloadManifest = m_manifest;
    downloadManifest.insert("files", downloads);
    QFile index(QDir(m_stagingPath).filePath("modrinth.index.json"));
    const auto indexData = QJsonDocument(downloadManifest).toJson(QJsonDocument::Compact);
    if (!index.open(QIODevice::WriteOnly | QIODevice::Truncate) || index.write(indexData) != indexData.size()) {
        emitFailed(tr("Could not write share pack index."));
        return;
    }
    index.close();

    const QDir mods(QDir(m_stagingPath).filePath("minecraft/mods"));
    if (!requirements.isEmpty() && !ModFolderModel::setServerRequirements(mods, requirements)) {
        emitFailed(tr("Could not save server requirements."));
        return;
    }
    auto task = makeShared<ModrinthCreationTask>(m_stagingPath, m_globalSettings, m_parent, QString());
    task->setCreateOnly();
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
