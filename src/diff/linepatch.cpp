#include "diffwidget.h"
#include <QRegularExpression>

QByteArray DiffWidget::selectedLinesPatch(const QByteArray &patch,const QSet<int> &selected,bool reverse) {
    const auto lines=patch.split('\n');QByteArray diff,oldPath,newPath,mode="100644",body,result;int oldStart=0,oldCount=0,newCount=0,delta=0;bool changed=false,kept=false,inHunk=false,all=true;
    QRegularExpression header("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@");
    auto flush=[&]{if(changed){int start=oldStart+delta;if(oldCount==0&&newCount>0)++start;if(newCount==0&&oldCount>0)--start;result+="@@ -"+QByteArray::number(oldStart)+","+QByteArray::number(oldCount)+" +"+QByteArray::number(start)+","+QByteArray::number(newCount)+" @@\n"+body;delta+=newCount-oldCount;}body.clear();oldCount=newCount=0;changed=false;};
    for(int i=0;i<lines.size();++i){auto line=lines[i];const auto match=header.match(QString::fromLatin1(line));
        if(match.hasMatch()){flush();oldStart=match.captured(reverse?3:1).toInt();inHunk=true;kept=false;continue;}
        if(!inHunk){if(line.startsWith("diff --git "))diff=line;else if(line.startsWith("--- "))oldPath=line.mid(4);else if(line.startsWith("+++ "))newPath=line.mid(4);else if(line.startsWith("new file mode ")||line.startsWith("deleted file mode "))mode=line.mid(line.lastIndexOf(' ')+1);continue;}
        if(line.startsWith("\\ ")){if(kept)body+=line+'\n';continue;}
        if(line.isEmpty())continue;char sign=line[0];if(reverse){if(sign=='+')sign='-';else if(sign=='-')sign='+';}
        const bool pick=selected.contains(i);if((sign=='+'||sign=='-')&&!pick)all=false;
        kept=true;
        if(sign==' '||(sign=='-'&&!pick)){body+=' '+line.mid(1)+'\n';++oldCount;++newCount;}
        else if(sign=='-'&&pick){body+='-'+line.mid(1)+'\n';++oldCount;changed=true;}
        else if(sign=='+'&&pick){body+='+'+line.mid(1)+'\n';++newCount;changed=true;}
        else kept=false;
    }flush();if(result.isEmpty())return {};
    if(reverse)std::swap(oldPath,newPath);
    QByteArray fileHeader=diff+'\n';if(oldPath=="/dev/null")fileHeader+="new file mode "+mode+'\n';
    if(newPath=="/dev/null"){if(all)fileHeader+="deleted file mode "+mode+'\n';else newPath=oldPath;}
    return fileHeader+"--- "+oldPath+"\n+++ "+newPath+'\n'+result;
}
