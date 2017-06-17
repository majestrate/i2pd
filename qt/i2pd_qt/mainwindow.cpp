#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include "RouterContext.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"

#ifndef ANDROID
# include <QtDebug>
#endif

#include <QScrollBar>

#include <fstream>

std::string programOptionsWriterCurrentSection;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent)
#ifndef ANDROID
    ,quitting(false)
#endif
    ,ui(new Ui::MainWindow)
    ,configItems()
    ,datadir()
    ,confpath()
    ,tunconfpath()
    ,tunnelsPageUpdateListener(this)

{
    ui->setupUi(this);
    setWindowTitle(QApplication::translate("AppTitle","I2PD"));

    //TODO handle resizes and change the below into resize() call
    setFixedSize(width(), 480);
    ui->centralWidget->setMinimumHeight(480);
    ui->centralWidget->setMaximumHeight(480);
    onResize();

    ui->stackedWidget->setCurrentIndex(0);
    ui->settingsScrollArea->resize(ui->settingsContentsGridLayout->sizeHint().width()+10,380);
    QScrollBar* const barSett = ui->settingsScrollArea->verticalScrollBar();
    //QSize szSettContents = ui->settingsContentsGridLayout->minimumSize();
    int w = 683;
    int h = 3060;
    ui->settingsContents->setFixedSize(w, h);
    //ui->settingsContents->resize(w, h);
    //ui->settingsContents->adjustSize();

    /*
    QPalette pal(palette());
    pal.setColor(QPalette::Background, Qt::red);
    ui->settingsContents->setAutoFillBackground(true);
    ui->settingsContents->setPalette(pal);
    */

    //ui->settingsScrollArea->adjustSize();
    /*ui->tunnelsScrollAreaWidgetContents->setFixedSize(
                ui->tunnelsScrollArea->width() - barSett->width(), 0);*/

#ifndef ANDROID
    createActions();
    createTrayIcon();
#endif

    QObject::connect(ui->statusPagePushButton, SIGNAL(released()), this, SLOT(showStatusPage()));
    QObject::connect(ui->settingsPagePushButton, SIGNAL(released()), this, SLOT(showSettingsPage()));

    QObject::connect(ui->tunnelsPagePushButton, SIGNAL(released()), this, SLOT(showTunnelsPage()));
    QObject::connect(ui->restartPagePushButton, SIGNAL(released()), this, SLOT(showRestartPage()));
    QObject::connect(ui->quitPagePushButton, SIGNAL(released()), this, SLOT(showQuitPage()));

    QObject::connect(ui->fastQuitPushButton, SIGNAL(released()), this, SLOT(handleQuitButton()));
    QObject::connect(ui->gracefulQuitPushButton, SIGNAL(released()), this, SLOT(handleGracefulQuitButton()));

#   define OPTION(section,option,defaultValueGetter) ConfigOption(QString(section),QString(option))

    initFileChooser(    OPTION("","conf",[](){return "";}), ui->configFileLineEdit, ui->configFileBrowsePushButton);
    initFolderChooser(  OPTION("","datadir",[]{return "";}), ui->dataFolderLineEdit, ui->dataFolderBrowsePushButton);
    initFileChooser(    OPTION("","tunconf",[](){return "";}), ui->tunnelsConfigFileLineEdit, ui->tunnelsConfigFileBrowsePushButton);

    initFileChooser(    OPTION("","pidfile",[]{return "";}), ui->pidFileLineEdit, ui->pidFileBrowsePushButton);
    logOption=initNonGUIOption(   OPTION("","log",[]{return "";}));
    daemonOption=initNonGUIOption(   OPTION("","daemon",[]{return "";}));
    serviceOption=initNonGUIOption(   OPTION("","service",[]{return "";}));

    logFileNameOption=initFileChooser(    OPTION("","logfile",[]{return "";}), ui->logFileLineEdit, ui->logFileBrowsePushButton);
    initLogLevelCombobox(OPTION("","loglevel",[]{return "";}), ui->logLevelComboBox);

    initIPAddressBox(   OPTION("","host",[]{return "";}), ui->routerExternalHostLineEdit, tr("Router external address -> Host"));
    initTCPPortBox(     OPTION("","port",[]{return "";}), ui->routerExternalPortLineEdit, tr("Router external address -> Port"));

    initCheckBox(       OPTION("","ipv6",[]{return "false";}), ui->ipv6CheckBox);
    initCheckBox(       OPTION("","notransit",[]{return "false";}), ui->notransitCheckBox);
    initCheckBox(       OPTION("","floodfill",[]{return "false";}), ui->floodfillCheckBox);
    initStringBox(      OPTION("","bandwidth",[]{return "";}), ui->bandwidthLineEdit);
    initStringBox(      OPTION("","family",[]{return "";}), ui->familyLineEdit);
    initIntegerBox(     OPTION("","netid",[]{return "2";}), ui->netIdLineEdit, tr("NetID"));

#ifdef Q_OS_WIN
    initCheckBox(       OPTION("","insomnia",[]{return "";}), ui->insomniaCheckBox);
    initNonGUIOption(   OPTION("","svcctl",[]{return "";}));
    initNonGUIOption(   OPTION("","close",[]{return "";}));
#else
    ui->insomniaCheckBox->setEnabled(false);
#endif

    initCheckBox(       OPTION("http","enabled",[]{return "true";}), ui->webconsoleEnabledCheckBox);
    initIPAddressBox(   OPTION("http","address",[]{return "";}), ui->webconsoleAddrLineEdit, tr("HTTP webconsole -> IP address"));
    initTCPPortBox(     OPTION("http","port",[]{return "7070";}), ui->webconsolePortLineEdit, tr("HTTP webconsole -> Port"));
    initCheckBox(       OPTION("http","auth",[]{return "";}), ui->webconsoleBasicAuthCheckBox);
    initStringBox(      OPTION("http","user",[]{return "i2pd";}), ui->webconsoleUserNameLineEditBasicAuth);
    initStringBox(      OPTION("http","pass",[]{return "";}), ui->webconsolePasswordLineEditBasicAuth);

    initCheckBox(       OPTION("httpproxy","enabled",[]{return "";}), ui->httpProxyEnabledCheckBox);
    initIPAddressBox(   OPTION("httpproxy","address",[]{return "";}), ui->httpProxyAddressLineEdit, tr("HTTP proxy -> IP address"));
    initTCPPortBox(     OPTION("httpproxy","port",[]{return "4444";}), ui->httpProxyPortLineEdit, tr("HTTP proxy -> Port"));
    initFileChooser(    OPTION("httpproxy","keys",[]{return "";}), ui->httpProxyKeyFileLineEdit, ui->httpProxyKeyFilePushButton);
    initSignatureTypeCombobox(OPTION("httpproxy","signaturetype",[]{return "7";}), ui->comboBox_httpPorxySignatureType);
    initStringBox(     OPTION("httpproxy","inbound.length",[]{return "3";}), ui->httpProxyInboundTunnelsLenLineEdit);
    initStringBox(     OPTION("httpproxy","inbound.quantity",[]{return "5";}), ui->httpProxyInboundTunnQuantityLineEdit);
    initStringBox(     OPTION("httpproxy","outbound.length",[]{return "3";}), ui->httpProxyOutBoundTunnLenLineEdit);
    initStringBox(     OPTION("httpproxy","outbound.quantity",[]{return "5";}), ui->httpProxyOutboundTunnQuantityLineEdit);

    initCheckBox(       OPTION("socksproxy","enabled",[]{return "";}), ui->socksProxyEnabledCheckBox);
    initIPAddressBox(   OPTION("socksproxy","address",[]{return "";}), ui->socksProxyAddressLineEdit, tr("Socks proxy -> IP address"));
    initTCPPortBox(     OPTION("socksproxy","port",[]{return "4447";}), ui->socksProxyPortLineEdit, tr("Socks proxy -> Port"));
    initFileChooser(    OPTION("socksproxy","keys",[]{return "";}), ui->socksProxyKeyFileLineEdit, ui->socksProxyKeyFilePushButton);
    initSignatureTypeCombobox(OPTION("socksproxy","signaturetype",[]{return "7";}), ui->comboBox_socksProxySignatureType);
    initStringBox(     OPTION("socksproxy","inbound.length",[]{return "";}), ui->socksProxyInboundTunnelsLenLineEdit);
    initStringBox(     OPTION("socksproxy","inbound.quantity",[]{return "";}), ui->socksProxyInboundTunnQuantityLineEdit);
    initStringBox(     OPTION("socksproxy","outbound.length",[]{return "";}), ui->socksProxyOutBoundTunnLenLineEdit);
    initStringBox(     OPTION("socksproxy","outbound.quantity",[]{return "";}), ui->socksProxyOutboundTunnQuantityLineEdit);
    initIPAddressBox(   OPTION("socksproxy","outproxy",[]{return "";}), ui->outproxyAddressLineEdit, tr("Socks proxy -> Outproxy address"));
    initTCPPortBox(     OPTION("socksproxy","outproxyport",[]{return "";}), ui->outproxyPortLineEdit, tr("Socks proxy -> Outproxy port"));

    initCheckBox(       OPTION("sam","enabled",[]{return "false";}), ui->samEnabledCheckBox);
    initIPAddressBox(   OPTION("sam","address",[]{return "";}), ui->samAddressLineEdit, tr("SAM -> IP address"));
    initTCPPortBox(     OPTION("sam","port",[]{return "7656";}), ui->samPortLineEdit, tr("SAM -> Port"));

    initCheckBox(       OPTION("bob","enabled",[]{return "false";}), ui->bobEnabledCheckBox);
    initIPAddressBox(   OPTION("bob","address",[]{return "";}), ui->bobAddressLineEdit, tr("BOB -> IP address"));
    initTCPPortBox(     OPTION("bob","port",[]{return "2827";}), ui->bobPortLineEdit, tr("BOB -> Port"));

    initCheckBox(       OPTION("i2cp","enabled",[]{return "false";}), ui->i2cpEnabledCheckBox);
    initIPAddressBox(   OPTION("i2cp","address",[]{return "";}), ui->i2cpAddressLineEdit, tr("I2CP -> IP address"));
    initTCPPortBox(     OPTION("i2cp","port",[]{return "7654";}), ui->i2cpPortLineEdit, tr("I2CP -> Port"));

    initCheckBox(       OPTION("i2pcontrol","enabled",[]{return "false";}), ui->i2pControlEnabledCheckBox);
    initIPAddressBox(   OPTION("i2pcontrol","address",[]{return "";}), ui->i2pControlAddressLineEdit, tr("I2PControl -> IP address"));
    initTCPPortBox(     OPTION("i2pcontrol","port",[]{return "7650";}), ui->i2pControlPortLineEdit, tr("I2PControl -> Port"));
    initStringBox(      OPTION("i2pcontrol","password",[]{return "";}), ui->i2pControlPasswordLineEdit);
    initFileChooser(    OPTION("i2pcontrol","cert",[]{return "i2pcontrol.crt.pem";}), ui->i2pControlCertFileLineEdit, ui->i2pControlCertFileBrowsePushButton);
    initFileChooser(    OPTION("i2pcontrol","key",[]{return "i2pcontrol.key.pem";}), ui->i2pControlKeyFileLineEdit, ui->i2pControlKeyFileBrowsePushButton);

    initCheckBox(       OPTION("upnp","enabled",[]{return "true";}), ui->enableUPnPCheckBox);
    initStringBox(      OPTION("upnp","name",[]{return "I2Pd";}), ui->upnpNameLineEdit);
	
    initCheckBox(       OPTION("precomputation","elgamal",[]{return "false";}), ui->useElGamalPrecomputedTablesCheckBox);
	
    initCheckBox(       OPTION("reseed","verify",[]{return "";}), ui->reseedVerifyCheckBox);
    initFileChooser(    OPTION("reseed","file",[]{return "";}), ui->reseedFileLineEdit, ui->reseedFileBrowsePushButton);
    initStringBox(      OPTION("reseed","urls",[]{return "";}), ui->reseedURLsLineEdit);
	
    initStringBox(      OPTION("addressbook","defaulturl",[]{return "";}), ui->addressbookDefaultURLLineEdit);
    initStringBox(      OPTION("addressbook","subscriptions",[]{return "";}), ui->addressbookSubscriptionsURLslineEdit);
	
    initUInt16Box(     OPTION("limits","transittunnels",[]{return "2500";}), ui->maxNumOfTransitTunnelsLineEdit, tr("maxNumberOfTransitTunnels"));
    initUInt16Box(     OPTION("limits","openfiles",[]{return "0";}), ui->maxNumOfOpenFilesLineEdit, tr("maxNumberOfOpenFiles"));
    initUInt32Box(     OPTION("limits","coresize",[]{return "0";}), ui->coreFileMaxSizeNumberLineEdit, tr("coreFileMaxSize"));

    initCheckBox(       OPTION("trust","enabled",[]{return "false";}), ui->checkBoxTrustEnable);
    initStringBox(      OPTION("trust","family",[]{return "";}), ui->lineEditTrustFamily);
    initStringBox(      OPTION("trust","routers",[]{return "";}), ui->lineEditTrustRouters);
    initCheckBox(       OPTION("trust","hidden",[]{return "false";}), ui->checkBoxTrustHidden);

    initCheckBox(       OPTION("websockets","enabled",[]{return "false";}), ui->checkBoxWebsocketsEnable);
    initIPAddressBox(   OPTION("websockets","address",[]{return "127.0.0.1";}), ui->lineEdit_webSock_addr, tr("Websocket server -> IP address"));
    initTCPPortBox(     OPTION("websockets","port",[]{return "7666";}), ui->lineEdit_webSock_port, tr("Websocket server -> Port"));

#   undef OPTION

    loadAllConfigs();

    //tunnelsFormGridLayoutWidget = new QWidget(ui->tunnelsScrollAreaWidgetContents);
    //tunnelsFormGridLayoutWidget->setObjectName(QStringLiteral("tunnelsFormGridLayoutWidget"));
    //tunnelsFormGridLayoutWidget->setGeometry(QRect(0, 0, 621, 451));
    ui->tunnelsScrollAreaWidgetContents->setGeometry(QRect(0, 0, 621, 451));

    appendTunnelForms("");

    ui->configFileLineEdit->setEnabled(false);
    ui->configFileBrowsePushButton->setEnabled(false);
    ui->configFileLineEdit->setText(confpath);
    ui->tunnelsConfigFileLineEdit->setText(tunconfpath);

    for(QList<MainWindowItem*>::iterator it = configItems.begin(); it!= configItems.end(); ++it) {
        MainWindowItem* item = *it;
        item->installListeners(this);
    }

    QObject::connect(ui->tunnelsConfigFileLineEdit, SIGNAL(textChanged(const QString &)),
                     this, SLOT(reloadTunnelsConfigAndUI()));

    QObject::connect(ui->addServerTunnelPushButton, SIGNAL(released()), this, SLOT(addServerTunnelPushButtonReleased()));
    QObject::connect(ui->addClientTunnelPushButton, SIGNAL(released()), this, SLOT(addClientTunnelPushButtonReleased()));


#ifndef ANDROID
    QObject::connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                this, SLOT(iconActivated(QSystemTrayIcon::ActivationReason)));

    setIcon();
    trayIcon->show();
