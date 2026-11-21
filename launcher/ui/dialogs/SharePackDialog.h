// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QDir>
#include <QHash>
#include <QJsonObject>

class MinecraftInstance;
class QCheckBox;
class QLabel;
class QTreeWidget;

class SharePackDialog : public QDialog {
    Q_OBJECT

   public:
    static void showForInstance(MinecraftInstance* instance, QWidget* parent);

   private:
    SharePackDialog(const QJsonObject& manifest, const QDir& modsDir, const QHash<QString, QString>& providerDeclarations, QWidget* parent);

    void updateFilter();
    void copyCode();

    QJsonObject m_manifest;
    QDir m_modsDir;
    QTreeWidget* m_mods;
    QCheckBox* m_onlyRequired;
    QLabel* m_status;
};
