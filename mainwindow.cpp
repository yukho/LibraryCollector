#include "mainwindow.h"
#include "ui_mainwindow.h"

//------------------------------------------------------
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QCursor>
#include <QRegularExpression>

#include <QThread>  //  QThread::usleep();
#include <QClipboard>

#include <QDebug>

#include "dependencies.h"
#include "file_functions.h"
//------------------------------------------------------
MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  processAlreadyStarted_(false)
{
  ui->setupUi(this);

  ui->splitter->setStretchFactor(0,1);
  ui->splitter->setStretchFactor(1,0);
  ui->splitter->setSizes(QList<int>()<<20<<50);

  /* QProcess */
  process_= new QProcess(this);
  connect( process_, &QProcess::errorOccurred,
           this,     &MainWindow::processErrorOccurred);

  connect( process_, &QProcess::started,
           this,     &MainWindow::processStarted);

  connect( process_, &QProcess::finished,
           this,     &MainWindow::processFinished);

  // Special button
  connect(ui->toolButtonAim, &ToolButton::leftMouseRelease,
          this, &MainWindow::on_AimRelease);

  // JS Wrappers
  log_= new Log(ui->textEditLog);
  tree_= new Tree(ui->treeWidget);
  utils_ = new Utils(this);

  // availible js-files
  QDir jsDir("./js");
  QFileInfoList files =
      jsDir.entryInfoList(QStringList()<<"*.js", QDir::Files);
  for(int i=0; i<files.size(); ++i)
     ui->comboBoxScript->addItem(files[i].fileName(),
                                 files[i].absoluteFilePath());

  // Settings
  loadSettings();
}
//-----------------------------------------------------------------------------
MainWindow::~MainWindow()
{
  delete ui;
}
//-----------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{    
  saveSettings();

  if(processAlreadyStarted_)
    process_->terminate();

  QMainWindow::closeEvent(event);
}
//------------------------------------------------------------------------------
void MainWindow::on_pushButtonRun_clicked()
{
  const QString qtDir = ui->lineEditQtDir->text();

  QProcessEnvironment enviroment =
      QProcessEnvironment::systemEnvironment();

  QChar valuesSeparator= ';';
#ifdef Q_OS_WIN
  systemRoot_ = enviroment.value("SYSTEMROOT");
  valuesSeparator= ';';
#elif defined Q_OS_LINUX
  valuesSeparator= ':';
#endif

  if(ui->checkBoxAddBin->isChecked())
  {
     const QString pathValues= enviroment.value("PATH");
     const QString qtBinDir = QDir::toNativeSeparators(qtDir+"/bin");
     const QString newPathValues = pathValues+valuesSeparator+ qtBinDir;
     enviroment.insert("PATH",newPathValues);
     // enviroment.insert("QTDIR",QDir::toNativeSeparators(qtDir));
     // Linux  LD_LIBRARY_PATH ???
  }

  if(ui->checkBoxAddPlugins->isChecked())
  {
     const QString qtPluginsDir =  QDir::toNativeSeparators(qtDir+"/plugins");
     enviroment.insert("QT_PLUGIN_PATH", qtPluginsDir);
  }

  process_->setEnvironment(enviroment.toStringList());
  process_->start("\""+ui->lineEditFilePath->text()+"\"");
}
//------------------------------------------------------
// BUTTONs
//------------------------------------------------------
void MainWindow::on_pushButtonUpdate_clicked()
{
  /* find Libraries  */
  QString error;
  LibraryCollector::PidType pid = startedProcessId();
  QStringList libs= LibraryCollector::librariesByPid(pid,error);
  if(!error.isEmpty())
  {
    log_->addError(error);
    return;
  }
  log_->addInfo(QString(tr("Found %1 libs.")).arg(libs.size()) );


  /* call update in JS */

  if(!jsEngine_)
  {
    log_->addError(tr("Js script is not valid!"));
    return;
  }

  jsEngine_->globalObject().setProperty("QTDIR",ui->lineEditQtDir->text().trimmed());
  jsEngine_->globalObject().setProperty("FILEPATH",ui->lineEditFilePath->text().trimmed());

  QJSValue jsUpdateFunction = jsEngine_->globalObject().property("update");
  QJSValue result =
      jsUpdateFunction.call(QJSValueList()<<jsEngine_->toScriptValue(libs));
  if(result.isError())
  {
      log_->addError(
            QString("[js] Error at line #%1 : %2")
            .arg(result.property("lineNumber").toInt())
            .arg(result.toString())
                 );
  }
}
//------------------------------------------------------
QString replaceAliesesToDir(const QString& toDir,
                            const QString& exeFileDir,
                            const QString& baseName)
{
  QString result= toDir;
  if(toDir.isEmpty())
  {
    result = exeFileDir;
  }
  else // replace alieses
  {
    QRegularExpression reDir("<ExeDir>");
    QRegularExpression reName("<BaseName>");
    result.replace(reDir, exeFileDir);
    result.replace(reName, baseName);
  }
  return result;
}
//------------------------------------------------------
void MainWindow::on_pushButtonCopy_clicked()
{
  QFile exeFile(ui->lineEditFilePath->text());
  QFileInfo exeFileInfo(exeFile);
  QString exeFileDir= exeFileInfo.dir().absolutePath();

  QString toDir=
      replaceAliesesToDir(ui->lineEditToDir->text(),
                          exeFileDir,
                          exeFileInfo.baseName());

  /* create dir if not exists */
  QDir dir(toDir);
  if(!dir.exists())
    dir.mkpath(tr("."));

  /* copy exe file */
//  if(QDir(toDir)!=QDir(exeFileDir))
//    exeFile.copy(toDir+QDir::separator()+exeFileInfo.fileName());

  /* call copy in js */
  if(!jsEngine_)
  {
    log_->addError(tr("Js script is not valid!"));
    return;
  }

  jsEngine_->globalObject().setProperty("QTDIR",ui->lineEditQtDir->text().trimmed());

  QJSValue jsCopyFunction = jsEngine_->globalObject().property("copy");
  QJSValue result = jsCopyFunction.call( QJSValueList()<<toDir);
  if(result.isError())
  {
      log_->addError(
            QString("[js] Error at line #%1 : %2")
            .arg(result.property("lineNumber").toInt())
            .arg(result.toString())
                 );
  }
}
//------------------------------------------------------------
QString MainWindow::currentJsScript() const
{
  QVariant data = ui->comboBoxScript->currentData(); // try get full absolute path
  return data.isNull()? ui->comboBoxScript->currentText() : data.toString();
}
//----------------------------------------------------------
void MainWindow::on_comboBoxScript_currentTextChanged(const QString &arg1)
{
  Q_UNUSED(arg1);
  jsEngine_ = createJSEngine(currentJsScript()); // change js-file
  if(!jsEngine_)
  {
    log_->addError(tr("Js script is not valid!"));
    return;
  }

  ui->pushButtonUpdate->setEnabled(jsEngine_ != Q_NULLPTR);
  ui->pushButtonCopy->setEnabled(jsEngine_   != Q_NULLPTR);
}
//------------------------------------------------------------
QSharedPointer<QJSEngine> MainWindow::createJSEngine(const QString &scriptFileName)
{
  QFile file(scriptFileName);
  if(!file.open(QIODevice::ReadOnly))
  {
    log_->addError(tr("Error: can`t open file \"%1\"").arg((scriptFileName)));
    return Q_NULLPTR;
  }

  QSharedPointer<QJSEngine> jsEngine(new QJSEngine);
  QString script = file.readAll();
  jsEngine->installExtensions(QJSEngine::TranslationExtension | QJSEngine::ConsoleExtension);
  QJSValue result = jsEngine->evaluate(script);
  if(result.isError())
  {
      log_->addError(
            QString("[js] Error at line #%1 : %2")
              .arg(result.property("lineNumber").toInt())
              .arg(result.toString())
                 );

      return Q_NULLPTR;
  }

  QJSValue jsLog = jsEngine->newQObject(log_);
  jsEngine->globalObject().setProperty("log",jsLog);

  QJSValue jsTree = jsEngine->newQObject(tree_);
  jsEngine->globalObject().setProperty("tree",jsTree);

  QJSValue jsUtils = jsEngine->newQObject(utils_);
  jsEngine->globalObject().setProperty("utils",jsUtils);

  QJSValue jsFileInfo = jsEngine->newQMetaObject<FileInfo>();
  jsEngine->globalObject().setProperty("FileInfo",jsFileInfo);

  QJSValue jsDir = jsEngine->newQMetaObject<Dir>();
  jsEngine->globalObject().setProperty("Dir",jsDir);

  jsEngine->globalObject().setProperty("QTDIR",ui->lineEditQtDir->text().trimmed());
  jsEngine->globalObject().setProperty("SYSTEMROOT",systemRoot_);
  jsEngine->globalObject().setProperty("FILEPATH",ui->lineEditFilePath->text().trimmed());

  QString os = "";
  #ifdef Q_OS_WIN
     os = "win";
  #elif defined Q_OS_LINUX
     os = "linux";
  #endif

  jsEngine->globalObject().setProperty("os",os);

  /* call init in js */
  QJSValue jsInitFunction = jsEngine->globalObject().property("init");
  result = jsInitFunction.call( QJSValueList());
  if(result.isError())
  {
      log_->addError(
            QString("[js] Error at line #%1 : %2")
            .arg(result.property("lineNumber").toInt())
            .arg(result.toString())
                 );
  }

  return jsEngine;
}
//------------------------------------------------------------
LibraryCollector::PidType MainWindow::startedProcessId()
{
  /* Already started */
  if(processAlreadyStarted_)
  {
     #ifdef Q_OS_WIN
        return  process_->processId();
     #elif defined Q_OS_LINUX
        return  process_->pid();
      #endif
  }
  else /* Find or Run */
  {
    QFileInfo info(QFile(ui->lineEditFilePath->text()));
    QString processFilePath= info.filePath();

    if(!info.exists())
      log_->addError(QString(tr("Executable file \"%1\" not exist")).arg(processFilePath));

    /* Find process */
    QString error;
    LibraryCollector::PidType pid=  LibraryCollector::processIdByFilePath(processFilePath,error);
    if(error.isEmpty())
    {
      return pid;
    }
    else /* Not Found */
    {
      log_->addInfo(error,"orange");

      /* Run process */
      on_pushButtonRun_clicked();
      if(process_->waitForStarted())
      {
        #ifdef Q_OS_WIN
             pid=  process_->processId();
             HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
             WaitForInputIdle(hProcess,5000);
             CloseHandle(hProcess);
        #elif defined Q_OS_LINUX
             pid=  process_->processId();
        #endif
        QThread::usleep(500);
        return pid;
      }
      else
      {
        return static_cast<LibraryCollector::PidType>(-1);
      }
    }
  }
}
//-----------------------------------------------------------------------------
void MainWindow::on_pushButtonCopyToClipboard_clicked()
{
    QStringList list;
    for(int i=0; i<ui->treeWidget->topLevelItemCount(); ++i)
    {
      QTreeWidgetItem* topLevalItem = ui->treeWidget->topLevelItem(i);
      list.append(topLevalItem->text(0));
      for(int j=0; j<topLevalItem->childCount(); ++j)
      {
        QTreeWidgetItem *childItem= topLevalItem->child(j);
        list.append("\t"+childItem->text(0));
      }
    }
    if(!list.empty())
      QApplication::clipboard()->setText(list.join("\n"));
}
//-----------------------------------------------------------------------------
void MainWindow::on_pushButtonClear_clicked()
{
  ui->textEditLog->clear();
  on_comboBoxScript_currentTextChanged("");
}
//-----------------------------------------------------------------------------
void MainWindow::clear()
{
  ui->treeWidget->clear();
  ui->lineEditFilePath->clear();
}
//-----------------------------------------------------------------------------
void MainWindow::processErrorOccurred(QProcess::ProcessError )
{
  log_->addError(process_->errorString());
}
//-----------------------------------------------------------------------------
void MainWindow::processStarted()
{
  processAlreadyStarted_= true;
  log_->addInfo(tr("Process started."),"blue");
}
//-----------------------------------------------------------------------------
void MainWindow::processFinished(int, QProcess::ExitStatus)
{
  processAlreadyStarted_= false;
  log_->addInfo(tr("Process finished."),"blue");
}
//-----------------------------------------------------------------------------
// Tools
//-----------------------------------------------------------------------------
void MainWindow::on_toolButtonTo_clicked()
{   
  QFile exeFile(ui->lineEditFilePath->text());
  QFileInfo exeFileInfo(exeFile);
  QString exeFileDir= exeFileInfo.dir().absolutePath();

  QString toDir=
      replaceAliesesToDir(ui->lineEditToDir->text(),
                          exeFileDir,
                          exeFileInfo.baseName());

  toDir = QFileDialog::getExistingDirectory( this,
                                         tr("Select directory"),
                                         toDir,
                                         QFileDialog::ShowDirsOnly);

  if(!toDir.isEmpty())
    ui->lineEditToDir->setText(toDir);
}
//-----------------------------------------------------------------------------
void MainWindow::on_toolButtonQTDIR_clicked()
{
  QString qtDir =
       QFileDialog::getExistingDirectory(this,
                                         tr("Select directory"),
                                         ui->lineEditQtDir->text(),
                                         QFileDialog::ShowDirsOnly);

  if(!qtDir.isEmpty())
    ui->lineEditQtDir->setText(qtDir);
}
//-----------------------------------------------------------------------------
void MainWindow::on_toolButtonFilePath_clicked()
{
#ifdef Q_OS_WIN
  const QString mask = "*.exe";
#elif defined Q_OS_LINUX
  const QString mask="*";
#endif

  QString name=
      QFileDialog::getOpenFileName(this,
                                   tr("Select file"),
                                   ui->lineEditFilePath->text(),
                                   tr("Executable (%1)").arg(mask)
                                   );
  if(!name.isEmpty())
    ui->lineEditFilePath->setText(name);
}
//-----------------------------------------------------------------------------
void MainWindow::on_AimRelease(QPoint pos)
{
#ifdef Q_OS_WIN
  QString error;
  QString processFilePath=
      LibraryCollector::processFilePathByPoint(pos.x(),pos.y(),error);
  if(error.isEmpty())
    ui->lineEditFilePath->setText(processFilePath);
  else
    log_->addError(error);
  setCursor(Qt::ArrowCursor);
#else
  Q_UNUSED(pos)
#endif
}
//-----------------------------------------------------------------------------
void MainWindow::on_toolButtonAim_pressed()
{
#ifdef Q_OS_WIN
  setCursor(Qt::CrossCursor);
#endif
}
//-----------------------------------------------------------------------------
void MainWindow::on_toolButtonAim_clicked()
{
#ifdef Q_OS_LINUX
  QString error;
  QString processFilePath=
      Lc::processFilePathBySelectedWindow(error);
  if(error.isEmpty())
    ui->lineEditFilePath->setText(processFilePath);
  else
    log_.addError(error);
#endif
}
//-----------------------------------------------------------------------------
void MainWindow::on_toolButtonScript_clicked()
{
  QString fileName=
      QFileDialog::getOpenFileName(this,
                                   tr("Select file"),
                                   ui->comboBoxScript->currentText(),
                                   tr("js (*.js)")
                                   );
  if(!fileName.isEmpty())
  {
    ui->comboBoxScript->setCurrentText(fileName);
    on_comboBoxScript_currentTextChanged(fileName);
  }
}
//-----------------------------------------------------------------------------
void MainWindow::loadSettings()
{
  QSettings settings(tr("settings.ini"),QSettings::IniFormat);
  settings.beginGroup(tr("Paths"));
  ui->lineEditQtDir->setText(settings.value("QT_DIR").toString());

  QString scriptFileName = settings.value("script").toString();
  int index = ui->comboBoxScript->findText(scriptFileName);
  if(index!=-1)
     ui->comboBoxScript->setCurrentIndex(index);
  else
    ui->comboBoxScript->setCurrentText(scriptFileName);

  QStringList args= QApplication::arguments();
  if(args.size()==5) // Run from QtCreator menu
  {
    QString buildPath= args[1];
    QString buildName= args[2];

    if(buildName==tr("Отладка")) buildName="debug";
    if(buildName==tr("Выпуск"))  buildName="release";

    // exe %{CurrentProject:BuildPath}\%{CurrentBuild:Name}\%{CurrentProject:FileBaseName}
    // bin %{CurrentProject:QT_HOST_BINS}
    QString fullName= buildPath+QDir::separator()+
                      buildName+QDir::separator()+
                      args[3]+".exe";

    QDir dir(args[4]);
    dir.cdUp();
    QString qtDir = dir.absolutePath();

    ui->lineEditFilePath->setText(fullName);
    ui->lineEditQtDir->setText(qtDir);
    process_->setWorkingDirectory(buildPath);
  } //
  else
  {
    QFile file(settings.value("name").toString());
    ui->lineEditFilePath->setText(file.fileName());
    process_->setWorkingDirectory(QFileInfo(file).absolutePath());
  }

  ui->lineEditToDir->setText(settings.value("to").toString());
  settings.endGroup();

  settings.beginGroup(tr("Other"));
  ui->checkBoxAddBin->setCheckState(
        static_cast<Qt::CheckState>(settings.value("addLibsDirToEnvironment").toInt()) );
  ui->checkBoxAddPlugins->setCheckState(
        static_cast<Qt::CheckState>(settings.value("addPluginsDirToEnvironment").toInt()) );
  settings.endGroup();

#ifdef Q_OS_WIN
  QProcessEnvironment enviroment(QProcessEnvironment::systemEnvironment());
  systemRoot_ = enviroment.value(tr("SYSTEMROOT"));
#endif
}
//-----------------------------------------------------------------------------
void MainWindow::saveSettings()
{
  QSettings settings("settings.ini",QSettings::IniFormat);
  settings.beginGroup("Paths");
  settings.setValue("name",   ui->lineEditFilePath->text());
  settings.setValue("to",     ui->lineEditToDir->text());
  settings.setValue("QT_DIR", ui->lineEditQtDir->text());
  settings.setValue("script", ui->comboBoxScript->currentText());
  settings.endGroup();

  settings.beginGroup("Other");
  settings.setValue("addLibsDirToEnvironment", ui->checkBoxAddBin->checkState());
  settings.setValue("addPluginsDirToEnvironment", ui->checkBoxAddPlugins->checkState());
  settings.endGroup();
}
//-----------------------------------------------------------------------------