#endif

    //QMetaObject::connectSlotsByName(this);
}

void MainWindow::showStatusPage(){ui->stackedWidget->setCurrentIndex(0);}
void MainWindow::showSettingsPage(){ui->stackedWidget->setCurrentIndex(1);}
void MainWindow::showTunnelsPage(){ui->stackedWidget->setCurrentIndex(2);}
void MainWindow::showRestartPage(){ui->stackedWidget->setCurrentIndex(3);}
void MainWindow::showQuitPage(){ui->stackedWidget->setCurrentIndex(4);}

//TODO
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    onResize();
}

//TODO
void MainWindow::onResize()
{
    if(isVisible()){
        ui->horizontalLayoutWidget->resize(ui->horizontalLayoutWidget->width(), height());

        //status
        ui->statusPage->resize(ui->statusPage->width(), height());

        //tunnels
        ui->tunnelsPage->resize(ui->tunnelsPage->width(), height());
        ui->verticalLayoutWidget_6->resize(ui->verticalLayoutWidget_6->width(), height()-20);
        /*ui->tunnelsScrollArea->resize(ui->tunnelsScrollArea->width(),
                                      ui->verticalLayoutWidget_6->height()-ui->label_5->height());*/
    }
}

#ifndef ANDROID
void MainWindow::createActions() {
    toggleWindowVisibleAction = new QAction(tr("&Toggle the window"), this);
    connect(toggleWindowVisibleAction, SIGNAL(triggered()), this, SLOT(toggleVisibilitySlot()));

    //quitAction = new QAction(tr("&Quit"), this);
    //connect(quitAction, SIGNAL(triggered()), QApplication::instance(), SLOT(quit()));
}

