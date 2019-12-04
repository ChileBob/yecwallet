#include "mainwindow.h"
#include "addressbook.h"
#include "viewalladdresses.h"
#include "validateaddress.h"
#include "ui_mainwindow.h"
#include "ui_mobileappconnector.h"
#include "ui_addressbook.h"
#include "ui_nullifiermigration.h"
#include "ui_rescandialog.h"
#include "ui_zboard.h"
#include "ui_privkey.h"
#include "ui_about.h"
#include "ui_settings.h"
#include "ui_turnstile.h"
#include "ui_turnstileprogress.h"
#include "ui_viewalladdresses.h"
#include "ui_validateaddress.h"
#include "controller.h"
#include "balancestablemodel.h"
#include "settings.h"
#include "version.h"
#include "turnstile.h"
#include "senttxstore.h"
#include "connection.h"
#include "requestdialog.h"
#include "websockets.h"
#include <QRegularExpression>

using json = nlohmann::json;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
	// Include css    
    QString theme_name;
    try
    {
       theme_name = Settings::getInstance()->get_theme_name();
    }
    catch (...)
    {
        theme_name = "default";
    }

    this->slot_change_theme(theme_name);

	    
    ui->setupUi(this);
    logger = new Logger(this, QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("zec-qt-wallet.log"));

    // Status Bar
    setupStatusBar();
    
    // Settings editor 
    setupSettingsModal();

    // Set up exit action
    QObject::connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    // Set up donate action
    QObject::connect(ui->actionDonate, &QAction::triggered, this, &MainWindow::donate);

    // File a bug
    QObject::connect(ui->actionFile_a_bug, &QAction::triggered, [=]() {
        QDesktopServices::openUrl(QUrl("https://github.com/ycashfoundation/yecwallet/issues/new"));
    });

    // Set up check for updates action
    QObject::connect(ui->actionCheck_for_Updates, &QAction::triggered, [=] () {
        // Silent is false, so show notification even if no update was found
        rpc->checkForUpdate(false);
    });

    // Request ycash
    QObject::connect(ui->actionRequest_zcash, &QAction::triggered, [=]() {
        RequestDialog::showRequestZcash(this);
    });

    // Pay Ycash URI
    QObject::connect(ui->actionPay_URI, &QAction::triggered, [=] () {
        payZcashURI();
    });

    // Import Private Key
    QObject::connect(ui->actionImport_Private_Key, &QAction::triggered, [=] () {this->importPrivKey(false);});

    // Import Viewing Key
    QObject::connect(ui->actionImport_viewing_key, &QAction::triggered, [=] () {this->importPrivKey(true);});

    // Export All Private Keys
    QObject::connect(ui->actionExport_All_Private_Keys, &QAction::triggered, this, &MainWindow::exportAllKeys);

    // Export All Viewing Keys
    QObject::connect(ui->actionExport_all_viewing_keys, &QAction::triggered, this, &MainWindow::exportAllViewKeys);

    // Backup wallet.dat
    QObject::connect(ui->actionBackup_wallet_dat, &QAction::triggered, this, &MainWindow::backupWalletDat);

    // Export transactions
    QObject::connect(ui->actionExport_transactions, &QAction::triggered, this, &MainWindow::exportTransactions);

    // Validate Address
    QObject::connect(ui->actionValidate_Address, &QAction::triggered, this, &MainWindow::validateAddress);

    // Nullifier Migration
    QObject::connect(ui->actionNullifier_Migration, &QAction::triggered, this, &MainWindow::nullifierMigration);

    // Rescan Blockchain
    QObject::connect(ui->actionRescanBlockchain, &QAction::triggered, this, &MainWindow::rescanBlockchain);

    // Connect mobile app
    QObject::connect(ui->actionConnect_Mobile_App, &QAction::triggered, this, [=] () {
        if (rpc->getConnection() == nullptr)
            return;

        AppDataServer::getInstance()->connectAppDialog(this);
    });

    // Address Book
    QObject::connect(ui->action_Address_Book, &QAction::triggered, this, &MainWindow::addressBook);

    // Set up about action
    QObject::connect(ui->actionAbout, &QAction::triggered, [=] () {
        QDialog aboutDialog(this);
        Ui_about about;
        about.setupUi(&aboutDialog);
        Settings::saveRestore(&aboutDialog);

        QString version    = QString("Version ") % QString(APP_VERSION) % " (" % QString(__DATE__) % ")";
        about.versionLabel->setText(version);
        
        aboutDialog.exec();
    });

    // Initialize to the balances tab
    ui->tabWidget->setCurrentIndex(0);

    // The ycashd tab is hidden by default, and only later added in if the embedded ycashd is started
    zcashdtab = ui->tabWidget->widget(4);
    ui->tabWidget->removeTab(4);

    setupSendTab();
    setupTransactionsTab();
    setupReceiveTab();
    setupBalancesTab();
    setupTurnstileDialog();
    setupZcashdTab();

    rpc = new Controller(this);

    restoreSavedStates();

    if (AppDataServer::getInstance()->isAppConnected()) {
        auto ads = AppDataServer::getInstance();

        QString wormholecode = "";
        if (ads->getAllowInternetConnection())
            wormholecode = ads->getWormholeCode(ads->getSecretHex());

        createWebsocket(wormholecode);
    }
}
 
void MainWindow::createWebsocket(QString wormholecode) {
    qDebug() << "Listening for app connections on port 8237";
    // Create the websocket server, for listening to direct connections
    wsserver = new WSServer(8237, false, this);

    if (!wormholecode.isEmpty()) {
        // Connect to the wormhole service
        wormhole = new WormholeClient(this, wormholecode);
    }
}

void MainWindow::stopWebsocket() {
    delete wsserver;
    wsserver = nullptr;

    delete wormhole;
    wormhole = nullptr;

    qDebug() << "Websockets for app connections shut down";
}

bool MainWindow::isWebsocketListening() {
    return wsserver != nullptr;
}

void MainWindow::replaceWormholeClient(WormholeClient* newClient) {
    delete wormhole;
    wormhole = newClient;
}

void MainWindow::restoreSavedStates() {
    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());

    ui->balancesTable->horizontalHeader()->restoreState(s.value("baltablegeometry").toByteArray());
    ui->transactionsTable->horizontalHeader()->restoreState(s.value("tratablegeometry").toByteArray());

    // Explicitly set the tx table resize headers, since some previous values may have made them
    // non-expandable.
    ui->transactionsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    ui->transactionsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
}

