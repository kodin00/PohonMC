#pragma once

#include <QJsonObject>
#include "InstanceTask.h"

class MinecraftInstance;

namespace SharePack {
QString encode(const QJsonObject& manifest, QString* error = nullptr);
bool decode(const QString& code, QJsonObject* manifest, QString* error = nullptr);
bool validate(const QJsonObject& manifest, QString* error = nullptr);
bool requiresManualDownload(const QJsonObject& file);
// Strips control characters, trims and truncates to `max`; empty when nothing usable remains.
QString sanitizeText(const QString& text, int max = 256);
// Returns a copy of an export manifest that always passes validate() (apart from the file-count cap): text is
// sanitised, unusable entries are downgraded to manual downloads, and entries with unusable or duplicate paths are
// dropped (their paths are appended to `skippedPaths`).
QJsonObject sanitizeForExport(const QJsonObject& manifest, QStringList* skippedPaths = nullptr);
// Keeps only server-required files, drops the ".disabled" suffix and keeps one entry per path, preferring the
// entry that was enabled in the source.
QJsonObject requiredOnly(const QJsonObject& manifest);
}  // namespace SharePack

class SharePackExportTask : public Task {
    Q_OBJECT
   public:
    explicit SharePackExportTask(MinecraftInstance* instance);
    QString code() const { return m_code; }
    QJsonObject manifest() const { return m_manifest; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    MinecraftInstance* m_instance;
    Task::Ptr m_task;
    QString m_code;
    QJsonObject m_manifest;
};

class SharePackImportTask : public InstanceTask {
    Q_OBJECT
   public:
    explicit SharePackImportTask(QJsonObject manifest, QWidget* parent = nullptr);
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    QJsonObject m_manifest;
    QWidget* m_parent;
    Task::Ptr m_task;
};