void MainWindow::toggleVisibilitySlot() {
    setVisible(!isVisible());
}

void MainWindow::createTrayIcon() {
    trayIconMenu = new QMenu(this);
    trayIconMenu->addAction(toggleWindowVisibleAction);
    //trayIconMenu->addSeparator();
    //trayIconMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayIconMenu);
}

void MainWindow::setIcon() {
    QIcon icon(":/images/icon.png");
    trayIcon->setIcon(icon);
    setWindowIcon(icon);

    trayIcon->setToolTip(QApplication::translate("MainWindow", "i2pd", 0));
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason) {
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
    case QSystemTrayIcon::MiddleClick:
        setVisible(!isVisible());
        break;
    default:
        qDebug() << "MainWindow::iconActivated(): unknown reason: " << reason << endl;
        break;
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if(quitting){ QMainWindow::closeEvent(event); return; }
    if (trayIcon->isVisible()) {
        QMessageBox::information(this, tr("i2pd"),
                                 tr("The program will keep running in the "
                                    "system tray. To gracefully terminate the program, "
                                    "choose <b>Graceful Quit</b> at the main i2pd window."));
        hide();
        event->ignore();
    }
}
#endif

void MainWindow::handleQuitButton() {
    qDebug("Quit pressed. Hiding the main window");
#ifndef ANDROID
    quitting=true;
#endif
    close();
    QApplication::instance()->quit();
}