void MainWindow::doClose() {
    closeEvent(nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s;

    s.setValue("geometry", saveGeometry());
    s.setValue("baltablegeometry", ui->balancesTable->horizontalHeader()->saveState());
    s.setValue("tratablegeometry", ui->transactionsTable->horizontalHeader()->saveState());

    s.sync();

    // Let the RPC know to shut down any running service.
    rpc->shutdownZcashd();

    // Bubble up
    if (event)
        QMainWindow::closeEvent(event);
}


void MainWindow::setupTurnstileDialog() {        
    // Turnstile migration
    QObject::connect(ui->actionTurnstile_Migration, &QAction::triggered, [=] () {
        // If the underlying zcashd has support for the migration and there is no existing migration
        // in progress, use that.         
        if (rpc->getMigrationStatus()->available) {
            Turnstile::showZcashdMigration(this);
        } else {
            // Else, do nothing
        }
    });

}

void MainWindow::setupStatusBar() {
    // Status Bar
    loadingLabel = new QLabel();
    loadingMovie = new QMovie(":/icons/res/loading.gif");
    loadingMovie->setScaledSize(QSize(32, 16));
    loadingMovie->start();
    loadingLabel->setAttribute(Qt::WA_NoSystemBackground);
    loadingLabel->setMovie(loadingMovie);

    ui->statusBar->addPermanentWidget(loadingLabel);
    loadingLabel->setVisible(false);

    // Custom status bar menu
    ui->statusBar->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(ui->statusBar, &QStatusBar::customContextMenuRequested, [=](QPoint pos) {
        auto msg = ui->statusBar->currentMessage();
        QMenu menu(this);

        if (!msg.isEmpty() && msg.startsWith(Settings::txidStatusMessage)) {
            auto txid = msg.split(":")[1].trimmed();
            menu.addAction(tr("Copy txid"), [=]() {
                QGuiApplication::clipboard()->setText(txid);
            });
            menu.addAction(tr("View tx on block explorer"), [=]() {
                Settings::openTxInExplorer(txid);
            });
        }

        menu.addAction(tr("Refresh"), [=]() {
            rpc->refresh(true);
        });
        QPoint gpos(mapToGlobal(pos).x(), mapToGlobal(pos).y() + this->height() - ui->statusBar->height());
        menu.exec(gpos);
    });

    statusLabel = new QLabel();
    ui->statusBar->addPermanentWidget(statusLabel);

    statusIcon = new QLabel();
    ui->statusBar->addPermanentWidget(statusIcon);
}

void MainWindow::setupSettingsModal() {    
    // Set up File -> Settings action
    QObject::connect(ui->actionSettings, &QAction::triggered, [=]() {
        QDialog settingsDialog(this);
        Ui_Settings settings;
        settings.setupUi(&settingsDialog);
        Settings::saveRestore(&settingsDialog);

        // Setup save sent check box
        QObject::connect(settings.chkSaveTxs, &QCheckBox::stateChanged, [=](auto checked) {
            Settings::getInstance()->setSaveZtxs(checked);
        });

        // Setup clear button
        QObject::connect(settings.btnClearSaved, &QCheckBox::clicked, [=]() {
            if (QMessageBox::warning(this, "Clear saved history?",
                "Shielded z-Address transactions are stored locally in your wallet, outside ycashd. You may delete this saved information safely any time for your privacy.\nDo you want to delete the saved shielded transactions now?",
                QMessageBox::Yes, QMessageBox::Cancel)) {
                    SentTxStore::deleteHistory();
                    // Reload after the clear button so existing txs disappear
                    rpc->refresh(true);
            }
        });

        // Setup theme combo
        int theme_index = settings.comboBoxTheme->findText(Settings::getInstance()->get_theme_name(), Qt::MatchExactly);
        settings.comboBoxTheme->setCurrentIndex(theme_index);

        QObject::connect(settings.comboBoxTheme, &QComboBox::currentTextChanged, [=] (QString theme_name) {
            this->slot_change_theme(theme_name);
            // Tell the user to restart
            QMessageBox::information(this, tr("Restart"), tr("Please restart Yecwallet to have the theme apply"), QMessageBox::Ok);
        });

        // Save sent transactions
        settings.chkSaveTxs->setChecked(Settings::getInstance()->getSaveZtxs());

        // Custom fees
        settings.chkCustomFees->setChecked(Settings::getInstance()->getAllowCustomFees());

        // Auto shielding
        settings.chkAutoShield->setChecked(Settings::getInstance()->getAutoShield());

        // Check for updates
        settings.chkCheckUpdates->setChecked(Settings::getInstance()->getCheckForUpdates());

        // Fetch prices
        settings.chkFetchPrices->setChecked(Settings::getInstance()->getAllowFetchPrices());

        // Use Tor
        bool isUsingTor = false;
        if (rpc->getConnection() != nullptr) {
            isUsingTor = !rpc->getConnection()->config->proxy.isEmpty();
        }
        settings.chkTor->setChecked(isUsingTor);
        if (rpc->getEZcashD() == nullptr) {
            settings.chkTor->setEnabled(false);
            settings.lblTor->setEnabled(false);
            QString tooltip = tr("Tor configuration is available only when running an embedded ycashd.");
            settings.chkTor->setToolTip(tooltip);
            settings.lblTor->setToolTip(tooltip);
        }

        // Connection Settings
        QIntValidator validator(0, 65535);
        settings.port->setValidator(&validator);

        // If values are coming from ycash.conf, then disable all the fields
        auto zcashConfLocation = Settings::getInstance()->getZcashdConfLocation();
        if (!zcashConfLocation.isEmpty()) {
            settings.confMsg->setText("Settings are being read from \n" + zcashConfLocation);
            settings.hostname->setEnabled(false);
            settings.port->setEnabled(false);
            settings.rpcuser->setEnabled(false);
            settings.rpcpassword->setEnabled(false);
        }
        else {
            settings.confMsg->setText("No local ycash.conf found. Please configure connection manually.");
            settings.hostname->setEnabled(true);
            settings.port->setEnabled(true);
            settings.rpcuser->setEnabled(true);
            settings.rpcpassword->setEnabled(true);
        }

        // Load current values into the dialog        
        auto conf = Settings::getInstance()->getSettings();
        settings.hostname->setText(conf.host);
        settings.port->setText(conf.port);
        settings.rpcuser->setText(conf.rpcuser);
        settings.rpcpassword->setText(conf.rpcpassword);

        // Connection tab by default
        settings.tabWidget->setCurrentIndex(0);

        // Enable the troubleshooting options only if using embedded ycashd
        // if (!rpc->isEmbedded()) {
        //     settings.chkRescan->setEnabled(false);
        //     settings.chkRescan->setToolTip(tr("You're using an external ycashd. Please restart ycashd with -rescan"));

        //     settings.chkReindex->setEnabled(false);
        //     settings.chkReindex->setToolTip(tr("You're using an external ycashd. Please restart ycashd with -reindex"));
        // }

        if (settingsDialog.exec() == QDialog::Accepted) {
            // Custom fees
            bool customFees = settings.chkCustomFees->isChecked();
            Settings::getInstance()->setAllowCustomFees(customFees);
            ui->minerFeeAmt->setReadOnly(!customFees);
            if (!customFees)
                ui->minerFeeAmt->setText(Settings::getDecimalString(Settings::getMinerFee()));

            // Auto shield
            Settings::getInstance()->setAutoShield(settings.chkAutoShield->isChecked());

            // Check for updates
            Settings::getInstance()->setCheckForUpdates(settings.chkCheckUpdates->isChecked());

            // Allow fetching prices
            Settings::getInstance()->setAllowFetchPrices(settings.chkFetchPrices->isChecked());

            if (!isUsingTor && settings.chkTor->isChecked()) {
                // If "use tor" was previously unchecked and now checked
                Settings::addToZcashConf(zcashConfLocation, "proxy=127.0.0.1:9050");
                rpc->getConnection()->config->proxy = "proxy=127.0.0.1:9050";

                QMessageBox::information(this, tr("Enable Tor"), 
                    tr("Connection over Tor has been enabled. To use this feature, you need to restart YecWallet."), 
                    QMessageBox::Ok);
            }

            if (isUsingTor && !settings.chkTor->isChecked()) {
                // If "use tor" was previously checked and now is unchecked
                Settings::removeFromZcashConf(zcashConfLocation, "proxy");
                rpc->getConnection()->config->proxy.clear();

                QMessageBox::information(this, tr("Disable Tor"),
                    tr("Connection over Tor has been disabled. To fully disconnect from Tor, you need to restart YecWallet."),
                    QMessageBox::Ok);
            }

            if (zcashConfLocation.isEmpty()) {
                // Save settings
                Settings::getInstance()->saveSettings(
                    settings.hostname->text(),
                    settings.port->text(),
                    settings.rpcuser->text(),
                    settings.rpcpassword->text());
                
                auto cl = new ConnectionLoader(this, rpc);
                cl->loadConnection();
            }

            // Check to see if rescan or reindex have been enabled
            bool showRestartInfo = false;

            if (settings.chkReindex->isChecked()) {
                Settings::addToZcashConf(zcashConfLocation, "reindex=1");
                showRestartInfo = true;
            }

            if (showRestartInfo) {
                auto desc = tr("YecWallet needs to restart to rescan/reindex. YecWallet will now close, please restart YecWallet to continue");
                
                QMessageBox::information(this, tr("Restart YecWallet"), desc, QMessageBox::Ok);
                QTimer::singleShot(1, [=]() { this->close(); });
            }
        }
    });
}

void MainWindow::addressBook() {
    // Check to see if there is a target.
    QRegularExpression re("Address[0-9]+", QRegularExpression::CaseInsensitiveOption);
    for (auto target: ui->sendToWidgets->findChildren<QLineEdit *>(re)) {
        if (target->hasFocus()) {
            AddressBook::open(this, target);
            return;
        }
    };

    // If there was no target, then just run with no target.
    AddressBook::open(this);
}


void MainWindow::donate() {
    // Set up a donation to me :)
    clearSendForm();

    ui->Address1->setText(Settings::getDonationAddr());
    ui->Address1->setCursorPosition(0);
    ui->Amount1->setText("0.01");
    ui->MemoTxt1->setText(tr("Thanks for supporting YecWallet!"));

    ui->statusBar->showMessage(tr("Donate 0.01 ") % Settings::getTokenName() % tr(" to support YecWallet"));

    // And switch to the send tab.
    ui->tabWidget->setCurrentIndex(1);
}


/**
 * Rescan the blockchain
 */
void MainWindow::rescanBlockchain() {
    if (!getRPC() || !getRPC()->getConnection())
        return;

    QDialog d(this);
    Ui_rescanDialog r;
    r.setupUi(&d);
    Settings::saveRestore(&d);

    // Set the default start height to 0
    r.txtStartHeight->setText("0");
    r.txtStartHeight->setValidator(new QIntValidator(0, 10000000, &d));

    if (d.exec() == QDialog::Accepted) {
        int startHeight = r.txtStartHeight->text().toInt();
        
        // Call the RPC. We ignore the return callback, since we'll monitor the progress via the rescaninfo RPC
        getRPC()->rescanBlockchain(startHeight, [=](auto) {});

        // Trigger monitoring the rescan with a slight delay, allowing the previous RPC to complete
        QTimer::singleShot(1000, [=]() {
            this->getRPC()->refreshRescanStatus();
        });
    }
} 


/** Migrate sapling nullifiers */
void MainWindow::nullifierMigration() {
        // Make sure everything is up and running
    if (!getRPC() || !getRPC()->getConnection())
        return;

    QDialog* d = new QDialog(this);
    Ui_NullifierMigrationDialog* nm = new Ui_NullifierMigrationDialog();
    nm->setupUi(d);
    Settings::saveRestore(d);    
    
    auto saplingBalances = new QList<QPair<QString, double>>();
    auto possibleDestinations = new QStringList();

    // Populate the table with sapling balances
    auto balances = rpc->getModel()->getAllBalances();
    auto zaddrs   = rpc->getModel()->getAllZAddresses();

    for (auto z: zaddrs) {
        if (Settings::getInstance()->isSaplingAddress(z)) {
            if (balances.value(z) == 0) {
                *possibleDestinations << z;
            } else {
                saplingBalances->push_back(QPair<QString, double>(z, balances.value(z)));
            }
        }
    }

    nm->balancesTable->setRowCount(saplingBalances->size());
    nm->balancesTable->setColumnCount(2);

    QStringList headers;
    headers << tr("Address") << tr("Balance");
    nm->balancesTable->setHorizontalHeaderLabels(headers);
    nm->balancesTable->horizontalHeader()->setStyleSheet("font-weight: bold;");
    nm->balancesTable->verticalHeader()->setVisible(false);
    
    nm->balancesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    nm->balancesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    for (int row = 0; row < saplingBalances->size(); row++) {
        nm->balancesTable->setItem(row, 0, new QTableWidgetItem(saplingBalances->value(row).first));
        nm->balancesTable->setItem(row, 1, new QTableWidgetItem(Settings::getZECDisplayFormat(saplingBalances->value(row).second)));
    }

    auto fnShowDialog = [=] () {
        for (auto a : *possibleDestinations) {
            nm->cmbAddresses->addItem(a, 0);
        }

        if (d->exec() == QDialog::Accepted) {
            auto destAddr = nm->cmbAddresses->currentText();

            // Create and send all the transactions
            QList<Tx> transactions;
            for (auto fromAddr: *saplingBalances) {
                Tx tx;
                tx.fromAddr = fromAddr.first;
                auto memo = "Nullifier Migration Transaction from " + fromAddr.first;
                tx.toAddrs.push_back( ToFields{destAddr, fromAddr.second - Settings::getMinerFee(), memo, memo.toUtf8().toHex()} );
                tx.fee = Settings::getMinerFee();

                transactions.push_back(tx);
            }

            // Then execute all the transactions
            for (auto tx: transactions) {
                getRPC()->executeStandardUITransaction(tx);
            }

            // Tell the user to backup the wallet. 
            QMessageBox::information(this, tr("Migration Started"), 
                tr("The nullifier migration transactions will now be executed.\nPlease make sure you BACKUP YOUR WALLET!"), 
                QMessageBox::Ok);
        };

        delete saplingBalances;
        delete possibleDestinations;
        delete nm;
        delete d;
    };

    // We need a possible destination that is not in the list of balances, since we can't send a z_sendmany
    // transaction to the same address.
    // If there isn't a possible destination, create one.
    if (possibleDestinations->isEmpty()) {
        getRPC()->createNewZaddr(true, [=] (const json& reply) {
            QString addr = QString::fromStdString(reply.get<json::string_t>());
            *possibleDestinations << addr;
            fnShowDialog();
        });
    } else {
        fnShowDialog();
    }

}

/**
 * Validate an address
 */
void MainWindow::validateAddress() {
    // Make sure everything is up and running
    if (!getRPC() || !getRPC()->getConnection())
        return;

    // First thing is ask the user for an address
    bool ok;
    auto address = QInputDialog::getText(this, tr("Enter Address to validate"), 
        tr("Transparent or Shielded Address:") + QString(" ").repeated(140),    // Pad the label so the dialog box is wide enough
        QLineEdit::Normal, "", &ok);
    if (!ok)
        return;

    getRPC()->validateAddress(address, [=] (json props) {
        QDialog d(this);
        Ui_ValidateAddress va;
        va.setupUi(&d);
        Settings::saveRestore(&d);
        Settings::saveRestoreTableHeader(va.tblProps, &d, "validateaddressprops");
        va.tblProps->horizontalHeader()->setStretchLastSection(true);

        va.lblAddress->setText(address);

        QList<QPair<QString, QString>> propsList;
        for (auto it = props.begin(); it != props.end(); it++) {

            propsList.append(
                QPair<QString, QString>(
                    QString::fromStdString(it.key()), QString::fromStdString(it.value().dump()))
            );
        }

        ValidateAddressesModel model(va.tblProps, propsList);
        va.tblProps->setModel(&model);

        d.exec();
    });

}


void MainWindow::doImport(QList<QString>* keys, int rescanHeight) {
    if (rpc->getConnection() == nullptr) {
        // No connection, just return
        return;
    }

    if (keys->isEmpty()) {
        delete keys;
        ui->statusBar->showMessage(tr("Started rescan. Please wait. This will take several hours..."));
        return;
    }

    // Pop the first key
    QString key = keys->first();
    keys->pop_front();
    bool rescan = keys->isEmpty();

    if (key.startsWith("SK") ||     // Sprout Secret key
        key.startsWith("secret")) { // Sapling Secret key
        rpc->importZPrivKey(key, rescan, rescanHeight, [=] (auto) { this->doImport(keys, rescanHeight); });
    } else if (key.startsWith("zivk")) { // Sapling view key
        // Sapling viewing keys also need a corresponding address. The address is expected to be the second half of the string, 
        // separated by a space or a "#"
        auto parts = key.trimmed().split(QRegularExpression("[ #]+"));
        if (parts.length() != 2) {
            QMessageBox::critical(this, tr("Error importing viewing key"), 
                tr("Couldn't find the address for the viewing key. Please type in the viewing key and address on the same line. eg:") + 
                "\n" + "zivks1k...sjjx9 # ys1fzse2...8vxr9t\n", 
                QMessageBox::Ok);
            return;
        }

        QString viewkey = parts[0];
        QString address = parts[1];
        rpc->importZViewingKey(viewkey, rescan, rescanHeight, address, [=] (auto) { this->doImport(keys, rescanHeight); });
    }
    else {
        rpc->importTPrivKey(key, rescan, rescanHeight, [=] (auto) { this->doImport(keys, rescanHeight); });
    }

    // And if this was a rescan, show the rescan dialog box
    if (rescan) {
        // Do it with a slight delay, allowing the previous RPC to complete
        QTimer::singleShot(1000, [=]() {
            this->getRPC()->refreshRescanStatus();
        });
    }
}


// Callback invoked when the RPC has finished loading all the balances, and the UI 
// is now ready to send transactions.
void MainWindow::balancesReady() {
    // First-time check
    if (uiPaymentsReady)
        return;

    uiPaymentsReady = true;
    qDebug() << "Payment UI now ready!";

    // There is a pending URI payment (from the command line, or from a secondary instance),
    // process it.
    if (!pendingURIPayment.isEmpty()) {
        qDebug() << "Paying ycash URI";
        payZcashURI(pendingURIPayment);
        pendingURIPayment = "";
    }

    // Execute any pending Recurring payments
    Recurring::getInstance()->processPending(this);
}

// Event filter for MacOS specific handling of payment URIs
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->url().isEmpty())
            payZcashURI(fileEvent->url().toString());

        return true;
    }

    return QObject::eventFilter(object, event);
}


