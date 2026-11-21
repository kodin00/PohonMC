// SPDX-License-Identifier: GPL-3.0-only

#include "SharePackPage.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

#include "modplatform/share/SharePack.h"
#include "ui/dialogs/NewInstanceDialog.h"

SharePackPage::SharePackPage(NewInstanceDialog* dialog) : m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    m_code = new QPlainTextEdit(this);
    m_code->setPlaceholderText(tr("Paste a modpack share code"));
    m_code->setMaximumHeight(m_code->fontMetrics().lineSpacing() * 5);
    auto* codeLabel = new QLabel(tr("Modpack share &code"), this);
    codeLabel->setBuddy(m_code);
    layout->addWidget(codeLabel);
    layout->addWidget(m_code);

    m_preview = new QPushButton(tr("Preview Modpack"), this);
    m_preview->setAutoDefault(false);
    layout->addWidget(m_preview, 0, Qt::AlignLeft);

    m_summary = new QLabel(this);
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_onlyRequired = new QCheckBox(tr("Show only server-required mods"), this);
    layout->addWidget(m_onlyRequired);

    m_mods = new QTreeWidget(this);
    m_mods->setRootIsDecorated(false);
    m_mods->setUniformRowHeights(true);
    m_mods->setHeaderLabels({ tr("Mod"), tr("Server requirement") });
    m_mods->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_mods, 1);

    m_installRequired = new QPushButton(tr("Install Server-Required Mods Only"), this);
    m_installRequired->setAutoDefault(false);
    layout->addWidget(m_installRequired, 0, Qt::AlignRight);

    connect(m_code, &QPlainTextEdit::textChanged, this, &SharePackPage::resetPreview);
    connect(m_preview, &QPushButton::clicked, this, &SharePackPage::preview);
    connect(m_onlyRequired, &QCheckBox::toggled, this, &SharePackPage::updateFilter);
    connect(m_installRequired, &QPushButton::clicked, this, [this] {
        if (prepareInstall(true))
            m_dialog->accept();
    });
    resetPreview();
}

void SharePackPage::openedImpl()
{
    prepareInstall(false);
}

void SharePackPage::resetPreview()
{
    m_manifest = {};
    m_mods->clear();
    m_summary->clear();
    m_onlyRequired->setChecked(false);
    m_onlyRequired->setEnabled(false);
    m_installRequired->setEnabled(false);
    m_preview->setEnabled(!m_code->document()->isEmpty());
    if (isOpened)
        prepareInstall(false);
}

void SharePackPage::preview()
{
    QString error;
    if (!SharePack::decode(m_code->toPlainText(), &m_manifest, &error)) {
        m_summary->setText(error);
        return;
    }

    const auto dependencies = m_manifest.value("dependencies").toObject();
    QStringList versions{ tr("Minecraft %1").arg(dependencies.value("minecraft").toString()) };
    const QList<std::pair<QString, QString>> loaders = {
        { "forge", tr("Forge") },
        { "neoforge", tr("NeoForge") },
        { "fabric-loader", tr("Fabric Loader") },
        { "quilt-loader", tr("Quilt Loader") },
    };
    for (const auto& [id, name] : loaders) {
        if (dependencies.contains(id))
            versions.append(tr("%1 %2").arg(name, dependencies.value(id).toString()));
    }
    m_summary->setText(versions.join(" · "));

    m_mods->clear();
    bool hasRequired = false;
    const auto files = m_manifest.value("files").toArray();
    for (const auto& value : files) {
        const auto file = value.toObject();
        const auto path = file.value("path").toString();
        auto label = file.value("name").toString(QFileInfo(path).fileName());
        const auto version = file.value("version").toString();
        if (!version.isEmpty())
            label = tr("%1 (%2)").arg(label, version);
        if (path.endsWith(".disabled"))
            label = tr("%1 [disabled]").arg(label);

        const auto requirement = file.value("pohonServerRequirement").toString();
        const bool required = requirement == "required";
        auto* item = new QTreeWidgetItem(m_mods, {
                                                     label,
                                                     required                    ? tr("Required")
                                                     : requirement == "optional" ? tr("Optional")
                                                                                 : tr("Unspecified"),
                                                 });
        item->setToolTip(0, path);
        item->setData(0, Qt::UserRole, required);
        hasRequired |= required;
    }
    m_onlyRequired->setEnabled(!files.isEmpty());
    m_installRequired->setEnabled(hasRequired);
    updateFilter();
    prepareInstall(false);
}

void SharePackPage::updateFilter()
{
    for (int i = 0; i < m_mods->topLevelItemCount(); ++i) {
        auto* item = m_mods->topLevelItem(i);
        item->setHidden(m_onlyRequired->isChecked() && !item->data(0, Qt::UserRole).toBool());
    }
}

bool SharePackPage::prepareInstall(bool requiredOnly)
{
    if (m_manifest.isEmpty()) {
        m_dialog->setSuggestedPack();
        return false;
    }

    auto manifest = m_manifest;
    if (requiredOnly) {
        QJsonArray files;
        const auto allFiles = manifest.value("files").toArray();
        for (const auto& value : allFiles) {
            auto file = value.toObject();
            if (file.value("pohonServerRequirement").toString() != "required")
                continue;
            auto path = file.value("path").toString();
            if (path.endsWith(".disabled"))
                file.insert("path", path.chopped(9));
            files.append(file);
        }
        manifest.insert("files", files);
        QString error;
        if (!SharePack::validate(manifest, &error)) {
            m_summary->setText(error);
            return false;
        }
    }
    m_dialog->setSuggestedPack(manifest.value("name").toString(), new SharePackImportTask(manifest, m_dialog));
    return true;
}
