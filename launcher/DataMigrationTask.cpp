// SPDX-FileCopyrightText: 2022 Sefa Eyeoglu <contact@scrumplex.net>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "DataMigrationTask.h"

#include "FileSystem.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>

#include <QtConcurrent>

DataMigrationTask::DataMigrationTask(const QString& sourcePath, const QString& targetPath, Filter pathMatcher)
    : Task(), m_sourcePath(sourcePath), m_targetPath(targetPath), m_pathMatcher(pathMatcher), m_copy(sourcePath, targetPath)
{
    m_copy.matcher(m_pathMatcher).whitelist(true);
}

void DataMigrationTask::executeTask()
{
    setStatus(tr("Scanning files..."));

    // 1. Scan
    // Check how many files we gotta copy
    m_copyFuture = QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        return m_copy(true);  // dry run to collect amount of files
    });
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::dryRunFinished);
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::dryRunAborted);
    m_copyFutureWatcher.setFuture(m_copyFuture);
}

void DataMigrationTask::dryRunFinished()
{
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::dryRunFinished);
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::dryRunAborted);

    if (!m_copyFuture.isValid() || !m_copyFuture.result()) {
        emitFailed(tr("Failed to scan source path."));
        return;
    }

    // 2. Copy
    // Actually copy all files now.
    m_toCopy = m_copy.totalCopied();
    connect(&m_copy, &FS::copy::fileCopied, [&, this](const QString& relativeName) {
        QString shortenedName = relativeName;
        // shorten the filename to hopefully fit into one line
        if (shortenedName.length() > 50)
            shortenedName = relativeName.left(20) + "…" + relativeName.right(29);
        setProgress(m_copy.totalCopied(), m_toCopy);
        setStatus(tr("Copying %1…").arg(shortenedName));
    });
    m_copyFuture = QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        return m_copy(false);  // actually copy now
    });
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::copyFinished);
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::copyAborted);
    m_copyFutureWatcher.setFuture(m_copyFuture);
}

void DataMigrationTask::dryRunAborted()
{
    emitAborted();
}

void DataMigrationTask::copyFinished()
{
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::copyFinished);
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::copyAborted);

    // FS::copy's return value only reflects the last file it copied, so also check the failure list.
    if (!m_copyFuture.isValid() || !m_copyFuture.result() || m_copy.totalFailed() > 0) {
        emitFailed(tr("Some paths could not be copied!"));
        return;
    }

    emitSucceeded();
}

void DataMigrationTask::copyAborted()
{
    emitAborted();
}

namespace DataMigration {

bool isDataDirectoryInUse(const QString& path)
{
    QDir dir(path);
    return dir.exists() && !dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

LegacyDataAction decideLegacyDataAction(const QString& defaultDataPath,
                                        const QString& legacyDataPath,
                                        const QStringList& legacyConfigFiles,
                                        const QString& declineMarkerName)
{
    if (QDir::cleanPath(QDir(defaultDataPath).absolutePath()) == QDir::cleanPath(QDir(legacyDataPath).absolutePath()))
        return LegacyDataAction::UseDefault;

    // A default directory that holds anything is authoritative, exactly as before.
    if (isDataDirectoryInUse(defaultDataPath))
        return LegacyDataAction::UseDefault;

    bool legacyHasConfig = false;
    for (const auto& configFile : legacyConfigFiles)
        legacyHasConfig |= QFileInfo(FS::PathCombine(legacyDataPath, configFile)).isFile();
    if (!legacyHasConfig)
        return LegacyDataAction::UseDefault;

    if (QFileInfo::exists(FS::PathCombine(legacyDataPath, declineMarkerName)))
        return LegacyDataAction::UseLegacyInPlace;

    return LegacyDataAction::OfferCopy;
}

QString stagingPathFor(const QString& defaultDataPath)
{
    return QDir::cleanPath(QDir(defaultDataPath).absolutePath()) + ".migration-partial";
}

bool commitStagedCopy(const QString& stagingPath, const QString& targetPath, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!QFileInfo(stagingPath).isDir())
        return fail(QObject::tr("The copied data at %1 is missing.").arg(stagingPath));

    if (QFileInfo::exists(targetPath)) {
        if (isDataDirectoryInUse(targetPath) || !QFileInfo(targetPath).isDir())
            return fail(QObject::tr("%1 already exists and is not empty.").arg(targetPath));
        // rmdir only removes empty directories, so nothing can be lost here.
        if (!QDir().rmdir(targetPath))
            return fail(QObject::tr("The empty folder %1 could not be replaced.").arg(targetPath));
    }

    if (!QDir().rename(stagingPath, targetPath))
        return fail(QObject::tr("%1 could not be renamed to %2.").arg(stagingPath, targetPath));

    return true;
}

Filter launcherDataFilter(const QStringList& configFiles)
{
    using namespace Filters;

    QList<Filter> filters;
    for (const auto& configFile : configFiles)
        filters.append(equals(configFile));
    filters.append(startsWith("logs/"));
    filters.append(equals("accounts.json"));
    filters.append(startsWith("accounts/"));
    filters.append(startsWith("assets/"));
    filters.append(startsWith("icons/"));
    filters.append(startsWith("instances/"));
    filters.append(startsWith("libraries/"));
    filters.append(startsWith("mods/"));
    filters.append(startsWith("themes/"));
    return any(std::move(filters));
}

Filter sameLayoutDataFilter(const QStringList& configFiles)
{
    using namespace Filters;

    QList<Filter> filters;
    filters.append(launcherDataFilter(configFiles));
    filters.append(startsWith("skins/"));
    filters.append(startsWith("java/"));
    filters.append(startsWith("catpacks/"));
    filters.append(startsWith("iconthemes/"));
    filters.append(regexp(QRegularExpression("^[^/]+_nomigrate\\.txt$")));
    return any(std::move(filters));
}

}  // namespace DataMigration