// Pay the Ycash URI by showing a confirmation window. If the URI parameter is empty, the UI
// will prompt for one. If the myAddr is empty, then the default from address is used to send
// the transaction.
void MainWindow::payZcashURI(QString uri, QString myAddr) {
    // If the Payments UI is not ready (i.e, all balances have not loaded), defer the payment URI
    if (!isPaymentsReady()) {
        qDebug() << "Payment UI not ready, waiting for UI to pay URI";
        pendingURIPayment = uri;
        return;
    }

    // If there was no URI passed, ask the user for one.
    if (uri.isEmpty()) {
        uri = QInputDialog::getText(this, tr("Paste Ycash URI"),
            "Ycash URI" + QString(" ").repeated(180));
    }

    // If there's no URI, just exit
    if (uri.isEmpty())
        return;

    // Extract the address
    qDebug() << "Received URI " << uri;
    PaymentURI paymentInfo = Settings::parseURI(uri);
    if (!paymentInfo.error.isEmpty()) {
        QMessageBox::critical(this, tr("Error paying ycash URI"), 
                tr("URI should be of the form 'ycash:<addr>?amt=x&memo=y") + "\n" + paymentInfo.error);
        return;
    }

    // Now, set the fields on the send tab
    clearSendForm();

    if (!myAddr.isEmpty()) {
        ui->inputsCombo->setCurrentText(myAddr);
    }

    ui->Address1->setText(paymentInfo.addr);
    ui->Address1->setCursorPosition(0);
    ui->Amount1->setText(Settings::getDecimalString(paymentInfo.amt.toDouble()));
    ui->MemoTxt1->setText(paymentInfo.memo);

    // And switch to the send tab.
    ui->tabWidget->setCurrentIndex(1);
    raise();

    // And click the send button if the amount is > 0, to validate everything. If everything is OK, it will show the confirm box
    // else, show the error message;
    if (paymentInfo.amt > 0) {
        sendButton();
    }
}


