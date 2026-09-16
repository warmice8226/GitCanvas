#include "mainwindow.h"
#include "diagnostics.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
void MainWindow::appendLog(const QString &text){log_->appendPlainText(Diagnostics::redact(text));}
void MainWindow::showAppSettings(){
    QDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setObjectName("appSettingsDialog");dialog.resize(760,560);auto *layout=new QVBoxLayout(&dialog);
    auto *title=new QLabel(tr("앱 설정 · 진단"));layout->addWidget(title);
    auto *language=new QComboBox;language->setObjectName("applicationLanguage");language->addItem(tr("운영체제 언어"),"system");language->addItem("한국어","ko");language->addItem("English","en");language->setCurrentIndex(qMax(0,language->findData(QSettings().value("ui/language","system"))));layout->addWidget(language);
    auto *hint=new QLabel(tr("언어와 날짜·숫자 형식은 다음 실행부터 적용됩니다. 작업 중인 내용은 그대로 유지됩니다."));hint->setWordWrap(true);layout->addWidget(hint);
    connect(language,&QComboBox::currentIndexChanged,&dialog,[language]{QSettings().setValue("ui/language",language->currentData());});
    auto *retain=new QCheckBox(tr("진단 기록 보관 · 최대 500건, 7일 (기본 꺼짐)"));retain->setObjectName("retainDiagnostics");retain->setChecked(QSettings().value("diagnostics/retain",false).toBool());layout->addWidget(retain);connect(retain,&QCheckBox::toggled,diagnostics_,&Diagnostics::setRetention);
    auto *privacy=new QLabel(tr("보고서에는 앱·OS 버전, 작업 종류, 종료 코드, 소요 시간과 복구 안내만 담습니다. 원격 주소·파일 경로·계정·명령 인자·본문·원문 출력은 포함하지 않습니다."));privacy->setWordWrap(true);layout->addWidget(privacy);
    auto *preview=new QPlainTextEdit;preview->setObjectName("diagnosticPreview");preview->setReadOnly(true);layout->addWidget(preview,1);
    auto refresh=[this,preview]{preview->setPlainText(QString::fromUtf8(QJsonDocument(diagnostics_->report()).toJson()));};refresh();connect(diagnostics_,&Diagnostics::changed,&dialog,refresh);
    auto *result=new QLabel;result->setWordWrap(true);result->setTextFormat(Qt::PlainText);layout->addWidget(result);
    auto showStorageError=[this,result]{result->setText(diagnostics_->storageError().isEmpty()?QString():tr("진단 기록 파일 작업에 실패했습니다: %1").arg(Diagnostics::redact(diagnostics_->storageError())));};showStorageError();connect(diagnostics_,&Diagnostics::storageErrorChanged,&dialog,showStorageError);
    auto *exportButton=new QPushButton(tr("진단 보고서 내보내기"));exportButton->setObjectName("exportDiagnostics");layout->addWidget(exportButton);
    connect(exportButton,&QPushButton::clicked,&dialog,[this,&dialog,result]{QFileDialog save(&dialog,tr("진단 보고서 저장"));save.setWindowFlags(Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);save.setOption(QFileDialog::DontUseNativeDialog);save.setAcceptMode(QFileDialog::AcceptSave);save.setNameFilter("JSON (*.json)");save.setDefaultSuffix("json");save.selectFile("GitCanvas-diagnostics.json");if(save.exec()!=QDialog::Accepted)return;QString error;const bool ok=diagnostics_->exportReport(save.selectedFiles().first(),&error);result->setText(ok?tr("진단 보고서를 저장했습니다."):Diagnostics::redact(error));});
    auto *clear=new QPushButton(tr("로그와 진단 기록 지우기"));clear->setObjectName("clearDiagnostics");layout->addWidget(clear);connect(clear,&QPushButton::clicked,&dialog,[this]{diagnostics_->clear();log_->clear();});
    auto *close=new QPushButton(tr("닫기"));layout->addWidget(close);connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
}
