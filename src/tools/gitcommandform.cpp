#include "gitcommandform.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QLocale>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTextDocumentFragment>
#include <QVBoxLayout>
#include <QTabWidget>

namespace {
const QJsonObject &translationCatalog() {
    // The Git documentation translation is a separately licensed data file.
    // Only application-relative paths are searched, never a repository's files.
    static const auto catalog=[] {
        const QDir app(QCoreApplication::applicationDirPath());
        for(const auto &relative : {"../share/gitcanvas/git-manuals.ko.json",
                                   "../Resources/git-manuals.ko.json",
                                   "share/gitcanvas/git-manuals.ko.json"}) {
            QFile file(app.filePath(QString::fromLatin1(relative)));
            if(file.open(QIODevice::ReadOnly)) {
                const auto document=QJsonDocument::fromJson(file.readAll());
                if(document.isObject())return document.object();
            }
        }
        return QJsonObject{};
    }();
    return catalog;
}
QString definitionBody(const QString &tail) {
    auto tags=QRegularExpression("</?dd\\b[^>]*>",QRegularExpression::CaseInsensitiveOption).globalMatch(tail);
    int depth=0,start=-1;
    while(tags.hasNext()){
        const auto tag=tags.next();
        if(tag.captured().startsWith("</")){if(--depth==0&&start>=0)return tail.mid(start,tag.capturedStart()-start);}
        else {if(depth++==0)start=tag.capturedEnd();}
    }
    return {};
}
QStringList optionChoices(const GitCommandOption &option) {
    const auto alternatives=QRegularExpression("[<(]([a-z][a-z0-9-]*(?:\\|[a-z][a-z0-9-]*)+)[>)]").match(option.syntax);
    if (alternatives.hasMatch()) return alternatives.captured(1).split('|');
    const QMap<QString,QStringList> known{
        {"--color", {"always","never","auto"}},
        {"--word-diff", {"color","plain","porcelain","none"}},
        {"--diff-algorithm", {"myers","minimal","patience","histogram"}},
        {"--ignore-submodules", {"none","untracked","dirty","all"}},
        {"--untracked-files", {"no","normal","all"}},
        {"--cleanup", {"strip","whitespace","verbatim","scissors","default"}}
    };
    const auto values=known.value(option.name);
    for (const auto &value:values) if (!option.description.contains(QRegularExpression("\\b"+value+"\\b"))) return {};
    return values;
}
}