void MainWindow::importPrivKey(bool viewKeys) {
    QDialog d(this);
    Ui_PrivKey pui;
    pui.setupUi(&d);
    Settings::saveRestore(&d);

    if (viewKeys) {
        d.setWindowTitle(tr("Viewing Keys"));
    }

    pui.buttonBox->button(QDialogButtonBox::Save)->setVisible(false);
    if (viewKeys) {
        pui.helpLbl->setText(QString() % 
        tr("Please paste the Sapling incoming viewing keys here, one per line, in the format:\n") %
        tr("<incoming_viewing_key> # <Sapling address>\n") %
        tr("For example:\n") %
        tr("zivks1k...sjjx9 # ys1fzse2...8vxr9t \n") %
        tr("The incoming viewing keys will be imported into your connected ycashd node."));
    } else {
        pui.helpLbl->setText(QString() %
                            tr("Please paste your private keys (z-Addr or t-Addr) here, one per line") % ".\n" %
                            tr("The keys will be imported into your connected ycashd node"));  
    }
    pui.txtRescanHeight->setText("0");
    pui.txtRescanHeight->setValidator(new QIntValidator(0, 10000000, this));

    if (d.exec() == QDialog::Accepted && !pui.privKeyTxt->toPlainText().trimmed().isEmpty()) {
        auto rawkeys = pui.privKeyTxt->toPlainText().trimmed().split("\n");
        int rescanHeight = pui.txtRescanHeight->text().toInt();

        QList<QString> keysTmp;
        // Filter out all the empty keys.
        std::copy_if(rawkeys.begin(), rawkeys.end(), std::back_inserter(keysTmp), [=] (auto key) {
            return !key.startsWith("#") && !key.trimmed().isEmpty();
        });

        auto keys = new QList<QString>();
        std::transform(keysTmp.begin(), keysTmp.end(), std::back_inserter(*keys), [=](auto key) {
            if (key.startsWith("zivk"))     // For viewkeys, preserve the whole line
                return key.trimmed();
            else 
                return key.trimmed().split(" ")[0];
        });

        // Special case. 
        // Sometimes, when importing from a paperwallet or such, the key is split by newlines, and might have 
        // been pasted like that. So check to see if the whole thing is one big private key
        if (Settings::getInstance()->isValidSaplingPrivateKey(keys->join(""))) {
            auto multiline = keys;
            keys = new QList<QString>();
            keys->append(multiline->join(""));
            delete multiline;
        }

        // Start the import. The function takes ownership of 'keys'
        QTimer::singleShot(1, [=]() {
            doImport(keys, rescanHeight);
        });
    }
}

