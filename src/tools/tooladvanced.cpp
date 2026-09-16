#include "gittoolbox.h"
#include <QDir>

QList<GitTool> ToolboxController::advancedCatalog(){
    const ToolField ref{"ref",tr("커밋 / 브랜치"),"HEAD"},path{"path",tr("파일 / 폴더 경로"),{}},remote{"remote",tr("원격 이름"),"origin"};
    const ToolField scope{"scope",tr("설정 범위"),"local",{"local","global","system","worktree"}},key{"key",tr("설정 키"),"gitcanvas.example"};
    const ToolField note{"notes",tr("Notes 참조"),"refs/notes/commits"},message{"message",tr("설명"),{}};
    return {
        {"notes.list", "Notes",tr("커밋 메모 목록"),tr("커밋 객체를 바꾸지 않고 별도 참조에 저장한 메모를 조회합니다."),{note},true},
        {"notes.show", "Notes",tr("커밋 메모 보기"),tr("선택한 커밋에 연결된 메모를 읽습니다."),{note,ref},true},
        {"notes.add", "Notes",tr("커밋 메모 추가"),tr("기존 메모를 덮어쓰지 않고 새 메모를 추가합니다. 메모의 변경은 별도 Notes 이력에 기록됩니다."),{note,ref,message}},
        {"notes.append", "Notes",tr("커밋 메모 이어 쓰기"),tr("선택한 커밋의 기존 메모 뒤에 내용을 추가합니다."),{note,ref,message}},
        {"notes.copy", "Notes",tr("커밋 메모 복사"),tr("원본 커밋의 메모를 다른 커밋에 복사합니다. 기존 대상 메모는 보호합니다."),{note,ref,{"target",tr("대상 커밋"),{}}}},
        {"notes.remove", "Notes",tr("커밋 메모 제거"),tr("선택한 커밋의 메모 연결을 제거합니다. Notes 참조의 기존 이력을 복구 참조에 보관합니다."),{note,ref}},
        {"replace.list","Replace",tr("객체 대체 목록"),tr("원본 객체 대신 읽을 대체 객체의 연결을 조회합니다."),{},true},
        {"replace.create","Replace",tr("객체 대체 연결"),tr("원본 객체를 변경하지 않고 읽기 시 사용할 대체 객체를 지정합니다. 이 연결은 Git 이력 조회와 후속 작업에 영향을 줍니다."),{{"ref",tr("원본 객체"),"HEAD"},{"target",tr("대체 객체"),{}}}},
        {"replace.delete","Replace",tr("객체 대체 해제"),tr("대체 참조를 백업한 후 연결을 제거합니다. 원본 객체는 유지됩니다."),{{"ref",tr("원본 객체"),"HEAD"}}},
        {"config.values",tr("Git 설정"),tr("설정의 모든 값 조회"),tr("선택한 범위에서 같은 키에 저장된 값들을 조회합니다."),{scope,key},true},
        {"config.add",tr("Git 설정"),tr("설정 값 추가"),tr("기존 값을 유지하고 같은 키에 값을 하나 더 저장합니다."),{scope,key,{"value",tr("값 (빈 문자열 허용)"),{}}}},
        {"config.remove-value",tr("Git 설정"),tr("일치하는 설정 값 제거"),tr("정규식이 아닌 문자열로 정확히 일치하는 값만 제거합니다. 같은 값이 여러 번 있으면 모두 제거합니다."),{scope,key,{"value",tr("값 (빈 문자열 허용)"),{}}}},
        {"sparse.list","Sparse checkout",tr("선택 폴더 조회"),tr("현재 작업 폴더에 펼치도록 지정한 경로를 조회합니다."),{},true},
        {"sparse.set","Sparse checkout",tr("작업 폴더 선택"),tr("Cone 모드로 선택한 폴더만 펼칩니다. 제외된 추적 파일은 작업 폴더에서 사라지지만 Git 이력에 유지됩니다. 루트 파일은 항상 포함됩니다."),{{"paths",tr("폴더 경로 · 한 줄에 하나"),{}}},false,true},
        {"sparse.add","Sparse checkout",tr("선택 폴더 추가"),tr("기존 Sparse 선택에 폴더를 더합니다. Cone 모드에서 사용하세요."),{{"paths",tr("폴더 경로 · 한 줄에 하나"),{}}},false,true},
        {"sparse.disable","Sparse checkout",tr("전체 파일 다시 펼치기"),tr("Sparse checkout을 해제하고 추적 파일 전체를 작업 폴더로 복원합니다."),{},false,true},
        {"sparse.reapply","Sparse checkout",tr("폴더 선택 다시 적용"),tr("현재 Sparse 규칙을 작업 폴더에 다시 적용합니다."),{},false,true},
        {"worktree.detached",tr("연결 작업 트리"),tr("분리 HEAD 작업 트리 생성"),tr("새 폴더에서 브랜치에 연결되지 않은 커밋을 엽니다. 새 커밋을 보관하려면 그 폴더에서 브랜치를 만드세요."),{path,ref}},
        {"worktree.repair",tr("연결 작업 트리"),tr("이동된 작업 트리 연결 복구"),tr("외부에서 이동한 작업 폴더와 공통 저장소 사이의 관리 경로를 복구합니다. 파일 내용이나 커밋을 복원하는 기능은 아닙니다."),{path}},
        {"submodule.add",tr("Submodule · LFS"),tr("Submodule 추가"),tr("하위 저장소를 새 상대 경로로 복제하고 .gitmodules와 Index에 등록합니다. 변경 내용을 검토한 뒤 커밋하세요."),{{"url",tr("저장소 URL"),{}},path},false,true},
        {"submodule.deinit",tr("Submodule · LFS"),tr("Submodule 작업 폴더 비우기"),tr("하위 저장소의 로컬 등록을 해제하고 작업 폴더를 비웁니다. 변경·미추적·무시 파일을 먼저 검사하며 force를 사용하지 않습니다."),{path},false,true},
        {"submodule.set-url",tr("Submodule · LFS"),tr("Submodule URL 변경"),tr(".gitmodules의 주소와 하위 저장소 설정을 갱신합니다. 기존 커밋은 이동하지 않습니다."),{path,{"url",tr("저장소 URL"),{}}}},
        {"submodule.set-branch",tr("Submodule · LFS"),tr("Submodule 추적 브랜치 변경"),tr("update --remote에서 사용할 브랜치를 .gitmodules에 지정합니다. 파일을 즉시 전환하지 않습니다."),{path,{"name",tr("이름"),{}}}},
        {"submodule.summary",tr("Submodule · LFS"),tr("Submodule 커밋 차이"),tr("기록된 하위 저장소 커밋과 현재 커밋의 차이를 조회합니다."),{},true},
        {"lfs.env",tr("Submodule · LFS"),tr("LFS 환경 진단"),tr("LFS endpoint와 설정을 조회합니다. 공유 전에 민감정보를 확인하세요."),{},true},
        {"lfs.files",tr("Submodule · LFS"),tr("LFS 파일 목록"),tr("LFS로 추적하는 파일과 객체 ID를 조회합니다."),{},true},
        {"lfs.locks",tr("Submodule · LFS"),tr("LFS 잠금 목록"),tr("서버에 등록된 파일 잠금과 소유자를 조회합니다. 서버 인증이 필요할 수 있습니다."),{},true},
        {"lfs.lock",tr("Submodule · LFS"),tr("LFS 파일 잠금"),tr("지원하는 서버에서 파일의 편집 잠금을 요청합니다. 다른 사용자에게 영향을 주며 자동으로 해제하지 않습니다."),{path}},
        {"lfs.unlock",tr("Submodule · LFS"),tr("LFS 파일 잠금 해제"),tr("내 파일 잠금을 서버에서 해제합니다. 다른 사용자의 잠금을 강제로 해제하지 않습니다."),{path}},
        {"lfs.fetch",tr("Submodule · LFS"),tr("LFS 객체 가져오기"),tr("선택한 참조의 LFS 객체를 내려받습니다. 작업 파일과 Git 브랜치는 바꾸지 않습니다."),{remote,ref}},
        {"lfs.push",tr("Submodule · LFS"),tr("LFS 객체 게시"),tr("선택한 참조의 LFS 객체를 업로드합니다. Git 브랜치를 Push하는 기능은 아닙니다."),{remote,ref}},
        {"lfs.prune-check",tr("Submodule · LFS"),tr("LFS 정리 대상 확인"),tr("삭제할 수 있는 로컬 LFS 객체를 미리 봅니다. 실제 삭제는 수행하지 않습니다."),{},true},
        {"lfs.migrate-info",tr("Submodule · LFS"),tr("LFS 이력 변환 분석"),tr("전체 참조의 파일 유형과 용량을 분석합니다. 이력을 변환하거나 서버에서 Fetch하지 않습니다."),{},true},
        {"tag.sign",tr("태그"),tr("서명 태그 생성"),tr("명시한 서명 키로 주석 태그를 생성합니다. Git의 서명 프로그램과 키가 준비되어 있어야 합니다."),{{"name",tr("이름"),{}},ref,message,{"signingKey",tr("서명 키"),{}}}},
        {"tag.verify",tr("태그"),tr("태그 서명 검증"),tr("서명 프로그램으로 태그의 서명을 검증합니다. 신뢰 수준은 로컬 키 설정에 따릅니다."),{{"name",tr("이름"),{}}},true},
        {"commit.verify",tr("태그"),tr("커밋 서명 검증"),tr("선택한 커밋의 서명을 검증합니다."),{ref},true},
        {"rerere.status","Rerere",tr("기억 중인 충돌 경로"),tr("Git이 해결 방법을 기록하고 있는 충돌 경로를 조회합니다."),{},true},
        {"rerere.diff","Rerere",tr("충돌 해결 기록 비교"),tr("기록 중인 충돌과 현재 해결 결과의 차이를 조회합니다."),{},true},
        {"rerere.remaining","Rerere",tr("남은 충돌 기록"),tr("자동 재사용으로 해결되지 않은 경로를 조회합니다."),{},true},
        {"health.graph-verify",tr("점검 · 정리"),tr("Commit graph 점검"),tr("커밋 그래프 캐시의 무결성을 검사합니다."),{},true},
        {"health.graph-write",tr("점검 · 정리"),tr("Commit graph 생성"),tr("참조 가능한 커밋의 그래프 캐시를 생성합니다. 커밋 이력은 바꾸지 않습니다."),{}},
        {"health.pack-refs",tr("점검 · 정리"),tr("참조 압축"),tr("참조를 packed-refs로 모읍니다. 브랜치가 가리키는 커밋은 바꾸지 않습니다."),{}},
    };
}
QStringList ToolboxController::advancedArguments(const QString &id,const QMap<QString,QString> &v,QString *error){
    const auto path=v.value("path"),ref=v.value("ref");
    if(id.startsWith("notes.")){QStringList args{"notes","--ref="+v.value("notes"),id.section('.',1)};if(id=="notes.add"||id=="notes.append")args.append({"-m",v.value("message")});if(id!="notes.list")args.append(ref);if(id=="notes.copy")args.append(v.value("target"));return args;}
    if(id=="replace.list")return {"replace","-l","--format=long"};if(id=="replace.create")return {"replace",ref,v.value("target")};if(id=="replace.delete")return {"replace","-d",ref};
    if(id=="config.values")return {"config","--"+v.value("scope"),"--get-all",v.value("key")};
    if(id=="config.add")return {"config","--"+v.value("scope"),"--add",v.value("key"),v.value("value")};
    if(id=="config.remove-value")return {"config","--"+v.value("scope"),"--fixed-value","--unset-all",v.value("key"),v.value("value")};
    if(id.startsWith("sparse.")){
        QStringList args{"sparse-checkout",id.section('.',1)};
        if(id=="sparse.set"||id=="sparse.add"){
            if(id=="sparse.set")args.append("--cone");args.append("--");
            for(auto p:v.value("paths").split('\n',Qt::SkipEmptyParts)){if(p.endsWith('\r'))p.chop(1);p=QDir::fromNativeSeparators(p);if(QDir::isAbsolutePath(p)||p.startsWith('-')||p.split('/').contains("..")||p.split('/').contains(".git",Qt::CaseInsensitive)){*error=tr("저장소 안의 상대 폴더 경로를 입력하세요.");return {};}args.append(p);}
            if(args.last()=="--"){*error=tr("저장소 안의 상대 폴더 경로를 입력하세요.");return {};}
        }return args;
    }
    if(id=="worktree.detached")return {"worktree","add","--detach",path,ref};if(id=="worktree.repair")return {"worktree","repair",path};
    if(id=="submodule.add")return {"submodule","add","--",v.value("url"),path};
    if(id=="submodule.deinit")return {"submodule","deinit","--",path};
    if(id=="submodule.set-url")return {"submodule","set-url","--",path,v.value("url")};
    if(id=="submodule.set-branch")return {"submodule","set-branch","--branch",v.value("name"),"--",path};
    if(id=="submodule.summary")return {"submodule","summary","--files"};
    if(id=="lfs.files")return {"lfs","ls-files","--long"};if(id=="lfs.lock"||id=="lfs.unlock")return {"lfs",id.section('.',1),"--",path};
    if(id=="lfs.fetch"||id=="lfs.push")return {"lfs",id.section('.',1),v.value("remote"),ref};
    if(id=="lfs.prune-check")return {"lfs","prune","--dry-run","--verbose"};if(id=="lfs.migrate-info")return {"lfs","migrate","info","--everything","--skip-fetch"};
    if(id=="tag.sign")return {"tag","-s","-u",v.value("signingKey"),"-m",v.value("message"),v.value("name"),ref};if(id=="tag.verify")return {"verify-tag",v.value("name")};if(id=="commit.verify")return {"verify-commit",ref};
    if(id.startsWith("rerere."))return {"rerere",id.section('.',1)};
    if(id=="health.graph-verify")return {"commit-graph","verify"};if(id=="health.graph-write")return {"commit-graph","write","--reachable"};if(id=="health.pack-refs")return {"pack-refs","--all"};
    return {};
}