void MainWindow::handleGracefulQuitButton() {
    qDebug("Graceful Quit pressed.");
    ui->gracefulQuitPushButton->setText(QApplication::translate("MainWindow", "Graceful quit is in progress", 0));
    ui->gracefulQuitPushButton->setEnabled(false);
    ui->gracefulQuitPushButton->adjustSize();
    ui->quitPage->adjustSize();
    i2p::context.SetAcceptsTunnels (false); // stop accpting tunnels
    QTimer::singleShot(10*60*1000//millis
        , this, SLOT(handleGracefulQuitTimerEvent()));
}

void MainWindow::handleGracefulQuitTimerEvent() {
    qDebug("Hiding the main window");
#ifndef ANDROID
    quitting=true;
#endif
    close();
    qDebug("Performing quit");
    QApplication::instance()->quit();
}

MainWindow::~MainWindow()
{
    qDebug("Destroying main window");
    for(QList<MainWindowItem*>::iterator it = configItems.begin(); it!= configItems.end(); ++it) {
        MainWindowItem* item = *it;
        item->deleteLater();
    }
    configItems.clear();
    //QMessageBox::information(0, "Debug", "mw destructor 1");
    //delete ui;
    //QMessageBox::information(0, "Debug", "mw destructor 2");
}

FileChooserItem* MainWindow::initFileChooser(ConfigOption option, QLineEdit* fileNameLineEdit, QPushButton* fileBrowsePushButton){
    FileChooserItem* retVal;
    retVal=new FileChooserItem(option, fileNameLineEdit, fileBrowsePushButton);
    MainWindowItem* super=retVal;
    configItems.append(super);
    return retVal;
}
void MainWindow::initFolderChooser(ConfigOption option, QLineEdit* folderLineEdit, QPushButton* folderBrowsePushButton){
    configItems.append(new FolderChooserItem(option, folderLineEdit, folderBrowsePushButton));
}
/*void MainWindow::initCombobox(ConfigOption option, QComboBox* comboBox){
    configItems.append(new ComboBoxItem(option, comboBox));
    QObject::connect(comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(saveAllConfigs()));
}*/
void MainWindow::initLogLevelCombobox(ConfigOption option, QComboBox* comboBox){
    configItems.append(new LogLevelComboBoxItem(option, comboBox));
}
void MainWindow::initSignatureTypeCombobox(ConfigOption option, QComboBox* comboBox){
    configItems.append(new SignatureTypeComboBoxItem(option, comboBox));
}
void MainWindow::initIPAddressBox(ConfigOption option, QLineEdit* addressLineEdit, QString fieldNameTranslated){
    configItems.append(new IPAddressStringItem(option, addressLineEdit, fieldNameTranslated));
}
void MainWindow::initTCPPortBox(ConfigOption option, QLineEdit* portLineEdit, QString fieldNameTranslated){
    configItems.append(new TCPPortStringItem(option, portLineEdit, fieldNameTranslated));
}
void MainWindow::initCheckBox(ConfigOption option, QCheckBox* checkBox) {
    configItems.append(new CheckBoxItem(option, checkBox));
}
void MainWindow::initIntegerBox(ConfigOption option, QLineEdit* numberLineEdit, QString fieldNameTranslated){
    configItems.append(new IntegerStringItem(option, numberLineEdit, fieldNameTranslated));
}
void MainWindow::initUInt32Box(ConfigOption option, QLineEdit* numberLineEdit, QString fieldNameTranslated){
    configItems.append(new UInt32StringItem(option, numberLineEdit, fieldNameTranslated));
}
void MainWindow::initUInt16Box(ConfigOption option, QLineEdit* numberLineEdit, QString fieldNameTranslated){
    configItems.append(new UInt16StringItem(option, numberLineEdit, fieldNameTranslated));
}
void MainWindow::initStringBox(ConfigOption option, QLineEdit* lineEdit){
    configItems.append(new BaseStringItem(option, lineEdit));
}
NonGUIOptionItem* MainWindow::initNonGUIOption(ConfigOption option) {
    NonGUIOptionItem * retValue;
    configItems.append(retValue=new NonGUIOptionItem(option));
    return retValue;
}