/** 
 * Export transaction history into a CSV file
 */
void MainWindow::exportTransactions() {
    // First, get the export file name
    QString exportName = "ycash-transactions-" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".csv";

    QUrl csvName = QFileDialog::getSaveFileUrl(this, 
            tr("Export transactions"), exportName, "CSV file (*.csv)");

    if (csvName.isEmpty())
        return;

    if (!rpc->getTransactionsModel()->exportToCsv(csvName.toLocalFile())) {
        QMessageBox::critical(this, tr("Error"), 
            tr("Error exporting transactions, file was not saved"), QMessageBox::Ok);
    }
} 

/**
 * Backup the wallet.dat file. This is kind of a hack, since it has to read from the filesystem rather than an RPC call
 * This might fail for various reasons - Remote ycashd, non-standard locations, custom params passed to ycashd, many others
*/
void MainWindow::backupWalletDat() {
    if (!rpc->getConnection())
        return;

    QDir zcashdir(rpc->getConnection()->config->zcashDir);
    QString backupDefaultName = "ycash-wallet-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".dat";

    if (Settings::getInstance()->isTestnet()) {
        zcashdir.cd("testnet3");
        backupDefaultName = "testnet-" + backupDefaultName;
    }
    
    QFile wallet(zcashdir.filePath("wallet.dat"));
    if (!wallet.exists()) {
        QMessageBox::critical(this, tr("No wallet.dat"), tr("Couldn't find the wallet.dat on this computer") + "\n" +
            tr("You need to back it up from the machine ycashd is running on"), QMessageBox::Ok);
        return;
    }
    
    QUrl backupName = QFileDialog::getSaveFileUrl(this, tr("Backup wallet.dat"), backupDefaultName, "Data file (*.dat)");
    if (backupName.isEmpty())
        return;

    if (!wallet.copy(backupName.toLocalFile())) {
        QMessageBox::critical(this, tr("Couldn't backup"), tr("Couldn't backup the wallet.dat file.") + 
            tr("You need to back it up manually."), QMessageBox::Ok);
    }
}

void MainWindow::exportAllKeys() {
    exportKeys("");
}

void MainWindow::exportAllViewKeys() {
    exportKeys("", true);
}

void MainWindow::exportKeys(QString addr, bool viewkey) {
    bool allKeys = addr.isEmpty() ? true : false;

    QDialog d(this);
    Ui_PrivKey pui;
    pui.setupUi(&d);
    
    // Make the window big by default
    auto ps = this->geometry();
    QMargins margin = QMargins() + 50;
    d.setGeometry(ps.marginsRemoved(margin));

    Settings::saveRestore(&d);

    pui.privKeyTxt->setPlainText(tr("This might take several minutes. Loading..."));
    pui.privKeyTxt->setReadOnly(true);
    pui.privKeyTxt->setLineWrapMode(QPlainTextEdit::LineWrapMode::NoWrap);

    if (viewkey) {
        if (allKeys)
            pui.helpLbl->setText(tr("These are all the viewing keys for all the addresses in your wallet"));
        else
            pui.helpLbl->setText(tr("Viewing key for ") + addr);
    } else {
        if (allKeys)
            pui.helpLbl->setText(tr("These are all the private keys for all the addresses in your wallet"));
        else
            pui.helpLbl->setText(tr("Private key for ") + addr);
    }

    // Disable the save button until it finishes loading
    pui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);
    pui.buttonBox->button(QDialogButtonBox::Ok)->setVisible(false);

    // Hide the rescanHeight button
    pui.lblRescanHeight->setVisible(false);
    pui.txtRescanHeight->setVisible(false);

    // Wire up save button
    QString filenamestr;
    if (viewkey) {
        filenamestr = allKeys ? "ycash-all-viewingkeys.txt" : "ycash-viewingkey.txt";
    } else {
        filenamestr = allKeys ? "ycash-all-privatekeys.txt" : "ycash-privatekey.txt";
    }

    QObject::connect(pui.buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, [=] () {
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"), filenamestr);
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::information(this, tr("Unable to open file"), file.errorString());
            return;
        }        
        QTextStream out(&file);
        out << pui.privKeyTxt->toPlainText();
    });

    // Call the API
    auto isDialogAlive = std::make_shared<bool>(true);

    auto fnUpdateUIWithKeys = [=](QList<QPair<QString, QString>> privKeys) {
        // Check to see if we are still showing.
        if (! *(isDialogAlive.get()) ) return;

        QString allKeysTxt;
        for (auto keypair : privKeys) {
            allKeysTxt = allKeysTxt % keypair.second % " # " % keypair.first % "\n";
        }

        pui.privKeyTxt->setPlainText(allKeysTxt);
        pui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
    };

    auto fnAddKey = [=](json key) {
        QList<QPair<QString, QString>> singleAddrKey;
        singleAddrKey.push_back(QPair<QString, QString>(addr, QString::fromStdString(key.get<json::string_t>())));
        fnUpdateUIWithKeys(singleAddrKey);
    };

    if (viewkey) {
        if (allKeys) {
            rpc->fetchAllViewingKeys(fnUpdateUIWithKeys);
        } else {
            if (Settings::getInstance()->isZAddress(addr)) {
                rpc->fetchZViewingKey(addr, fnAddKey);
            } else {
                // T addresses don't have viewing keys
            }
        }
    } else {
        if (allKeys) {
            rpc->fetchAllPrivKeys(fnUpdateUIWithKeys);
        }
        else {        
            if (Settings::getInstance()->isZAddress(addr)) {
                rpc->fetchZPrivKey(addr, fnAddKey);
            }
            else {
                rpc->fetchTPrivKey(addr, fnAddKey);
            }        
        }
    }
    
    d.exec();
    *isDialogAlive = false;
}

