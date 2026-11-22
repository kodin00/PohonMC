// SPDX-License-Identifier: GPL-3.0-only

#include "SharePackDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ProgressDialog.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/share/SharePack.h"

void SharePackDialog::showForInstance(MinecraftInstance* instance, QWidget* parent)
{
    SharePackExportTask task(instance);
    ProgressDialog progress(parent);
    progress.setSkipButton(true, tr("Cancel"));
    progress.execWithTask(&task);
    if (!task.wasSuccessful()) {
        if (!task.failReason().isEmpty())
            QMessageBox::warning(parent, tr("Share Modpack"), task.failReason());
        return;
    }

    QHash<QString, QString> providerDeclarations;
    for (const auto* mod : instance->loaderModList()->allMods()) {
        if (const auto metadata = mod->metadata(); metadata && !metadata->serverRequirementDeclaration.isEmpty())
            providerDeclarations.insert(mod->fileinfo().fileName(), metadata->serverRequirementDeclaration);
    }
    SharePackDialog dialog(task.manifest(), QDir(instance->modsRoot()), providerDeclarations, parent);
    dialog.exec();
}

SharePackDialog::SharePackDialog(const QJsonObject& manifest,
                                 const QDir& modsDir,
                                 const QHash<QString, QString>& providerDeclarations,
                                 QWidget* parent)
    : QDialog(parent), m_manifest(manifest), m_modsDir(modsDir)
{
    setWindowTitle(tr("Share Modpack"));
    resize(650, 420);

    auto* layout = new QVBoxLayout(this);
    auto* guidance = new QLabel(tr("Mark the mods your server requires. A share code includes the mod list and game versions."), this);
    guidance->setWordWrap(true);
    layout->addWidget(guidance);

    m_onlyRequired = new QCheckBox(tr("Show only server-required mods"), this);
    layout->addWidget(m_onlyRequired);

    m_mods = new QTreeWidget(this);
    m_mods->setRootIsDecorated(false);
    m_mods->setHeaderLabels({ tr("Mod"), tr("Server requirement") });
    m_mods->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mods->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_mods, 1);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copy = buttons->addButton(tr("Copy Share Code"), QDialogButtonBox::ActionRole);
    copy->setDefault(true);
    connect(copy, &QPushButton::clicked, this, &SharePackDialog::copyCode);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const auto files = m_manifest.value("files").toArray();
    m_onlyRequired->setEnabled(!files.isEmpty());
    bool hasManualDownloads = false;
    for (const auto& value : files) {
        const auto file = value.toObject();
        const auto path = file.value("path").toString();
        const auto fileName = QFileInfo(path).fileName();
        const bool manualDownload = SharePack::requiresManualDownload(file);
        const auto name = manualDownload ? tr("%1 [manual file]").arg(fileName) : file.value("name").toString(fileName);
        hasManualDownloads |= manualDownload;
        auto* item = new QTreeWidgetItem(m_mods, { name });
        item->setToolTip(0, path);

        auto* requirement = new QComboBox(m_mods);
        requirement->setAccessibleName(tr("Server requirement for %1").arg(name));
        requirement->addItem(tr("Unspecified"), "unknown");
        requirement->addItem(tr("Required"), "required");
        requirement->addItem(tr("Optional"), "optional");
        const auto saved = file.value("pohonServerRequirement").toString("unknown");
        requirement->setCurrentIndex(requirement->findData(saved));
        const auto declaration = providerDeclarations.value(QFileInfo(path).fileName());
        if (declaration == "required")
            requirement->setToolTip(tr("The mod author declares it required on the server."));
        else if (declaration == "optional")
            requirement->setToolTip(tr("The mod author declares it optional on the server."));
        else if (declaration == "unsupported")
            requirement->setToolTip(tr("The mod author declares it unsupported on the server."));
        m_mods->setItemWidget(item, 1, requirement);
        connect(requirement, &QComboBox::currentIndexChanged, this, [this] {
            m_status->clear();
            updateFilter();
        });
    }
    if (hasManualDownloads)
        guidance->setText(guidance->text() + "\n\n" +
                          tr("Share the files marked Manual file separately. Your friend will see which files to add."));
    connect(m_onlyRequired, &QCheckBox::toggled, this, &SharePackDialog::updateFilter);
}

void SharePackDialog::updateFilter()
{
    for (int i = 0; i < m_mods->topLevelItemCount(); ++i) {
        auto* item = m_mods->topLevelItem(i);
        auto* requirement = static_cast<QComboBox*>(m_mods->itemWidget(item, 1));
        item->setHidden(m_onlyRequired->isChecked() && requirement->currentData().toString() != "required");
    }
}

void SharePackDialog::copyCode()
{
    auto files = m_manifest.value("files").toArray();
    QMap<QString, QString> changed;
    for (qsizetype i = 0; i < files.size(); ++i) {
        auto file = files.at(i).toObject();
        auto* item = m_mods->topLevelItem(i);
        auto* requirement = static_cast<QComboBox*>(m_mods->itemWidget(item, 1));
        const auto selected = requirement->currentData().toString();
        if (selected != file.value("pohonServerRequirement").toString("unknown")) {
            changed.insert(QFileInfo(file.value("path").toString()).fileName(), selected);
            file.insert("pohonServerRequirement", selected);
            files.replace(i, file);
        }
    }

    auto updated = m_manifest;
    updated.insert("files", files);
    QString error;
    const auto code = SharePack::encode(updated, &error);
    if (code.isEmpty()) {
        m_status->setText(error);
        return;
    }
    if (!changed.isEmpty() && !ModFolderModel::setServerRequirements(m_modsDir, changed)) {
        m_status->setText(tr("Could not save the server requirements."));
        return;
    }
    m_manifest = updated;
    QApplication::clipboard()->setText(code);
    m_status->setText(tr("Share code copied to the clipboard."));
}