void MainWindow::loadAllConfigs(){

    //BORROWED FROM ??? //TODO move this code into single location
    std::string config;  i2p::config::GetOption("conf",    config);
    std::string datadir; i2p::config::GetOption("datadir", datadir);
    bool service = false;
#ifndef _WIN32
    i2p::config::GetOption("service", service);
#endif
    i2p::fs::DetectDataDir(datadir, service);
    i2p::fs::Init();

    datadir = i2p::fs::GetDataDir();
    // TODO: drop old name detection in v2.8.0
    if (config == "")
    {
        config = i2p::fs::DataDirPath("i2p.conf");
        if (i2p::fs::Exists (config)) {
            LogPrint(eLogWarning, "Daemon: please rename i2p.conf to i2pd.conf here: ", config);
        } else {
            config = i2p::fs::DataDirPath("i2pd.conf");
            if (!i2p::fs::Exists (config)) {
                // use i2pd.conf only if exists
                config = ""; /* reset */
            }
        }
    }

    //BORROWED FROM ClientContext.cpp //TODO move this code into single location
    std::string tunConf; i2p::config::GetOption("tunconf", tunConf);
    if (tunConf == "") {
        // TODO: cleanup this in 2.8.0
        tunConf = i2p::fs::DataDirPath ("tunnels.cfg");
        if (i2p::fs::Exists(tunConf)) {
            LogPrint(eLogWarning, "FS: please rename tunnels.cfg -> tunnels.conf here: ", tunConf);
        } else {
            tunConf = i2p::fs::DataDirPath ("tunnels.conf");
        }
    }

    this->confpath = config.c_str();
    this->datadir = datadir.c_str();
    this->tunconfpath = tunConf.c_str();

    for(QList<MainWindowItem*>::iterator it = configItems.begin(); it!= configItems.end(); ++it) {
        MainWindowItem* item = *it;
        item->loadFromConfigOption();
    }

    ReadTunnelsConfig();
}
/** returns false iff not valid items present and save was aborted */
bool MainWindow::saveAllConfigs(){
    programOptionsWriterCurrentSection="";
    if(!logFileNameOption->lineEdit->text().trimmed().isEmpty())logOption->optionValue=boost::any(std::string("file"));
    else logOption->optionValue=boost::any(std::string("stdout"));
    daemonOption->optionValue=boost::any(false);
    serviceOption->optionValue=boost::any(false);

    std::stringstream out;
    for(QList<MainWindowItem*>::iterator it = configItems.begin(); it!= configItems.end(); ++it) {
        MainWindowItem* item = *it;
        if(!item->isValid()) return false;
    }

    for(QList<MainWindowItem*>::iterator it = configItems.begin(); it!= configItems.end(); ++it) {
        MainWindowItem* item = *it;
        item->saveToStringStream(out);
    }

    using namespace std;

    QString backup=confpath+"~";
    if(QFile::exists(backup)) QFile::remove(backup);//TODO handle errors
    QFile::rename(confpath, backup);//TODO handle errors
    ofstream outfile;
    outfile.open(confpath.toStdString());//TODO handle errors
    outfile << out.str().c_str();
    outfile.close();

    SaveTunnelsConfig();

    return true;
}