void MainWindow::setupBalancesTab() {
    ui->unconfirmedWarning->setVisible(false);
    ui->lblSyncWarning->setVisible(false);
    ui->lblSyncWarningReceive->setVisible(false);

    // Double click on balances table
    auto fnDoSendFrom = [=](const QString& addr, const QString& to = QString(), bool sendMax = false) {
        // Find the inputs combo
        for (int i = 0; i < ui->inputsCombo->count(); i++) {
            auto inputComboAddress = ui->inputsCombo->itemText(i);
            if (inputComboAddress.startsWith(addr)) {
                ui->inputsCombo->setCurrentIndex(i);
                break;
            }
        }

        // If there's a to address, add that as well
        if (!to.isEmpty()) {
            // Remember to clear any existing address fields, because we are creating a new transaction.
            this->clearSendForm();
            ui->Address1->setText(to);
        }

        // See if max button has to be checked
        if (sendMax) {
            ui->Max1->setChecked(true);
        }

        // And switch to the send tab.
        ui->tabWidget->setCurrentIndex(1);
    };

    // Double click opens up memo if one exists
    QObject::connect(ui->balancesTable, &QTableView::doubleClicked, [=](auto index) {
        index = index.sibling(index.row(), 0);
        auto addr = AddressBook::addressFromAddressLabel(ui->balancesTable->model()->data(index).toString());
        
        fnDoSendFrom(addr);
    });

    // Setup context menu on balances tab
    ui->balancesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(ui->balancesTable, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
        QModelIndex index = ui->balancesTable->indexAt(pos);
        if (index.row() < 0) return;

        index = index.sibling(index.row(), 0);
        auto addr = AddressBook::addressFromAddressLabel(
                            ui->balancesTable->model()->data(index).toString());

        QMenu menu(this);

        menu.addAction(tr("Copy address"), [=] () {
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setText(addr);            
            ui->statusBar->showMessage(tr("Copied to clipboard"), 3 * 1000);
        });

        menu.addAction(tr("Get private key"), [=] () {
            this->exportKeys(addr);
        });

        menu.addAction("Send from " % addr.left(40) % (addr.size() > 40 ? "..." : ""), [=]() {
            fnDoSendFrom(addr);
        });

        if (Settings::isTAddress(addr)) {
            auto defaultSapling = rpc->getDefaultSaplingAddress();
            if (!defaultSapling.isEmpty()) {
                menu.addAction(tr("Shield balance to Sapling"), [=] () {
                    fnDoSendFrom(addr, defaultSapling, true);
                });
            }

            menu.addAction(tr("View on block explorer"), [=] () {
                Settings::openAddressInExplorer(addr);
            });
        }

        menu.exec(ui->balancesTable->viewport()->mapToGlobal(pos));            
    });
}

void MainWindow::setupZcashdTab() {    
    ui->zcashdlogo->setBasePixmap(QPixmap(":/img/res/zcashdlogo.gif"));
}

void MainWindow::setupTransactionsTab() {
    // Double click opens up memo if one exists
    QObject::connect(ui->transactionsTable, &QTableView::doubleClicked, [=] (auto index) {
        auto txModel = dynamic_cast<TxTableModel *>(ui->transactionsTable->model());
        QString memo = txModel->getMemo(index.row());

        if (!memo.isEmpty()) {
            QMessageBox mb(QMessageBox::Information, tr("Memo"), memo, QMessageBox::Ok, this);
            // Don't render html in the memo to avoid phishing-type attacks
            // revist this in the future once the design  of how to best handle memo based applications exists.
            mb.setTextFormat(Qt::PlainText);
            mb.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            mb.exec();
        }
    });

    // Set up context menu on transactions tab
    ui->transactionsTable->setContextMenuPolicy(Qt::CustomContextMenu);

    // Table right click
    QObject::connect(ui->transactionsTable, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
        QModelIndex index = ui->transactionsTable->indexAt(pos);
        if (index.row() < 0) return;

        QMenu menu(this);

        auto txModel = dynamic_cast<TxTableModel *>(ui->transactionsTable->model());

        QString txid = txModel->getTxId(index.row());
        QString memo = txModel->getMemo(index.row());
        QString addr = txModel->getAddr(index.row());

        menu.addAction(tr("Copy txid"), [=] () {            
            QGuiApplication::clipboard()->setText(txid);
            ui->statusBar->showMessage(tr("Copied to clipboard"), 3 * 1000);
        });

        if (!addr.isEmpty()) {
            menu.addAction(tr("Copy address"), [=] () {
                QGuiApplication::clipboard()->setText(addr);
                ui->statusBar->showMessage(tr("Copied to clipboard"), 3 * 1000);
            });
        }

        menu.addAction(tr("View on block explorer"), [=] () {
            Settings::openTxInExplorer(txid);
        });

        // Payment Request
        if (!memo.isEmpty() && memo.startsWith("ycash:")) {
            menu.addAction(tr("View Payment Request"), [=] () {
                RequestDialog::showPaymentConfirmation(this, memo);
            });
        }

        // View Memo
        if (!memo.isEmpty()) {
            menu.addAction(tr("View Memo"), [=] () {               
                QMessageBox mb(QMessageBox::Information, tr("Memo"), memo, QMessageBox::Ok, this);
                // Don't render html in the memo to avoid phishing-type attacks
                // revist this in the future once the design  of how to best handle memo based applications exists.
                mb.setTextFormat(Qt::PlainText);
                mb.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
                mb.exec();
            });
        }

        // If memo contains a reply to address, add a "Reply to" menu item
        if (!memo.isEmpty()) {
            int lastPost     = memo.trimmed().lastIndexOf(QRegExp("[\r\n]+"));
            QString lastWord = memo.right(memo.length() - lastPost - 1);
            
            if (Settings::getInstance()->isSaplingAddress(lastWord) || 
                Settings::getInstance()->isSproutAddress(lastWord)) {
                menu.addAction(tr("Reply to ") + lastWord.left(25) + "...", [=]() {
                    // First, cancel any pending stuff in the send tab by pretending to click
                    // the cancel button
                    cancelButton();

                    // Then set up the fields in the send tab
                    ui->Address1->setText(lastWord);
                    ui->Address1->setCursorPosition(0);
                    ui->Amount1->setText("0.0001");

                    // And switch to the send tab.
                    ui->tabWidget->setCurrentIndex(1);

                    qApp->processEvents();

                    // Click the memo button
                    this->memoButtonClicked(1, true);
                });
            }
        }

        menu.exec(ui->transactionsTable->viewport()->mapToGlobal(pos));        
    });
}