GitCommandManual GitCommandManual::parse(const QString &html){
    GitCommandManual result;result.text=QTextDocumentFragment::fromHtml(html).toPlainText();
    auto section=[&html](const QString &heading){const auto match=QRegularExpression("<h2\\b[^>]*>\\s*"+heading+"\\s*</h2>([\\s\\S]*?)(?=<h2\\b|$)",QRegularExpression::CaseInsensitiveOption).match(html);return QTextDocumentFragment::fromHtml(match.captured(1)).toPlainText().trimmed();};
    result.summary=section("NAME");result.summary.remove(QRegularExpression("^git-\\S+\\s*[-–]\\s*"));result.description=section("DESCRIPTION");
    const QRegularExpression terms("<dt\\b[^>]*>([\\s\\S]*?)</dt>",QRegularExpression::CaseInsensitiveOption);
    auto matches=terms.globalMatch(html);QSet<QString> seen;
    while(matches.hasNext()){
        const auto match=matches.next();const auto syntax=QTextDocumentFragment::fromHtml(match.captured(1)).toPlainText().simplified();
        const auto body=definitionBody(html.mid(match.capturedEnd()));
        const auto description=QTextDocumentFragment::fromHtml(body).toPlainText();
        const auto before=html.left(match.capturedStart());const int heading=before.lastIndexOf("<h2",-1,Qt::CaseInsensitive);const auto section=heading<0?QString():QTextDocumentFragment::fromHtml(before.mid(heading,before.indexOf("</h2>",heading,Qt::CaseInsensitive)-heading)).toPlainText().toUpper();
        if(section.contains("COMMANDS")||section=="MODES"){
            const auto verb=QRegularExpression("^[a-z][a-z0-9-]*(?=$|[ \\[])").match(syntax).captured();if(!verb.isEmpty()&&!result.subcommands.contains(verb))result.subcommands.append(verb);
        }
        if(!syntax.startsWith('-'))continue;
        QStringList variants{syntax};if(syntax.startsWith("--[no-]"))variants={QString(syntax).replace("[no-]",""),QString(syntax).replace("[no-]","no-")};
        bool parsed=false;
        for(const auto &variant:variants){
            if(variant=="--"||variant=="-"||QRegularExpression("^-[A-Za-z]{2,}$").match(variant).hasMatch()){if(!seen.contains(variant)){result.options.append({variant,variant,description});seen.insert(variant);}parsed=true;continue;}
            const auto attached=QRegularExpression("^(-|-[A-Za-z]+:)(<[^>]+>.*)$").match(variant);
            if(attached.hasMatch()){const auto name=attached.captured(1);if(!seen.contains(variant)){result.options.append({name,variant,description,true,false,false,true});seen.insert(variant);}parsed=true;continue;}
            const auto option=QRegularExpression("^(--[A-Za-z0-9][A-Za-z0-9-]*|-[A-Za-z0-9])(?=$|[ =\\[<])").match(variant);if(!option.hasMatch())continue;
            const auto name=option.captured(1);if(seen.contains(name)){parsed=true;continue;}
            auto rest=variant.mid(name.size()).trimmed();
            // Composite spellings require separate explicit arguments; do not guess.
            if(rest.contains(" | ")||rest.startsWith(',')||rest.contains(" --"))continue;
            GitCommandOption entry{name,variant,description,!rest.isEmpty(),rest.startsWith('['),rest.startsWith('=')||rest.startsWith("[=")};
            const auto rawRest=variant.mid(name.size());entry.attached=!name.startsWith("--")&&(rawRest.startsWith('<')||rawRest.startsWith('['));
            if(name.startsWith("--no-")&&syntax.startsWith("--[no-]"))entry.value=entry.optional=entry.equals=false;
            if(entry.value){
                entry.choices=optionChoices(entry);
                if(!syntax.contains("<format>")&&!QRegularExpression("custom|arbitrary|format:",QRegularExpression::CaseInsensitiveOption).match(description).hasMatch()&&QRegularExpression("must be one of|possible values are|variants are as follows",QRegularExpression::CaseInsensitiveOption).match(description).hasMatch()){
                    auto enums=terms.globalMatch(body);QStringList values;
                    while(enums.hasNext()){const auto text=QTextDocumentFragment::fromHtml(enums.next().captured(1)).toPlainText().trimmed();if(QRegularExpression("^[a-z][a-z0-9-]*$").match(text).hasMatch())values.append(text);}
                    if(values.size()>1){for(const auto &value:values)if(!entry.choices.contains(value))entry.choices.append(value);}
                }
            }
            result.options.append(entry);seen.insert(name);parsed=true;
        }
        if(!parsed&&!result.unparsed.contains(syntax))result.unparsed.append(syntax);
    }
    return result;
}
GitCommandForm::GitCommandForm(GitClient *git,QWidget *parent):QDialog(parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint),git_(git),controller_(git,this){
    setObjectName("gitCommandForm");resize(1300,850);setMinimumSize(1000,650);auto *layout=new QVBoxLayout(this);
    auto *intro=new QLabel(tr("Git 명령 · 옵션 GUI — 설치된 Git 설명서의 옵션을 선택하고 값을 입력합니다. 순서·반복·선택 값을 조작할 수 있습니다. 미리보기 후 확인해야 실행됩니다."));intro->setWordWrap(true);layout->addWidget(intro);
    inputs_=new QWidget;auto *inputLayout=new QVBoxLayout(inputs_);inputLayout->setContentsMargins(0,0,0,0);
    descriptionLanguage_=new QComboBox;descriptionLanguage_->setObjectName("formDescriptionLanguage");descriptionLanguage_->addItem("한국어","ko");descriptionLanguage_->addItem("English","en");descriptionLanguage_->setCurrentIndex(QLocale().language()==QLocale::Korean?0:1);inputLayout->addWidget(descriptionLanguage_);
    auto *commandPage=new QWidget;auto *commandLayout=new QVBoxLayout(commandPage);commandSearch_=new QLineEdit;commandSearch_->setObjectName("formCommandSearch");commandSearch_->setPlaceholderText(tr("명령 이름 · 설명 검색"));commandLayout->addWidget(commandSearch_);commands_=new QComboBox;commands_->setObjectName("formCommand");commands_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);commands_->setMinimumContentsLength(25);commandLayout->addWidget(commands_);
    auto *commandViews=new QTabWidget;commandDescription_=new QPlainTextEdit;commandDescription_->setObjectName("formCommandDescription");commandDescription_->setReadOnly(true);commandOriginal_=new QPlainTextEdit;commandOriginal_->setObjectName("formCommandOriginal");commandOriginal_->setReadOnly(true);commandViews->addTab(commandDescription_,tr("명령 설명"));commandViews->addTab(commandOriginal_,tr("원문 전체"));commandLayout->addWidget(commandViews);
    auto *useCommand=new QPushButton(tr("명령사용"));useCommand->setObjectName("formUseCommand");useCommand->setToolTip(tr("선택한 명령의 옵션을 구성합니다. 실행은 미리보기 후 별도로 확인합니다."));commandViews->setCornerWidget(useCommand,Qt::TopRightCorner);
    summary_=new QLabel;summary_->setWordWrap(true);summary_->setTextFormat(Qt::PlainText);inputLayout->addWidget(summary_);
    auto *split=new QSplitter;auto *left=new QWidget;auto *leftLayout=new QVBoxLayout(left);optionSearch_=new QLineEdit;optionSearch_->setPlaceholderText(tr("옵션 이름 · 설명 검색"));leftLayout->addWidget(optionSearch_);
    options_=new QTableWidget(0,2);options_->setObjectName("formOptions");options_->setHorizontalHeaderLabels({tr("옵션"),tr("설명")});options_->setEditTriggers(QAbstractItemView::NoEditTriggers);options_->setSelectionBehavior(QAbstractItemView::SelectRows);options_->setSelectionMode(QAbstractItemView::SingleSelection);options_->horizontalHeader()->setStretchLastSection(true);options_->setColumnWidth(0,220);leftLayout->addWidget(options_,2);
    auto *add=new QPushButton(tr("선택 옵션 추가"));add->setObjectName("formAddOption");leftLayout->addWidget(add);auto *chooser=new QTabWidget;chooser->setObjectName("formChooser");chooser->addTab(commandPage,"git");chooser->addTab(left,"--option");split->addWidget(chooser);
    auto *right=new QWidget;auto *rightLayout=new QVBoxLayout(right);auto *subRow=new QHBoxLayout;subcommands_=new QComboBox;subcommands_->setObjectName("formSubcommand");subRow->addWidget(subcommands_,1);auto *subAdd=new QPushButton(tr("하위 명령 추가"));subRow->addWidget(subAdd);rightLayout->addLayout(subRow);
    chooser->setTabText(0,tr("명령어"));chooser->setTabText(1,tr("옵션"));
    connect(useCommand,&QPushButton::clicked,chooser,[this,chooser]{if(!commands_->currentData().toString().isEmpty()){chooser->setCurrentIndex(1);optionSearch_->setFocus();}});
    selected_=new QTableWidget(0,4);selected_->setObjectName("formArguments");selected_->setHorizontalHeaderLabels({tr("선택 항목 · 실행 순서"),tr("값"),tr("값 전달"),tr("경로 선택")});selected_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);selected_->setColumnWidth(0,180);selected_->setColumnWidth(2,80);selected_->setColumnWidth(3,90);selected_->setSelectionBehavior(QAbstractItemView::SelectRows);rightLayout->addWidget(selected_,2);
    auto *row=new QHBoxLayout;auto *operand=new QPushButton(tr("대상 값 추가"));auto *separator=new QPushButton(tr("경로 구분자 추가"));auto *remove=new QPushButton(tr("선택 제거"));auto *up=new QPushButton("↑");auto *down=new QPushButton("↓");for(auto *button:{operand,separator,remove,up,down})row->addWidget(button);rightLayout->addLayout(row);
    auto *hint=new QLabel(tr("대상 값에는 커밋·경로·패턴 등을 하나씩 입력합니다. 옵션은 왼쪽 목록에서 추가합니다. 경로 구분자 뒤의 값은 옵션으로 해석되지 않습니다. 같은 옵션을 여러 번 추가할 수 있습니다."));hint->setWordWrap(true);rightLayout->addWidget(hint);
    stdin_=new QPlainTextEdit;stdin_->setObjectName("formStdin");stdin_->setPlaceholderText(tr("표준 입력 · UTF-8 텍스트 (선택)"));stdin_->setMaximumHeight(90);rightLayout->addWidget(stdin_);split->addWidget(right);split->setSizes({600,600});inputLayout->addWidget(split,1);layout->addWidget(inputs_,1);
    auto *assemble = new QPushButton(tr("명령문에 추가")); assemble->setObjectName("formAssemble"); inputLayout->addWidget(assemble);
    sentence_=new QPlainTextEdit; sentence_->setObjectName("formSentence"); sentence_->setReadOnly(true); sentence_->setMaximumHeight(80); sentence_->setPlaceholderText(tr("명령과 옵션 값을 정한 뒤 '명령문에 추가'를 누르세요.")); layout->addWidget(sentence_);
    connect(assemble,&QPushButton::clicked,this,&GitCommandForm::compose);
    output_=new QPlainTextEdit;output_->setReadOnly(true);output_->setObjectName("formOutput");output_->setMaximumHeight(170);layout->addWidget(output_);
    auto *actions=new QHBoxLayout;previewButton_=new QPushButton(tr("조회 / 실행 미리보기"));previewButton_->setObjectName("formPreview");executeButton_=new QPushButton(tr("확인 후 실행"));executeButton_->setObjectName("formExecute");auto *close=new QPushButton(tr("닫기"));actions->addWidget(previewButton_);actions->addWidget(executeButton_);actions->addWidget(close);layout->addLayout(actions);
    connect(close,&QPushButton::clicked,this,&GitCommandForm::reject);connect(commands_,&QComboBox::currentIndexChanged,this,&GitCommandForm::loadManual);connect(optionSearch_,&QLineEdit::textChanged,this,&GitCommandForm::filterOptions);
    connect(commandSearch_,&QLineEdit::textChanged,this,&GitCommandForm::refreshCommands);
    connect(descriptionLanguage_,&QComboBox::currentIndexChanged,this,[this]{const QSignalBlocker block(commandSearch_);commandSearch_->clear();refreshCommands();filterOptions();refreshDescriptions();});
    connect(add,&QPushButton::clicked,this,[this]{const auto row=options_->currentRow();if(row<0)return;const auto index=options_->item(row,0)->data(Qt::UserRole).toInt();const auto &o=manual_.options[index];addRow(o.name,o.syntax,{},o.attached?(o.optional?7:6):o.value?(o.optional?3:1)+(o.equals?1:0):0);});
    connect(options_,&QTableWidget::cellDoubleClicked,this,[add](int,int){add->click();});
    connect(subAdd,&QPushButton::clicked,this,[this]{if(!subcommands_->currentText().isEmpty())addRow(subcommands_->currentText(),subcommands_->currentText(),{},0);});
    connect(operand,&QPushButton::clicked,this,[this]{addRow({},tr("대상 값"),{},5);});connect(separator,&QPushButton::clicked,this,[this]{addRow("--","--",{},0);});
    connect(remove,&QPushButton::clicked,this,[this]{selected_->removeRow(selected_->currentRow());invalidate();});connect(up,&QPushButton::clicked,this,[this]{moveRow(-1);});connect(down,&QPushButton::clicked,this,[this]{moveRow(1);});
    connect(stdin_,&QPlainTextEdit::textChanged,this,&GitCommandForm::invalidate);connect(previewButton_,&QPushButton::clicked,this,&GitCommandForm::preview);connect(executeButton_,&QPushButton::clicked,this,&GitCommandForm::execute);connect(git_,&GitClient::busyChanged,this,&GitCommandForm::updateButtons);
    loading_=true;updateButtons();git_->inspectEnvironment({"--html-path"},[this](bool ok,const QByteArray &out,const QString &error){
        if(!ok){loading_=false;output_->setPlainText(error);updateButtons();return;}docs_=QString::fromUtf8(out).trimmed();
        git_->inspectEnvironment({"--list-cmds=main"},[this](bool ok,const QByteArray &out,const QString &error){loading_=false;if(!ok){output_->setPlainText(error);updateButtons();return;}auto names=QString::fromUtf8(out).split('\n',Qt::SkipEmptyParts);names.sort();commands_->setProperty("names",names);{const QSignalBlocker block(commands_);for(const auto &name:names)commands_->addItem(commandLabel(name),name);}loadManual();});
    });
}
void GitCommandForm::loadManual(){
    selected_->setRowCount(0);stdin_->clear();invalidate();manual_={};const auto name=commands_->currentData().toString();
    if(QRegularExpression("^[a-z][a-z0-9-]*$").match(name).hasMatch()){
        QFile file(QDir(docs_).filePath("git-"+name+".html"));if(file.size()<=8*1024*1024&&file.open(QIODevice::ReadOnly))manual_=GitCommandManual::parse(QString::fromUtf8(file.readAll()));
    }
    subcommands_->clear();subcommands_->addItems(manual_.subcommands);filterOptions();refreshDescriptions();
    summary_->setText(manual_.text.isEmpty()?tr("이 명령의 로컬 HTML 설명서가 없습니다. Git 설명서 패키지를 설치해야 옵션 폼을 만들 수 있습니다."):tr("%1 · 옵션 %2개 · 복합 표기 %3개. 복합 표기는 자동 변환하지 않으며 옵션 조합의 유효성은 Git이 검사합니다.").arg(name).arg(manual_.options.size()).arg(manual_.unparsed.size()));
    if(!manual_.unparsed.isEmpty())summary_->setToolTip(manual_.unparsed.join('\n'));else summary_->setToolTip({});updateButtons();
}
QString GitCommandForm::translated(const QString &text) const {
    if(text.trimmed().isEmpty())return {};
    const auto key=QString::fromLatin1(QCryptographicHash::hash(text.simplified().toUtf8(),QCryptographicHash::Sha256).toHex());
    const auto result=translationCatalog().value("texts").toObject().value(key).toString();
    return result.isEmpty()?tr("이 설명서 버전의 번역이 없습니다. 원문을 표시합니다.")+"\n\n"+text:result;
}
QString GitCommandForm::commandLabel(const QString &name) const {
    const auto entry=translationCatalog().value("commands").toObject().value(name).toObject();
    auto summary=entry.value(descriptionLanguage_->currentData()=="ko"?"summary_ko":"summary_en").toString();
    if(descriptionLanguage_->currentData()=="ko"){
        static const auto summaries=[]{QFile file(":/i18n/git-command-summaries.ko.json");if(!file.open(QIODevice::ReadOnly))return QJsonObject{};return QJsonDocument::fromJson(file.readAll()).object();}();
        summary=summaries.value(name).toString(summary);
    }
    return name+" · "+(summary.isEmpty()?tr("로컬 설명서 없음"):summary);
}
void GitCommandForm::refreshCommands(){
    const auto previous=commands_->currentData().toString();
    {const QSignalBlocker block(commands_);commands_->clear();for(const auto &name:commands_->property("names").toStringList()){
        const auto label=commandLabel(name);if(label.contains(commandSearch_->text(),Qt::CaseInsensitive))commands_->addItem(label,name);
    }const int row=commands_->findData(previous);if(row>=0)commands_->setCurrentIndex(row);}
    if(commands_->currentData().toString()!=previous)loadManual();
}
void GitCommandForm::refreshDescriptions(){
    const bool korean=descriptionLanguage_->currentData()=="ko";
    commandDescription_->setPlainText(korean?tr("한국어 설명 · 자동 번역 (원문과 함께 확인하세요)")+"\n\n"+translated(manual_.description):manual_.description);
    commandOriginal_->setPlainText(manual_.text);

}
void GitCommandForm::filterOptions(){
    const int previous=options_->currentRow()>=0&&options_->item(options_->currentRow(),0)?options_->item(options_->currentRow(),0)->data(Qt::UserRole).toInt():-1;
    const QSignalBlocker block(options_);options_->setRowCount(0);int selection=-1;
    for(int i=0;i<manual_.options.size();++i){const auto &o=manual_.options[i];const auto description=descriptionLanguage_->currentData()=="ko"?translated(o.description):o.description;
        if(!(o.syntax+" "+description+" "+o.description).contains(optionSearch_->text(),Qt::CaseInsensitive))continue;
        int row=options_->rowCount();options_->insertRow(row);auto *name=new QTableWidgetItem(o.syntax);name->setData(Qt::UserRole,i);options_->setItem(row,0,name);auto *item=new QTableWidgetItem(description);item->setToolTip(description);options_->setItem(row,1,item);options_->setRowHeight(row,44);if(i==previous)selection=row;
    }
    if(options_->rowCount())options_->setCurrentCell(selection>=0?selection:0,0);refreshDescriptions();
}
void GitCommandForm::addRow(const QString &name,const QString &syntax,const QString &value,int mode){
    const int row=selected_->rowCount();selected_->insertRow(row);auto *label=new QTableWidgetItem(syntax);label->setFlags(label->flags()&~Qt::ItemIsEditable);label->setData(Qt::UserRole,name);label->setData(Qt::UserRole+1,mode);selected_->setItem(row,0,label);
    QStringList choices;for(const auto &option:manual_.options)if(option.name==name&&mode!=0){choices=option.choices;break;}
    QWidget *editor=nullptr;
    if(!choices.isEmpty()){
        auto *choice=new QComboBox;choice->setEditable(false);choice->addItems(choices);if(!value.isEmpty())choice->setCurrentText(value);editor=choice;
        connect(choice,&QComboBox::currentIndexChanged,this,&GitCommandForm::invalidate);
    }else{
        auto *edit=new QPlainTextEdit(value);editor=edit;connect(edit,&QPlainTextEdit::textChanged,this,&GitCommandForm::invalidate);
    }
    editor->setEnabled(mode!=0);selected_->setCellWidget(row,1,editor);auto *use=new QCheckBox;use->setChecked(mode==1||mode==2||mode==5||mode==6);use->setEnabled(mode==3||mode==4||mode==7);selected_->setCellWidget(row,2,use);selected_->setRowHeight(row,55);
    auto *browse=new QPushButton(tr("찾기…"));browse->setObjectName("formBrowseValue");selected_->setCellWidget(row,3,browse);
    auto *edit=qobject_cast<QPlainTextEdit*>(editor);
    browse->setEnabled(edit&&mode!=0&&use->isChecked());
    connect(use,&QCheckBox::toggled,browse,[browse,edit,mode](bool enabled){browse->setEnabled(edit&&mode!=0&&enabled);});
    if(edit&&mode!=0){
        auto *menu=new QMenu(browse);browse->setMenu(menu);
        const QStringList labels{tr("파일 · 절대경로"),tr("파일 · 상대경로"),tr("폴더 · 절대경로"),tr("폴더 · 상대경로")};
        for(int kind=0;kind<4;++kind){
            auto *action=menu->addAction(labels[kind]);action->setObjectName(QString("formBrowsePath%1").arg(kind));
            connect(action,&QAction::triggered,this,[this,edit,kind]{
                const bool directory=kind>=2,relative=kind%2;
                const auto root=git_->repositoryPath();
                if(relative&&root.isEmpty()){QMessageBox::information(this,tr("경로 선택"),tr("상대경로를 지정하려면 먼저 저장소를 여세요."));return;}
                QFileDialog dialog(this,directory?tr("폴더 경로 선택"):tr("파일 경로 선택"),root);
                dialog.setObjectName("formValuePathDialog");dialog.setOption(QFileDialog::DontUseNativeDialog);dialog.setWindowFlag(Qt::WindowTitleHint);
                dialog.setFileMode(directory?QFileDialog::Directory:QFileDialog::AnyFile);
                dialog.setAcceptMode(directory?QFileDialog::AcceptOpen:QFileDialog::AcceptSave);
                dialog.setOption(QFileDialog::DontConfirmOverwrite);
                dialog.setLabelText(QFileDialog::Accept,tr("선택"));
                dialog.setOption(QFileDialog::ShowDirsOnly,directory);
                if(dialog.exec()!=QDialog::Accepted||dialog.selectedFiles().isEmpty())return;
                auto path=QDir::fromNativeSeparators(dialog.selectedFiles().first());
                if(relative){
                    path=QDir(root).relativeFilePath(path);
                    if(QDir::isAbsolutePath(path)){QMessageBox::information(this,tr("경로 선택"),tr("다른 드라이브의 경로는 절대경로로 지정하세요."));return;}
                    if(path.startsWith('-'))path="./"+path;
                }
                edit->setPlainText(path);
            });
        }
    }
    connect(use,&QCheckBox::toggled,this,&GitCommandForm::invalidate);connect(use,&QCheckBox::toggled,editor,[editor,mode](bool enabled){editor->setEnabled(mode!=0&&enabled);});editor->setEnabled(mode!=0&&use->isChecked());selected_->setCurrentCell(row,0);invalidate();
}
QString GitCommandForm::rowValue(int row) const {
    if(auto *choice=qobject_cast<QComboBox*>(selected_->cellWidget(row,1)))return choice->currentText();
    return qobject_cast<QPlainTextEdit*>(selected_->cellWidget(row,1))->toPlainText();
}
void GitCommandForm::moveRow(int delta){
    int row=selected_->currentRow(),target=row+delta;if(row<0||target<0||target>=selected_->rowCount())return;
    // Recreate the two rows so cell-widget ownership remains with the table.
    struct Row{QString name,syntax,value;int mode;bool enabled;};auto read=[this](int r){auto *item=selected_->item(r,0);return Row{item->data(Qt::UserRole).toString(),item->text(),rowValue(r),item->data(Qt::UserRole+1).toInt(),qobject_cast<QCheckBox*>(selected_->cellWidget(r,2))->isChecked()};};
    QList<Row> rows;for(int i=0;i<selected_->rowCount();++i)rows.append(read(i));rows.swapItemsAt(row,target);selected_->setRowCount(0);for(const auto &r:rows){addRow(r.name,r.syntax,r.value,r.mode);qobject_cast<QCheckBox*>(selected_->cellWidget(selected_->rowCount()-1,2))->setChecked(r.enabled);}selected_->setCurrentCell(target,0);invalidate();
}
QStringList GitCommandForm::arguments(QString *error) const{
    error->clear();QStringList args{commands_->currentData().toString()};bool separated=false;
    for(int row=0;row<selected_->rowCount();++row){const auto *item=selected_->item(row,0);const auto name=item->data(Qt::UserRole).toString();const int mode=item->data(Qt::UserRole+1).toInt();const auto value=rowValue(row);const bool use=qobject_cast<QCheckBox*>(selected_->cellWidget(row,2))->isChecked();
        if(mode==5){if(!separated&&value.startsWith('-')){*error=tr("옵션은 목록에서 선택하세요. '-'로 시작하는 경로는 경로 구분자 뒤에 넣으세요.");return {};}args.append(value);}
        else if(mode==0||!use){args.append(name);if(name=="--")separated=true;}
        else if(mode==6||mode==7){if(name=="-"&&!QRegularExpression("^[0-9]+$").match(value).hasMatch()){*error=tr("유효하지 않은 선택입니다.");return {};}args.append(name+value);}
        else if(mode==2||mode==4)args.append(name+"="+value);else args.append({name,value});
    }return args;
}
void GitCommandForm::invalidate(){review_={};assembled_.clear();if(sentence_)sentence_->clear();updateButtons();}
void GitCommandForm::compose(){
    QString error;const auto args=arguments(&error);if(!error.isEmpty()){output_->setPlainText(error);return;}
    if(args.isEmpty()||args.first().isEmpty())return;
    assembled_=args;QStringList display{"git"};
    for(const auto &arg:args){
        if(!arg.isEmpty()&&QRegularExpression("^[a-zA-Z0-9_./:=@+,-]+$").match(arg).hasMatch())display.append(arg);
        else {QJsonArray array{arg};auto json=QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));display.append(json.mid(1,json.size()-2));}
    }
    sentence_->setPlainText(display.join(' '));review_={};updateButtons();
}
void GitCommandForm::updateButtons(){const bool ready=!loading_&&!git_->isBusy();inputs_->setEnabled(ready);previewButton_->setEnabled(ready&&!git_->repositoryPath().isEmpty()&&!manual_.text.isEmpty()&&!assembled_.isEmpty());executeButton_->setEnabled(ready&&!review_.arguments.isEmpty());}
void GitCommandForm::preview(){
    QString error;const auto args=assembled_;if(args.isEmpty())return;QJsonArray array;for(const auto &arg:args)array.append(arg);loading_=true;review_={};updateButtons();
    controller_.review("expert",{{"argumentsJson",QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact))},{"stdin",stdin_->toPlainText()}},[this,array](bool ok,ToolReview review,QString error){loading_=false;if(ok){review_=review;output_->setPlainText(review.repository+"\n"+QString::fromUtf8(QJsonDocument(array).toJson())+tr("\n변경은 자동으로 되돌려지지 않습니다."));}else output_->setPlainText(error);updateButtons();});
}
void GitCommandForm::execute(){
    if(loading_||git_->isBusy()||review_.arguments.isEmpty())return;
    QMessageBox box(QMessageBox::Warning,tr("Git 작업 확인"),tr("선택한 옵션과 실행 순서를 확인하세요. 모든 옵션 조합에 대한 파일 백업이나 자동 복구를 보장하지 않습니다."),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setDetailedText(output_->toPlainText());box.setDefaultButton(QMessageBox::No);if(box.exec()!=QMessageBox::Yes)return;
    loading_=true;updateButtons();controller_.execute(review_,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;review_={};output_->setPlainText(QString::fromUtf8(out)+"\n"+error+"\n"+(ok?tr("작업을 완료했습니다."):tr("작업 실패. 일부 변경이 남았을 수 있습니다. 상태를 확인하세요.")));updateButtons();emit repositoryChanged();});
}
void GitCommandForm::reject(){if(!loading_&&!git_->isBusy())QDialog::reject();}
void GitCommandForm::closeEvent(QCloseEvent *event){if(loading_||git_->isBusy())event->ignore();else event->accept();}
