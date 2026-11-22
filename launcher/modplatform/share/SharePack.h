#pragma once

#include <QJsonObject>
#include "InstanceTask.h"

class MinecraftInstance;

namespace SharePack {
QString encode(const QJsonObject& manifest, QString* error = nullptr);
bool decode(const QString& code, QJsonObject* manifest, QString* error = nullptr);
bool validate(const QJsonObject& manifest, QString* error = nullptr);
bool requiresManualDownload(const QJsonObject& file);
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
