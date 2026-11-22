#include "LoginWizardPage.h"
#include "minecraft/auth/AccountList.h"
#include "ui/dialogs/ChooseOfflineNameDialog.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui_LoginWizardPage.h"

#include "Application.h"

LoginWizardPage::LoginWizardPage(QWidget* parent) : BaseWizardPage(parent), ui(new Ui::LoginWizardPage)
{
    ui->setupUi(this);
}

LoginWizardPage::~LoginWizardPage()
{
    delete ui;
}

void LoginWizardPage::initializePage() {}

bool LoginWizardPage::validatePage()
{
    return true;
}

void LoginWizardPage::retranslate()
{
    ui->retranslateUi(this);
}

void LoginWizardPage::on_mojangButton_clicked()
{
    wizard()->hide();
    auto account = MSALoginDialog::newAccount(nullptr);
    wizard()->show();
    if (account) {
        useAccount(account);
    }
}

void LoginWizardPage::on_offlineButton_clicked()
{
    wizard()->hide();
    ChooseOfflineNameDialog dialog(tr("Please enter your desired username to add your offline account."), nullptr);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    wizard()->show();
    if (!accepted) {
        return;
    }

    if (const MinecraftAccountPtr account = MinecraftAccount::createOffline(dialog.getUsername())) {
        account->login()->start();  // Offline accounts have no auth steps, so this completes immediately.
        useAccount(account);
    }
}

void LoginWizardPage::useAccount(const MinecraftAccountPtr& account)
{
    APPLICATION->accounts()->addAccount(account);
    APPLICATION->accounts()->setDefaultAccount(account);
    if (wizard()->currentId() == wizard()->pageIds().last()) {
        wizard()->accept();
    } else {
        wizard()->next();
    }
}
