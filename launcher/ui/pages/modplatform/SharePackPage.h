// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QWidget>

#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

class SharePackPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit SharePackPage(NewInstanceDialog* dialog);

    QString displayName() const override { return tr("Share Code"); }
    QIcon icon() const override { return QIcon::fromTheme("export"); }
    QString id() const override { return "share-code"; }
    void openedImpl() override;

   private:
    void preview();
    void resetPreview();
    void updateFilter();
    bool prepareInstall(bool requiredOnly);

    NewInstanceDialog* m_dialog;
    QPlainTextEdit* m_code;
    QPushButton* m_preview;
    QLabel* m_summary;
    QTreeWidget* m_mods;
    QCheckBox* m_onlyRequired;
    QPushButton* m_installRequired;
    QJsonObject m_manifest;
};
