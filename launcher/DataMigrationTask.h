// SPDX-FileCopyrightText: 2022 Sefa Eyeoglu <contact@scrumplex.net>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FileSystem.h"
#include "Filter.h"
#include "tasks/Task.h"

#include <QFuture>
#include <QFutureWatcher>

/*
 * Migrate existing data from other MMC-like launchers.
 */

class DataMigrationTask : public Task {
    Q_OBJECT
   public:
    explicit DataMigrationTask(const QString& sourcePath, const QString& targetPath, Filter pathmatcher);
    ~DataMigrationTask() override = default;

   protected:
    virtual void executeTask() override;

   protected slots:
    void dryRunFinished();
    void dryRunAborted();
    void copyFinished();
    void copyAborted();

   private:
    const QString& m_sourcePath;
    const QString& m_targetPath;
    const Filter m_pathMatcher;

    FS::copy m_copy;
    int m_toCopy = 0;
    QFuture<bool> m_copyFuture;
    QFutureWatcher<bool> m_copyFutureWatcher;
};

/*
 * Pure decision helpers for choosing between the default data directory and the data directory of a launcher that
 * shares our data layout (ElyPrismLauncher), which this launcher used to adopt in place.
 * They only look at the file system (and, for commitStagedCopy, rename one directory), so they can be unit tested.
 */
namespace DataMigration {

enum class LegacyDataAction {
    UseDefault,        //!< Use the default data directory (it is in use already, or there is nothing to migrate).
    UseLegacyInPlace,  //!< The user declined copying earlier: keep using the legacy directory in place.
    OfferCopy,         //!< Ask the user whether to copy the legacy directory into a new default directory.
};

/// True when path is a directory with at least one entry. An empty directory is treated like a missing one.
bool isDataDirectoryInUse(const QString& path);

/**
 * Decides how to treat a legacy data directory.
 *
 * @param defaultDataPath the data directory this launcher uses when nothing else is configured
 * @param legacyDataPath the data directory of the other launcher
 * @param legacyConfigFiles names of config files, any of which marks legacyDataPath as a real data directory
 * @param declineMarkerName name of the file inside legacyDataPath that records a declined copy
 */
LegacyDataAction decideLegacyDataAction(const QString& defaultDataPath,
                                        const QString& legacyDataPath,
                                        const QStringList& legacyConfigFiles,
                                        const QString& declineMarkerName);

/// The directory a copy is staged in before it becomes defaultDataPath. A sibling, so the final rename stays on one volume.
QString stagingPathFor(const QString& defaultDataPath);

/**
 * Turns a completed staged copy into the data directory by renaming stagingPath to targetPath.
 * An existing empty targetPath is removed first; a non-empty one is never touched and makes this fail.
 * On failure, errorMessage (if given) receives the reason and stagingPath is left as it was.
 */
bool commitStagedCopy(const QString& stagingPath, const QString& targetPath, QString* errorMessage = nullptr);

/// The paths every data migration copies: the given top-level config files plus accounts, instances and the like.
Filter launcherDataFilter(const QStringList& configFiles);

/// launcherDataFilter() plus what a launcher with our exact data layout also keeps: skins, managed Java runtimes,
/// cat packs, icon themes and the top-level "*_nomigrate.txt" answers to earlier migration prompts.
Filter sameLayoutDataFilter(const QStringList& configFiles);

}  // namespace DataMigration