void FileChooserItem::pushButtonReleased() {
    QString fileName = lineEdit->text().trimmed();
    fileName = QFileDialog::getOpenFileName(nullptr, tr("Open File"), fileName, tr("All Files (*.*)"));
    if(fileName.length()>0)lineEdit->setText(fileName);
}
void FolderChooserItem::pushButtonReleased() {
    QString fileName = lineEdit->text().trimmed();
    fileName = QFileDialog::getExistingDirectory(nullptr, tr("Open Folder"), fileName);
    if(fileName.length()>0)lineEdit->setText(fileName);
}

void BaseStringItem::installListeners(MainWindow *mainWindow) {
    QObject::connect(lineEdit, SIGNAL(textChanged(const QString &)), mainWindow, SLOT(saveAllConfigs()));
}
void ComboBoxItem::installListeners(MainWindow *mainWindow) {
    QObject::connect(comboBox, SIGNAL(currentIndexChanged(int)), mainWindow, SLOT(saveAllConfigs()));
}
void CheckBoxItem::installListeners(MainWindow *mainWindow) {
    QObject::connect(checkBox, SIGNAL(stateChanged(int)), mainWindow, SLOT(saveAllConfigs()));
}


void MainWindowItem::installListeners(MainWindow *mainWindow) {}

void MainWindow::appendTunnelForms(std::string tunnelNameToFocus) {
    int height=0;
    ui->tunnelsScrollAreaWidgetContents->setGeometry(0,0,0,0);
    for(std::map<std::string, TunnelConfig*>::iterator it = tunnelConfigs.begin(); it != tunnelConfigs.end(); ++it) {
        const std::string& name=it->first;
        TunnelConfig* tunconf = it->second;
        ServerTunnelConfig* stc = tunconf->asServerTunnelConfig();
        if(stc){
            ServerTunnelPane * tunnelPane=new ServerTunnelPane(&tunnelsPageUpdateListener, stc);
            int h=tunnelPane->appendServerTunnelForm(stc, ui->tunnelsScrollAreaWidgetContents, tunnelPanes.size(), height);
            height+=h;
            qDebug() << "tun.height:" << height << "sz:" <<  tunnelPanes.size();
            tunnelPanes.push_back(tunnelPane);
            if(name==tunnelNameToFocus)tunnelPane->getNameLineEdit()->setFocus();
            continue;
        }
        ClientTunnelConfig* ctc = tunconf->asClientTunnelConfig();
        if(ctc){
            ClientTunnelPane * tunnelPane=new ClientTunnelPane(&tunnelsPageUpdateListener, ctc);
            int h=tunnelPane->appendClientTunnelForm(ctc, ui->tunnelsScrollAreaWidgetContents, tunnelPanes.size(), height);
            height+=h;
            qDebug() << "tun.height:" << height << "sz:" <<  tunnelPanes.size();
            tunnelPanes.push_back(tunnelPane);
            if(name==tunnelNameToFocus)tunnelPane->getNameLineEdit()->setFocus();
            continue;
        }
        throw "unknown TunnelConfig subtype";
    }
    qDebug() << "tun.setting height:" << height;
    ui->tunnelsScrollAreaWidgetContents->setGeometry(QRect(0, 0, 621, height));
    QList<QWidget*> childWidgets = ui->tunnelsScrollAreaWidgetContents->findChildren<QWidget*>();
    foreach(QWidget* widget, childWidgets)
        widget->show();
}
void MainWindow::deleteTunnelForms() {
    for(std::list<TunnelPane*>::iterator it = tunnelPanes.begin(); it != tunnelPanes.end(); ++it) {
        TunnelPane* tp = *it;
        ServerTunnelPane* stp = tp->asServerTunnelPane();
        if(stp){
            stp->deleteServerTunnelForm();
            delete stp;
            continue;
        }
        ClientTunnelPane* ctp = tp->asClientTunnelPane();
        if(ctp){
            ctp->deleteClientTunnelForm();
            delete ctp;
            continue;
        }
        throw "unknown TunnelPane subtype";
    }
    tunnelPanes.clear();
}