void MainWindow::addNewZaddr(bool sapling) {
    rpc->createNewZaddr(sapling, [=] (json reply) {
        QString addr = QString::fromStdString(reply.get<json::string_t>());
        // Make sure the RPC class reloads the z-addrs for future use
        rpc->refreshAddresses();

        // Just double make sure the z-address is still checked
        if ( sapling && ui->rdioZSAddr->isChecked() ) {
            ui->listReceiveAddresses->insertItem(0, addr); 
            ui->listReceiveAddresses->setCurrentIndex(0);

            ui->statusBar->showMessage(QString::fromStdString("Created new zAddr") %
                                       (sapling ? "(Sapling)" : "(Sprout)"), 
                                       10 * 1000);
        }
    });
}


// Adds sapling or sprout z-addresses to the combo box. Technically, returns a
// lambda, which can be connected to the appropriate signal
std::function<void(bool)> MainWindow::addZAddrsToComboList(bool sapling) {
    return [=] (bool checked) { 
        if (checked) { 
            auto addrs = this->rpc->getModel()->getAllZAddresses();

            // Save the current address, so we can update it later
            auto zaddr = ui->listReceiveAddresses->currentText();
            ui->listReceiveAddresses->clear();

            std::for_each(addrs.begin(), addrs.end(), [=] (auto addr) {
                if ( (sapling &&  Settings::getInstance()->isSaplingAddress(addr)) ||
                    (!sapling && !Settings::getInstance()->isSaplingAddress(addr))) {                        
                        auto bal = rpc->getModel()->getAllBalances().value(addr);
                        ui->listReceiveAddresses->addItem(addr, bal);
                }
            }); 
            
            if (!zaddr.isEmpty() && Settings::isZAddress(zaddr)) {
                ui->listReceiveAddresses->setCurrentText(zaddr);
            }

            // If z-addrs are empty, then create a new one.
            if (addrs.isEmpty()) {
                addNewZaddr(sapling);
            }
        } 
    };
}

void MainWindow::setupReceiveTab() {
    auto addNewTAddr = [=] () {
        rpc->createNewTaddr([=] (json reply) {
            QString addr = QString::fromStdString(reply.get<json::string_t>());
            // Make sure the RPC class reloads the t-addrs for future use
            rpc->refreshAddresses();

            // Just double make sure the t-address is still checked
            if (ui->rdioTAddr->isChecked()) {
                ui->listReceiveAddresses->insertItem(0, addr);
                ui->listReceiveAddresses->setCurrentIndex(0);

                ui->statusBar->showMessage(tr("Created new t-Addr"), 10 * 1000);
            }
        });
    };

    // Connect t-addr radio button
    QObject::connect(ui->rdioTAddr, &QRadioButton::toggled, [=] (bool checked) { 
        // Whenever the t-address is selected, we generate a new address, because we don't
        // want to reuse t-addrs
        if (checked) { 
            updateTAddrCombo(checked);
        } 

        // Toggle the "View all addresses" button as well
        ui->btnViewAllAddresses->setVisible(checked);

        // T addresses don't have view keys
        ui->exportViewKey->setVisible(!checked);
    });

    // View all addresses goes to "View all private keys"
    QObject::connect(ui->btnViewAllAddresses, &QPushButton::clicked, [=] () {
        // If there's no RPC, return
        if (!getRPC())
            return;

        QDialog d(this);
        Ui_ViewAddressesDialog viewaddrs;
        viewaddrs.setupUi(&d);
        Settings::saveRestore(&d);
        Settings::saveRestoreTableHeader(viewaddrs.tblAddresses, &d, "viewalladdressestable");
        viewaddrs.tblAddresses->horizontalHeader()->setStretchLastSection(true);

        ViewAllAddressesModel model(viewaddrs.tblAddresses, getRPC()->getModel()->getAllTAddresses(), getRPC());
        viewaddrs.tblAddresses->setModel(&model);

        QObject::connect(viewaddrs.btnExportAll, &QPushButton::clicked,  this, &MainWindow::exportAllKeys);

        viewaddrs.tblAddresses->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(viewaddrs.tblAddresses, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
            QModelIndex index = viewaddrs.tblAddresses->indexAt(pos);
            if (index.row() < 0) return;

            index = index.sibling(index.row(), 0);
            QString addr = viewaddrs.tblAddresses->model()->data(index).toString();

            QMenu menu(this);
            menu.addAction(tr("Export Private Key"), [=] () {                
                if (addr.isEmpty())
                    return;

                this->exportKeys(addr);
            });
            menu.addAction(tr("Copy Address"), [=]() {
                QGuiApplication::clipboard()->setText(addr);
            });
            menu.exec(viewaddrs.tblAddresses->viewport()->mapToGlobal(pos));
        });

        d.exec();
    });

    QObject::connect(ui->rdioZSAddr, &QRadioButton::toggled, addZAddrsToComboList(true));

    // Explicitly get new address button.
    QObject::connect(ui->btnReceiveNewAddr, &QPushButton::clicked, [=] () {
        if (!rpc->getConnection())
            return;

        if (ui->rdioZSAddr->isChecked()) {
            addNewZaddr(true);
        } else if (ui->rdioTAddr->isChecked()) {
            addNewTAddr();
        }
    });

    // Focus enter for the Receive Tab
    QObject::connect(ui->tabWidget, &QTabWidget::currentChanged, [=] (int tab) {
        if (tab == 2) {
            // Switched to receive tab, select the z-addr radio button
            ui->rdioZSAddr->setChecked(true);
            ui->btnViewAllAddresses->setVisible(false);
            
            // And then select the first one
            ui->listReceiveAddresses->setCurrentIndex(0);
        }
    });

    // Validator for label
    QRegExpValidator* v = new QRegExpValidator(QRegExp(Settings::labelRegExp), ui->rcvLabel);
    ui->rcvLabel->setValidator(v);

    // Select item in address list
    QObject::connect(ui->listReceiveAddresses, 
        QOverload<int>::of(&QComboBox::currentIndexChanged), [=] (int index) {
        QString addr = ui->listReceiveAddresses->itemText(index);
        if (addr.isEmpty()) {
            // Draw empty stuff

            ui->rcvLabel->clear();
            ui->rcvBal->clear();
            ui->txtReceive->clear();
            ui->qrcodeDisplay->clear();
            return;
        }

        auto label = AddressBook::getInstance()->getLabelForAddress(addr);
        if (label.isEmpty()) {
            ui->rcvUpdateLabel->setText("Add Label");
        }
        else {
            ui->rcvUpdateLabel->setText("Update Label");
        }
        
        ui->rcvLabel->setText(label);
        ui->rcvBal->setText(Settings::getZECUSDDisplayFormat(rpc->getModel()->getAllBalances().value(addr)));
        ui->txtReceive->setPlainText(addr);       
        ui->qrcodeDisplay->setQrcodeString(addr);
        if (rpc->getModel()->getUsedAddresses().value(addr, false)) {
            ui->rcvBal->setToolTip(tr("Address has been previously used"));
        } else {
            ui->rcvBal->setToolTip(tr("Address is unused"));
        }
        
    });    

    // Receive tab add/update label
    QObject::connect(ui->rcvUpdateLabel, &QPushButton::clicked, [=]() {
        QString addr = ui->listReceiveAddresses->currentText();
        if (addr.isEmpty())
            return;

        auto curLabel = AddressBook::getInstance()->getLabelForAddress(addr);
        auto label = ui->rcvLabel->text().trimmed();

        if (curLabel == label)  // Nothing to update
            return;

        QString info;

        if (!curLabel.isEmpty() && label.isEmpty()) {
            info = "Removed Label '" % curLabel % "'";
            AddressBook::getInstance()->removeAddressLabel(curLabel, addr);
        }
        else if (!curLabel.isEmpty() && !label.isEmpty()) {
            info = "Updated Label '" % curLabel % "' to '" % label % "'";
            AddressBook::getInstance()->updateLabel(curLabel, addr, label);
        }
        else if (curLabel.isEmpty() && !label.isEmpty()) {
            info = "Added Label '" % label % "'";
            AddressBook::getInstance()->addAddressLabel(label, addr);
        }

        // Update labels everywhere on the UI
        updateLabels();

        // Show the user feedback
        if (!info.isEmpty()) {
            QMessageBox::information(this, "Label", info, QMessageBox::Ok);
        }
    });

    // Receive Export Key
    QObject::connect(ui->exportKey, &QPushButton::clicked, [=]() {
        QString addr = ui->listReceiveAddresses->currentText();
        if (addr.isEmpty())
            return;

        this->exportKeys(addr);
    });

    // Receive tab Export View key
    QObject::connect(ui->exportViewKey, &QPushButton::clicked, [=]() {
        QString addr = ui->listReceiveAddresses->currentText();
        if (addr.isEmpty())
            return;

        this->exportKeys(addr, true);
    });
}

