// SPDX-License-Identifier: GPL-3.0-only

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <DataMigrationTask.h>
#include <FileSystem.h>

using DataMigration::LegacyDataAction;

namespace {
const QString LEGACY_CONFIG = QStringLiteral("elyprismlauncher.cfg");
const QString OWN_CONFIG = QStringLiteral("pohonmc.cfg");
const QString DECLINE_MARKER = QStringLiteral("PohonMCLauncher_nomigrate.txt");
const QStringList CONFIG_FILES = { LEGACY_CONFIG, OWN_CONFIG };

bool writeFile(const QString& path, const QByteArray& contents = "x")
{
    if (!FS::ensureFilePathExists(path))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(contents) == contents.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Relative path -> contents for every file below root.
QMap<QString, QByteArray> snapshot(const QString& root)
{
    QMap<QString, QByteArray> result;
    QDir rootDir(root);
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto path = it.next();
        result.insert(rootDir.relativeFilePath(path), readFile(path));
    }
    return result;
}

bool runTask(Task& task)
{
    QSignalSpy finished(&task, &Task::finished);
    task.start();
    if (!task.isFinished() && !finished.wait(30000))
        return false;
    return task.wasSuccessful();
}
}  // namespace

class DataMigrationTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_root;
    QString m_default;
    QString m_legacy;

    LegacyDataAction decide() const { return DataMigration::decideLegacyDataAction(m_default, m_legacy, CONFIG_FILES, DECLINE_MARKER); }

   private slots:
    void init()
    {
        QVERIFY(m_root.isValid());
        m_default = FS::PathCombine(m_root.path(), "parent", "PohonMCLauncher");
        m_legacy = FS::PathCombine(m_root.path(), "parent", "ElyPrismLauncher");
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(m_root.path(), "parent")));
    }

    void cleanup() { QVERIFY(FS::deletePath(FS::PathCombine(m_root.path(), "parent"))); }

    void test_noDirectories_usesDefault() { QCOMPARE(decide(), LegacyDataAction::UseDefault); }

    void test_legacyWithoutConfig_usesDefault()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, "instances", "a", "instance.cfg")));
        QVERIFY(QDir().mkpath(FS::PathCombine(m_legacy, LEGACY_CONFIG + ".d")));
        QCOMPARE(decide(), LegacyDataAction::UseDefault);
    }

    void test_legacyConfig_offersCopy()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QCOMPARE(decide(), LegacyDataAction::OfferCopy);
    }

    void test_onlyOwnConfigInLegacy_offersCopy()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, OWN_CONFIG)));
        QCOMPARE(decide(), LegacyDataAction::OfferCopy);
    }

    void test_declineMarker_usesLegacyInPlace()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, OWN_CONFIG)));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, DECLINE_MARKER), ""));
        QCOMPARE(decide(), LegacyDataAction::UseLegacyInPlace);
    }

    void test_defaultInUse_winsOverLegacy()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QVERIFY(writeFile(FS::PathCombine(m_default, "logs", "PohonMCLauncher-0.log")));
        QCOMPARE(decide(), LegacyDataAction::UseDefault);

        // also after a decline
        QVERIFY(writeFile(FS::PathCombine(m_legacy, DECLINE_MARKER), ""));
        QCOMPARE(decide(), LegacyDataAction::UseDefault);
    }

    void test_emptyDefault_isTreatedAsMissing()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QVERIFY(QDir().mkpath(m_default));
        QCOMPARE(decide(), LegacyDataAction::OfferCopy);
    }

    void test_hiddenFileMakesDefaultInUse()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QVERIFY(writeFile(FS::PathCombine(m_default, ".hidden")));
        QCOMPARE(decide(), LegacyDataAction::UseDefault);
    }

    void test_samePath_usesDefault()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QCOMPARE(DataMigration::decideLegacyDataAction(m_legacy, m_legacy + "/../ElyPrismLauncher", CONFIG_FILES, DECLINE_MARKER),
                 LegacyDataAction::UseDefault);
    }

    void test_partialStagingCopy_isNotMistakenForDefault()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG)));
        QVERIFY(writeFile(FS::PathCombine(DataMigration::stagingPathFor(m_default), OWN_CONFIG)));
        QCOMPARE(decide(), LegacyDataAction::OfferCopy);
    }

    void test_stagingPath_isSibling()
    {
        auto staging = DataMigration::stagingPathFor(m_default + "/");
        QCOMPARE(QFileInfo(staging).absolutePath(), QFileInfo(m_default).absolutePath());
        QVERIFY(staging != QDir::cleanPath(m_default));
    }

    void test_commit_renamesStaging()
    {
        auto staging = DataMigration::stagingPathFor(m_default);
        QVERIFY(writeFile(FS::PathCombine(staging, "instances", "a", "instance.cfg"), "cfg"));
        QString error;
        QVERIFY2(DataMigration::commitStagedCopy(staging, m_default, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(staging));
        QCOMPARE(readFile(FS::PathCombine(m_default, "instances", "a", "instance.cfg")), QByteArray("cfg"));
    }

    void test_commit_replacesEmptyTarget()
    {
        auto staging = DataMigration::stagingPathFor(m_default);
        QVERIFY(writeFile(FS::PathCombine(staging, OWN_CONFIG)));
        QVERIFY(QDir().mkpath(m_default));
        QString error;
        QVERIFY2(DataMigration::commitStagedCopy(staging, m_default, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_default, OWN_CONFIG)));
    }

    void test_commit_refusesNonEmptyTarget()
    {
        auto staging = DataMigration::stagingPathFor(m_default);
        QVERIFY(writeFile(FS::PathCombine(staging, OWN_CONFIG), "new"));
        QVERIFY(writeFile(FS::PathCombine(m_default, OWN_CONFIG), "existing"));
        QString error;
        QVERIFY(!DataMigration::commitStagedCopy(staging, m_default, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(FS::PathCombine(m_default, OWN_CONFIG)), QByteArray("existing"));
        QCOMPARE(readFile(FS::PathCombine(staging, OWN_CONFIG)), QByteArray("new"));
    }

    void test_commit_failsWithoutStaging()
    {
        QString error;
        QVERIFY(!DataMigration::commitStagedCopy(DataMigration::stagingPathFor(m_default), m_default, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(m_default));
    }

    void test_sameLayoutFilter()
    {
        auto filter = DataMigration::sameLayoutDataFilter(CONFIG_FILES);
        for (auto path : { "pohonmc.cfg", "elyprismlauncher.cfg", "accounts.json", "instances/a/instance.cfg", "icons/x.png",
                           "libraries/a.jar", "assets/indexes/1.json", "skins/s.png", "java/jre/bin/java", "catpacks/c.png",
                           "iconthemes/t/x.svg", "themes/t/theme.json", "logs/PohonMCLauncher-0.log", "Prism Launcher_nomigrate.txt" })
            QVERIFY2(filter(path), path);
        for (auto path : { "cache/x", "meta/index.json", "metacache", "translations/x.qm", "prismlauncher.cfg", "instances_nomigrate/x",
                           "sub/PolyMC_nomigrate.txt", "PohonMCLauncher.migration.lock" })
            QVERIFY2(!filter(path), path);

        auto upstream = DataMigration::launcherDataFilter(CONFIG_FILES);
        QVERIFY(upstream("instances/a/instance.cfg"));
        QVERIFY(!upstream("skins/s.png"));
        QVERIFY(!upstream("PolyMC_nomigrate.txt"));
    }

    // The whole accepted flow without the dialogs: copy into staging, commit, and the source stays untouched.
    void test_copyAndCommit_leavesSourceUntouched()
    {
        QVERIFY(writeFile(FS::PathCombine(m_legacy, LEGACY_CONFIG), "legacy settings"));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, OWN_CONFIG), "own settings"));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, "accounts.json"), "{}"));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, "instances", "My Pack", "instance.cfg"), "name=My Pack"));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, "Prism Launcher_nomigrate.txt"), ""));
        QVERIFY(writeFile(FS::PathCombine(m_legacy, "cache", "big.bin"), "cache"));
        const auto before = snapshot(m_legacy);
        QCOMPARE(decide(), LegacyDataAction::OfferCopy);

        const QString staging = DataMigration::stagingPathFor(m_default);
        QVERIFY(FS::ensureFolderPathExists(staging));
        DataMigrationTask task(m_legacy, staging, DataMigration::sameLayoutDataFilter(CONFIG_FILES));
        QVERIFY2(runTask(task), qPrintable(task.failReason()));
        // until the commit, the next launch still offers the copy
        QCOMPARE(decide(), LegacyDataAction::OfferCopy);

        QString error;
        QVERIFY2(DataMigration::commitStagedCopy(staging, m_default, &error), qPrintable(error));
        QCOMPARE(decide(), LegacyDataAction::UseDefault);

        QCOMPARE(snapshot(m_legacy), before);
        QCOMPARE(readFile(FS::PathCombine(m_default, OWN_CONFIG)), QByteArray("own settings"));
        QCOMPARE(readFile(FS::PathCombine(m_default, LEGACY_CONFIG)), QByteArray("legacy settings"));
        QCOMPARE(readFile(FS::PathCombine(m_default, "instances", "My Pack", "instance.cfg")), QByteArray("name=My Pack"));
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_default, "accounts.json")));
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_default, "Prism Launcher_nomigrate.txt")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(m_default, "cache")));
    }

    // FS::copy only reports the last file's result; the task must still fail when an earlier file could not be copied.
    void test_task_failsWhenAnyFileFails()
    {
#ifdef Q_OS_WIN
        QSKIP("Removing read permission is not reliable on Windows");
#endif
        const QStringList names = { "a.cfg", "b.cfg", "c.cfg", "d.cfg" };
        for (const auto& name : names)
            QVERIFY(writeFile(FS::PathCombine(m_legacy, "instances", name)));
        const QString unreadable = FS::PathCombine(m_legacy, "instances", "b.cfg");
        QVERIFY(QFile::setPermissions(unreadable, QFileDevice::WriteOwner));
        {
            QFile probe(unreadable);
            if (probe.open(QIODevice::ReadOnly)) {
                QFile::setPermissions(unreadable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                QSKIP("File permissions are not enforced for this user");
            }
        }

        const QString staging = DataMigration::stagingPathFor(m_default);
        QVERIFY(FS::ensureFolderPathExists(staging));
        DataMigrationTask task(m_legacy, staging, DataMigration::sameLayoutDataFilter(CONFIG_FILES));
        const bool succeeded = runTask(task);
        QFile::setPermissions(unreadable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(task.isFinished());
        QVERIFY(!succeeded);
        QVERIFY(!task.failReason().isEmpty());
    }
};

QTEST_GUILESS_MAIN(DataMigrationTest)

#include "DataMigration_test.moc"