void MainWindow::reloadTunnelsConfigAndUI(std::string tunnelNameToFocus) {
    deleteTunnelForms();
    for (std::map<std::string,TunnelConfig*>::iterator it=tunnelConfigs.begin(); it!=tunnelConfigs.end(); ++it) {
        TunnelConfig* tunconf = it->second;
        delete tunconf;
    }
    tunnelConfigs.clear();
    ReadTunnelsConfig();
    appendTunnelForms(tunnelNameToFocus);
}

void MainWindow::SaveTunnelsConfig() {
    std::stringstream out;

    for (std::map<std::string,TunnelConfig*>::iterator it=tunnelConfigs.begin(); it!=tunnelConfigs.end(); ++it) {
        const std::string& name = it->first;
        TunnelConfig* tunconf = it->second;
        tunconf->saveHeaderToStringStream(out);
        tunconf->saveToStringStream(out);
        tunconf->saveI2CPParametersToStringStream(out);
    }

    using namespace std;

    QString backup=tunconfpath+"~";
    if(QFile::exists(backup)) QFile::remove(backup);//TODO handle errors
    QFile::rename(tunconfpath, backup);//TODO handle errors
    ofstream outfile;
    outfile.open(tunconfpath.toStdString());//TODO handle errors
    outfile << out.str().c_str();
    outfile.close();

}

void MainWindow::TunnelsPageUpdateListenerMainWindowImpl::updated(std::string oldName, TunnelConfig* tunConf) {
    if(oldName!=tunConf->getName()) {
        //name has changed
        std::map<std::string,TunnelConfig*>::const_iterator it=mainWindow->tunnelConfigs.find(oldName);
        if(it!=mainWindow->tunnelConfigs.end())mainWindow->tunnelConfigs.erase(it);
        mainWindow->tunnelConfigs[tunConf->getName()]=tunConf;
    }
    mainWindow->SaveTunnelsConfig();
}

void MainWindow::TunnelsPageUpdateListenerMainWindowImpl::needsDeleting(std::string oldName){
    mainWindow->DeleteTunnelNamed(oldName);
}

void MainWindow::addServerTunnelPushButtonReleased() {
    CreateDefaultServerTunnel();
}

void MainWindow::addClientTunnelPushButtonReleased() {
    CreateDefaultClientTunnel();
}