void MainWindow::updateTAddrCombo(bool checked) {
    if (checked) {
        auto utxos = this->rpc->getModel()->getUTXOs();

        // Save the current address so we can restore it later
        auto currentTaddr = ui->listReceiveAddresses->currentText();

        ui->listReceiveAddresses->clear();

        // Maintain a set of addresses so we don't duplicate any, because we'll be adding
        // t addresses multiple times
        QSet<QString> addrs;

        // 1. Add all t addresses that have a balance
        std::for_each(utxos.begin(), utxos.end(), [=, &addrs](auto& utxo) {
            auto addr = utxo.address;
            if (Settings::isTAddress(addr) && !addrs.contains(addr)) {
                auto bal = rpc->getModel()->getAllBalances().value(addr);
                ui->listReceiveAddresses->addItem(addr, bal);

                addrs.insert(addr);
            }
        });
        
        // 2. Add all t addresses that have a label
        auto allTaddrs = this->rpc->getModel()->getAllTAddresses();
        QSet<QString> labels;
        for (auto p : AddressBook::getInstance()->getAllAddressLabels()) {
            labels.insert(p.second);
        }
        std::for_each(allTaddrs.begin(), allTaddrs.end(), [=, &addrs] (auto& taddr) {
            // If the address is in the address book, add it. 
            if (labels.contains(taddr) && !addrs.contains(taddr)) {
                addrs.insert(taddr);
                ui->listReceiveAddresses->addItem(taddr, 0);
            }
        });

        // 3. Add all t-addresses. We won't add more than 20 total t-addresses,
        // since it will overwhelm the dropdown
        for (int i=0; addrs.size() < 20 && i < allTaddrs.size(); i++) {
            auto addr = allTaddrs.at(i);
            if (!addrs.contains(addr))  {
                addrs.insert(addr);
                // Balance is zero since it has not been previously added
                ui->listReceiveAddresses->addItem(addr, 0);
            }
        }

        // 4. Add the previously selected t-address
        if (!currentTaddr.isEmpty() && Settings::isTAddress(currentTaddr)) {
            // Make sure the current taddr is in the list
            if (!addrs.contains(currentTaddr)) {
                auto bal = rpc->getModel()->getAllBalances().value(currentTaddr);
                ui->listReceiveAddresses->addItem(currentTaddr, bal);
            }
            ui->listReceiveAddresses->setCurrentText(currentTaddr);
        }

        // 5. Add a last, disabled item if there are remaining items
        if (allTaddrs.size() > addrs.size()) {
            auto num = QString::number(allTaddrs.size() - addrs.size());
            ui->listReceiveAddresses->addItem("-- " + num + " more --", 0);

            QStandardItemModel* model = qobject_cast<QStandardItemModel*>(ui->listReceiveAddresses->model());
            QStandardItem* item =  model->findItems("--", Qt::MatchStartsWith)[0];
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
};

// Updates the labels everywhere on the UI. Call this after the labels have been updated
void MainWindow::updateLabels() {
    // Update the Receive tab
    if (ui->rdioTAddr->isChecked()) {
        updateTAddrCombo(true);
    }
    else {
        addZAddrsToComboList(ui->rdioZSAddr->isChecked())(true);
    }

    // Update the Send Tab
    updateFromCombo();

    // Update the autocomplete
    updateLabelsAutoComplete();
}

void MainWindow::slot_change_theme(const QString& theme_name)
{
    Settings::getInstance()->set_theme_name(theme_name);

    // Include css
    QString saved_theme_name;
    try
    {
       saved_theme_name = Settings::getInstance()->get_theme_name();
    }
    catch (...)
    {
        saved_theme_name = "default";
    }

    QFile qFile(":/css/res/css/" + saved_theme_name +".css");
    if (qFile.open(QFile::ReadOnly))
    {
      QString styleSheet = QLatin1String(qFile.readAll());
      this->setStyleSheet(""); // reset styles    
      this->setStyleSheet(styleSheet);
    }

}

MainWindow::~MainWindow()
{
    delete ui;
    delete rpc;
    delete labelCompleter;

    delete sendTxRecurringInfo;
    delete amtValidator;
    delete feesValidator;

    delete loadingMovie;
    delete logger;

    delete wsserver;
    delete wormhole;
}
