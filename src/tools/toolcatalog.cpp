#include "gittoolbox.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>

QList<GitTool> ToolboxController::catalog(){
    using F=ToolField;const F ref{"ref",tr("커밋 / 브랜치"),"HEAD"},name{"name",tr("이름"),{}},path{"path",tr("파일 / 폴더 경로"),{}},remote{"remote",tr("원격 이름"),"origin"};
    const F scope{"scope",tr("설정 범위"),"local",{"local","global","system","worktree"}},key{"key",tr("설정 키"),"core.autocrlf"};
    return QList<GitTool>{
        {"tag.list",tr("태그"),tr("태그 목록"),tr("이름·객체 ID·유형·설명을 조회합니다. 행을 선택하면 이름을 복사할 수 있습니다."),{},true},
        {"tag.create",tr("태그"),tr("가벼운 태그 생성"),tr("커밋에 이름을 붙입니다. 같은 이름을 덮어쓰지 않습니다."),{name,ref}},
        {"tag.annotate",tr("태그"),tr("주석 태그 생성"),tr("작성자·메시지를 포함한 태그를 만듭니다. 서명 옵션은 고급 실행에서 지정합니다."),{name,ref,{"message",tr("태그 설명"),{}}}},
        {"tag.delete",tr("태그"),tr("로컬 태그 삭제"),tr("태그 객체를 앱 복구 참조에 보관한 뒤 로컬 이름을 제거합니다. 원격 태그는 유지됩니다."),{name}},
        {"tag.remote-list",tr("태그"),tr("원격 태그 조회"),tr("서버의 태그 이름과 ID를 읽습니다. 로컬 태그는 바뀌지 않습니다."),{remote},true},
        {"tag.publish",tr("태그"),tr("원격에 태그 게시"),tr("미리보기에서 먼저 Fetch하고 서버와 비교합니다. 새 태그만 게시하며 기존 이름을 덮어쓰지 않습니다."),{remote,name}},
        {"tag.remote-delete",tr("태그"),tr("원격 태그 삭제"),tr("미리보기에서 Fetch·서버 ID 확인 후 그 ID가 유지된 경우에만 삭제합니다. 다른 사용자의 태그에도 영향을 줍니다."),{remote,name}},
        {"worktree.list",tr("연결 작업 트리"),tr("작업 트리 목록"),tr("한 저장소의 별도 작업 폴더를 조회합니다. 선택한 폴더를 Workspace에서 열 수 있습니다."),{},true},
        {"worktree.add",tr("연결 작업 트리"),tr("새 브랜치와 작업 트리 생성"),tr("새 폴더에서 별도 브랜치를 작업합니다. 기존 폴더를 덮어쓰지 않습니다."),{path,name,ref}},
        {"worktree.attach",tr("연결 작업 트리"),tr("기존 브랜치의 작업 트리 생성"),tr("다른 작업 트리에서 사용 중인 브랜치는 Git이 거부합니다."),{path,{"name",tr("기존 로컬 브랜치"),{}}}},
        {"worktree.remove",tr("연결 작업 트리"),tr("작업 트리 제거"),tr("등록된 작업 폴더를 삭제합니다. 현재 폴더·주 작업 트리·변경/미추적 파일·잠긴 작업 트리는 제거하지 않습니다. --force는 사용하지 않습니다."),{path}},
        {"worktree.move",tr("연결 작업 트리"),tr("작업 트리 이동"),tr("등록된 별도 작업 폴더를 새 경로로 이동합니다. 현재 열려 있는 작업 트리는 대상에서 제외합니다."),{path,{"destination",tr("새 폴더 경로"),{}}}},
        {"worktree.lock",tr("연결 작업 트리"),tr("작업 트리 잠금"),tr("일시적으로 연결되지 않는 폴더의 등록 정보가 정리되지 않도록 보호합니다."),{path,{"message",tr("잠금 이유"),{}}}},
        {"worktree.unlock",tr("연결 작업 트리"),tr("작업 트리 잠금 해제"),tr("Git worktree의 관리용 잠금을 해제합니다. index.lock 같은 실행 잠금을 지우는 기능은 아닙니다."),{path}},
        {"worktree.prune-check",tr("연결 작업 트리"),tr("오래된 등록 정리 미리보기"),tr("3개월 이상 지난 사라진 작업 트리의 등록 정리 대상을 표시합니다."),{},true},
        {"worktree.prune",tr("연결 작업 트리"),tr("오래된 등록 정리"),tr("3개월 이상 지난 사라진 폴더의 등록 정보를 정리합니다. 실제 작업 폴더는 삭제하지 않습니다."),{}},
        {"bisect.start",tr("문제 커밋 찾기"),tr("Bisect 시작"),tr("정상·문제 커밋 사이를 이분 탐색합니다. 파일을 자동 전환하지 않는 --no-checkout 모드이며 검사 대상은 BISECT_HEAD입니다."),{{"bad",tr("문제가 있는 커밋"),"HEAD"},{"good",tr("정상 커밋"),{}}},false,true},
        {"bisect.show",tr("문제 커밋 찾기"),tr("검사 대상 커밋 보기"),tr("BISECT_HEAD의 메시지와 변경 내용을 조회합니다. 파일은 유지됩니다."),{},true},
        {"bisect.good",tr("문제 커밋 찾기"),tr("현재 검사 대상: 정상"),tr("BISECT_HEAD를 정상으로 기록하고 다음 후보를 고릅니다."),{}},
        {"bisect.bad",tr("문제 커밋 찾기"),tr("현재 검사 대상: 문제 있음"),tr("BISECT_HEAD를 문제 있음으로 기록합니다. 마지막에는 최초 문제 커밋을 출력합니다."),{}},
        {"bisect.skip",tr("문제 커밋 찾기"),tr("현재 검사 대상: 검사 불가"),tr("현재 후보를 제외합니다. 제외한 커밋 때문에 답이 하나로 좁혀지지 않을 수 있습니다."),{}},
        {"bisect.log",tr("문제 커밋 찾기"),tr("Bisect 판단 기록"),tr("시작 범위와 정상·문제·제외 판단을 조회합니다."),{},true},
        {"bisect.reset",tr("문제 커밋 찾기"),tr("Bisect 종료"),tr("현재 탐색 상태를 종료합니다. 이 화면의 no-checkout 모드에서는 작업 파일이 유지됩니다."),{}},
        {"health.objects",tr("점검 · 정리"),tr("객체 크기 조회"),tr("느슨한 객체·팩 파일 개수와 디스크 사용량을 조회합니다."),{},true},
        {"health.fsck",tr("점검 · 정리"),tr("저장소 무결성 점검"),tr("객체와 연결 관계를 검사합니다. dangling 객체는 반드시 손상이라는 뜻은 아닙니다. 오류가 나면 복사본을 확보하고 진단하세요."),{},true},
        {"health.gc",tr("점검 · 정리"),tr("기본 Garbage Collection"),tr("객체를 압축하고 Git의 기본 만료 정책으로 정리합니다. 만료된 미참조 객체는 복구할 수 없을 수 있습니다. prune=now는 사용하지 않습니다."),{}},
        {"health.repack",tr("점검 · 정리"),tr("객체 다시 압축"),tr("참조 가능한 객체를 새 팩으로 묶고 중복된 기존 팩을 정리합니다. 실행 중 강제 중단하지 않습니다."),{}},
        {"health.maintenance",tr("점검 · 정리"),tr("유지보수 작업 실행"),tr("선택한 유지보수를 한 번 실행합니다. 백그라운드 스케줄 등록은 하지 않습니다."),{{"task",tr("작업"),"commit-graph",{"commit-graph","loose-objects","incremental-repack","gc"}}}},
        {"config.list",tr("Git 설정"),tr("설정과 출처 조회"),tr("유효한 설정·범위·파일 출처를 표시합니다. 비밀 값이 있을 수 있으므로 결과 공유 시 확인하세요."),{},true},
        {"config.set",tr("Git 설정"),tr("설정 값 저장"),tr("Local은 현재 저장소, Global은 사용자 전체, System은 시스템 전체에 적용됩니다. Worktree는 extensions.worktreeConfig를 먼저 설정해야 합니다. 기존 동일 키 값은 교체됩니다."),{scope,key,{"value",tr("값 (빈 문자열 허용)"),{}}}},
        {"config.unset",tr("Git 설정"),tr("설정 키 제거"),tr("선택 범위에서 해당 키의 모든 값을 제거합니다. 상위 범위의 값이 다시 적용될 수 있습니다."),{scope,key}},
        {"export.archive",tr("내보내기 · 패치"),tr("커밋 파일 ZIP 내보내기"),tr("선택 커밋의 파일을 새 ZIP으로 내보냅니다. .git과 미커밋 파일은 포함하지 않습니다."),{ref,{"path",tr("새 ZIP 파일 경로"),{}}}},
        {"export.patch",tr("내보내기 · 패치"),tr("커밋 패치 파일 생성"),tr("지정 범위를 새 폴더에 format-patch로 내보냅니다. 예: HEAD~3..HEAD. 메일은 전송하지 않습니다."),{{"range",tr("커밋 범위"),"HEAD~1..HEAD"},{"path",tr("새 출력 폴더"),{}}}},
        {"export.bundle",tr("내보내기 · 패치"),tr("저장소 Bundle 생성"),tr("모든 참조의 Git 이력을 새 bundle 파일에 보관합니다. 미커밋 파일·Git 설정·LFS 객체는 포함하지 않습니다."),{{"path",tr("새 Bundle 파일"),{}}}},
        {"bundle.verify",tr("내보내기 · 패치"),tr("Bundle 확인"),tr("Bundle 헤더·전제 커밋 등 사용 가능 여부를 검사합니다."),{path},true},
        {"bundle.fetch",tr("내보내기 · 패치"),tr("Bundle 이력 가져오기"),tr("Bundle의 참조를 FETCH_HEAD로 읽습니다. 현재 브랜치·파일은 전환하지 않습니다."),{path,ref}},
        {"patch.check",tr("내보내기 · 패치"),tr("패치 적용 가능 여부"),tr("현재 작업 파일에 패치를 적용할 수 있는지만 검사합니다."),{path},true},
        {"patch.apply",tr("내보내기 · 패치"),tr("패치 적용·Stage"),tr("깨끗한 작업 트리에서 apply --check 후 --index로 적용합니다. 커밋은 만들지 않습니다. 패치 경로의 무시 파일 덮어쓰기는 Git이 거부합니다."),{path},false,true},
        {"patch.am",tr("내보내기 · 패치"),tr("메일 패치로 커밋 생성"),tr("format-patch 파일을 작성자·메시지와 함께 커밋으로 적용합니다. 실패 시 작업 상태에서 AM Continue/Abort를 사용합니다."),{path},false,true},
        {"submodule.status",tr("Submodule · LFS"),tr("Submodule 상태"),tr("하위 저장소의 체크아웃 상태와 기록된 커밋을 조회합니다."),{},true},
        {"submodule.update",tr("Submodule · LFS"),tr("Submodule 초기화·갱신"),tr("등록된 하위 저장소를 재귀적으로 가져와 기록된 커밋으로 전환합니다. 네트워크와 인증이 필요할 수 있습니다. 강제 옵션은 사용하지 않습니다."),{},false,true},
        {"submodule.sync",tr("Submodule · LFS"),tr("Submodule URL 동기화"),tr(".gitmodules에 기록된 URL을 로컬 하위 저장소 설정에 반영합니다."),{}},
        {"lfs.version",tr("Submodule · LFS"),tr("Git LFS 설치 확인"),tr("별도로 설치된 Git LFS의 버전을 확인합니다. 이 앱이 LFS를 설치하지는 않습니다."),{},true},
        {"lfs.status",tr("Submodule · LFS"),tr("LFS 상태"),tr("LFS 파일의 변경 상태를 조회합니다."),{},true},
        {"lfs.track",tr("Submodule · LFS"),tr("LFS 패턴 등록"),tr("예: *.psd. .gitattributes를 수정합니다. 이후 변경 사항 화면에서 검토·Stage·커밋하세요."),{{"pattern",tr("파일 패턴"),{}}}},
        {"lfs.untrack",tr("Submodule · LFS"),tr("LFS 패턴 해제"),tr("추적 패턴을 제거합니다. 과거 커밋이나 이미 저장된 LFS 객체를 변환하지 않습니다."),{{"pattern",tr("파일 패턴"),{}}}},
        {"lfs.pull",tr("Submodule · LFS"),tr("LFS 파일 받기"),tr("현재 체크아웃에 필요한 LFS 객체를 다운로드합니다. Git 브랜치 Pull과는 별도입니다."),{},false,true},
        {"commands",tr("고급 명령"),tr("설치된 Git 명령 탐색"),tr("현재 설치된 Git의 내장 명령과 확장 명령·별칭 이름을 조회합니다. 지원 옵션은 사용 중인 Git 버전에 따릅니다."),{},true},
        {"expert",tr("고급 명령"),tr("인자 단위 Git 실행"),tr("내장 명령 또는 lfs를 한 줄에 인자 하나씩 입력합니다. 첫 줄에 git 자체는 쓰지 않습니다. 공백·따옴표는 그대로 전달하며 셸 문법은 해석하지 않습니다. 옵션별 영향 검증·파일 백업을 보장하지 않습니다. Hook과 일부 Git 기능은 외부 프로그램을 실행할 수 있습니다. 대화형 입력은 지원하지 않습니다. Push/Pull은 상단 동기화, 원격 태그는 전용 항목을 사용하세요."),{{"arguments",tr("Git 인자 (한 줄에 하나)"),"status\n--short"},{"stdin",tr("표준 입력 · UTF-8 텍스트 (선택)"),{}}}}
    } + advancedCatalog();
}
QStringList ToolboxController::arguments(const QString &id,const QMap<QString,QString> &v,QString *error){
    error->clear();
    for(const auto &value:v)if(value.contains(QChar(0))||value.toUtf8().size()>8*1024*1024){*error=tr("입력은 NUL이 없는 8 MiB 이하 텍스트여야 합니다.");return {};}
    auto get=[&](const char *key){return v.value(key);};const auto name=get("name"),ref=get("ref"),path=get("path");
    if(id=="expert"){
        auto args=get("arguments").split('\n');for(auto &arg:args)if(arg.endsWith('\r'))arg.chop(1);while(!args.isEmpty()&&args.last().isEmpty())args.removeLast();
        if(v.contains("argumentsJson")){args.clear();const auto doc=QJsonDocument::fromJson(get("argumentsJson").toUtf8());if(!doc.isArray()){*error=tr("유효하지 않은 선택입니다.");return {};}for(const auto &arg:doc.array()){if(!arg.isString()||arg.toString().contains(QChar(0))){*error=tr("유효하지 않은 선택입니다.");return {};}args.append(arg.toString());}}
        if(args.isEmpty()||!QRegularExpression("^[a-z][a-z0-9-]*$").match(args.first()).hasMatch()){*error=tr("첫 줄에는 git을 제외한 명령 이름을 입력하세요.");return {};}
        if(QStringList{"push","pull","gui","citool","daemon","credential","receive-pack","upload-pack","upload-archive","shell","http-backend"}.contains(args.first())){*error=tr("이 명령은 전용 동기화 화면 또는 외부 터미널에서 실행하세요.");return {};}
        const bool interactiveCommand=QStringList{"add","reset","checkout","restore","stash","clean","rebase","am"}.contains(args.first());
        if(args.contains("--interactive")||(interactiveCommand&&(args.contains("-i")||args.contains("--patch")||args.contains("-p")))||args.contains("--edit")){*error=tr("대화형·외부 편집 모드는 이 실행 화면에서 지원하지 않습니다.");return {};}
        return args;
    }
    const auto definitions=catalog();auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto &d){return d.id==id;});if(found==definitions.end()){*error=tr("알 수 없는 기능입니다.");return {};}
    for(const auto &field:found->fields){const auto value=v.value(field.key);if(value.isEmpty()&&field.key!="value"){*error=tr("%1을 입력하세요.").arg(field.label);return {};}
        if(!field.choices.isEmpty()&&!field.choices.contains(value)){*error=tr("유효하지 않은 선택입니다.");return {};}
        if(field.key!="value"&&field.key!="message"&&value.startsWith('-')){*error=tr("이름·참조·경로는 '-'로 시작할 수 없습니다. 경로는 절대 경로로 입력하세요.");return {};}}
    auto advanced=advancedArguments(id,v,error);if(!advanced.isEmpty()||!error->isEmpty())return advanced;
    if(id=="tag.list")return {"for-each-ref","--sort=refname","--format=%(refname:short)%09%(objectname)%09%(objecttype)%09%(subject)","refs/tags"};
    if(id=="tag.create")return {"tag",name,ref};if(id=="tag.annotate")return {"tag","-a",name,"-m",get("message"),ref};if(id=="tag.delete")return {"tag","-d",name};
    if(id=="tag.remote-list")return {"ls-remote","--tags","--refs",get("remote")};
    if(id=="tag.publish"||id=="tag.remote-delete")return {"push",get("remote"),id=="tag.publish"?"refs/tags/"+name+":refs/tags/"+name:":refs/tags/"+name};
    if(id=="worktree.list")return {"worktree","list","--porcelain","-z"};
    if(id=="worktree.add")return {"worktree","add","-b",name,path,ref};if(id=="worktree.attach")return {"worktree","add",path,name};
    if(id=="worktree.remove")return {"worktree","remove",path};if(id=="worktree.move")return {"worktree","move",path,get("destination")};
    if(id=="worktree.lock")return {"worktree","lock","--reason",get("message"),path};if(id=="worktree.unlock")return {"worktree","unlock",path};
    if(id=="worktree.prune-check")return {"worktree","prune","--dry-run","--verbose","--expire=3.months.ago"};if(id=="worktree.prune")return {"worktree","prune","--verbose","--expire=3.months.ago"};
    if(id=="bisect.start")return {"bisect","start","--no-checkout",get("bad"),get("good")};if(id=="bisect.show")return {"show","--no-ext-diff","--no-textconv","--stat","--patch","BISECT_HEAD"};
    if(id.startsWith("bisect."))return {"bisect",id.section('.',1)};
    if(id=="health.objects")return {"count-objects","-vH"};if(id=="health.fsck")return {"fsck","--full"};if(id=="health.gc")return {"gc"};if(id=="health.repack")return {"repack","-a","-d"};if(id=="health.maintenance")return {"maintenance","run","--task="+get("task")};
    if(id=="config.list")return {"config","--show-origin","--show-scope","--list"};if(id=="config.set")return {"config","--"+get("scope"),"--replace-all",get("key"),get("value")};if(id=="config.unset")return {"config","--"+get("scope"),"--unset-all",get("key")};
    if(id=="export.archive")return {"archive","--format=zip","--output="+path,ref};if(id=="export.patch")return {"format-patch","--output-directory="+path,get("range")};if(id=="export.bundle")return {"bundle","create",path,"--all"};
    if(id=="bundle.verify")return {"bundle","verify",path};if(id=="bundle.fetch")return {"fetch","--no-tags",path,ref};
    if(id=="patch.check")return {"apply","--check","--",path};if(id=="patch.apply")return {"apply","--index","--",path};if(id=="patch.am")return {"am","--3way","--",path};
    if(id=="submodule.status")return {"submodule","status","--recursive"};if(id=="submodule.update")return {"submodule","update","--init","--recursive","--checkout"};if(id=="submodule.sync")return {"submodule","sync","--recursive"};
    if(id=="lfs.track"||id=="lfs.untrack")return {"lfs",id.section('.',1),"--",get("pattern")};if(id.startsWith("lfs."))return {"lfs",id.section('.',1)};
    if(id=="commands")return {"--list-cmds=main,others,alias"};return advancedArguments(id,v,error);
}
