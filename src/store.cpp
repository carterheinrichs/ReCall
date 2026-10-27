#include "store.h"
#include <sqlite3.h>
#include <algorithm>
#include <set>

namespace recall {
namespace {
struct Statement {
    sqlite3_stmt* p{};
    Statement(sqlite3* db,const char* sql) {
        if(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(p); }
    void text(int index,const std::string& s) {
        if(sqlite3_bind_text(p,index,s.c_str(),static_cast<int>(s.size()),SQLITE_TRANSIENT)!=SQLITE_OK) throw std::runtime_error("SQLite bind failed");
    }
    void done() { if(sqlite3_step(p)!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(p))); }
};
std::string column(sqlite3_stmt* s,int i) {
    auto p=sqlite3_column_text(s,i); return p?std::string(reinterpret_cast<const char*>(p),sqlite3_column_bytes(s,i)):std::string{};
}
}
void Store::exec(const char* sql) {
    if(sqlite3_exec(db_,sql,nullptr,nullptr,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
}
Store::Store(const fs::path& state,unsigned limit):media_(state/L"clips_media"),limit_(limit) {
    const auto path=utf8((state/L"recall.db").wstring());
    if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
        std::string error=db_?sqlite3_errmsg(db_):"Cannot open database"; sqlite3_close(db_); db_=nullptr; throw std::runtime_error(error);
    }
    try {
        sqlite3_busy_timeout(db_,3000);
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
             "CREATE TABLE IF NOT EXISTS clipboard_items (id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "type TEXT NOT NULL CHECK(type IN ('text','image')), content TEXT, char_count INTEGER DEFAULT 0,"
             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);"
             "CREATE INDEX IF NOT EXISTS idx_clipboard_created ON clipboard_items(created_at DESC);");
        // Reclaim PNGs left behind by a crash between disk capture and DB insertion/retention.
        std::set<fs::path> live;
        Statement q(db_,"SELECT content FROM clipboard_items WHERE type='image'");
        int rc; while((rc=sqlite3_step(q.p))==SQLITE_ROW) live.insert(fs::path(wide(column(q.p,0))));
        if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
        for(auto& entry:fs::directory_iterator(media_)) if(entry.path().extension()==L".png"&&!live.contains(entry.path())) {
            std::error_code ec; fs::remove(entry.path(),ec);
        }
    } catch(...) { sqlite3_close(db_); db_=nullptr; throw; }
}
Store::~Store() { if(db_) sqlite3_close(db_); }
void Store::insert(const Item& item) {
    std::vector<fs::path> expired;
    exec("BEGIN IMMEDIATE");
    try {
        Statement s(db_,"INSERT INTO clipboard_items(type,content,char_count) VALUES(?,?,?)");
        s.text(1,item.kind==Kind::Image?"image":"text"); s.text(2,utf8(item.value));
        sqlite3_int64 count=0;
        if(item.kind==Kind::Text) for(wchar_t ch:item.value) if(ch<0xDC00||ch>0xDFFF) ++count;
        sqlite3_bind_int64(s.p,3,count); s.done();
        Statement old(db_,"SELECT content FROM clipboard_items WHERE type='image' AND id NOT IN (SELECT id FROM clipboard_items ORDER BY id DESC LIMIT ?)");
        sqlite3_bind_int(old.p,1,static_cast<int>(limit_));
        int rc; while((rc=sqlite3_step(old.p))==SQLITE_ROW) expired.emplace_back(wide(column(old.p,0)));
        if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
        Statement trim(db_,"DELETE FROM clipboard_items WHERE id NOT IN (SELECT id FROM clipboard_items ORDER BY id DESC LIMIT ?)");
        sqlite3_bind_int(trim.p,1,static_cast<int>(limit_)); trim.done(); exec("COMMIT");
    } catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
    for(auto& p:expired) if(p.parent_path()==media_) { std::error_code ec; fs::remove(p,ec); }
}
std::vector<Item> Store::recent() {
    Statement q(db_,"SELECT id,type,content,char_count,created_at FROM clipboard_items ORDER BY created_at DESC,id DESC");
    std::vector<Item> out;
    int rc;
    while((rc=sqlite3_step(q.p))==SQLITE_ROW) {
        Item i; i.id=sqlite3_column_int64(q.p,0); i.kind=column(q.p,1)=="image"?Kind::Image:Kind::Text;
        i.value=wide(column(q.p,2)); i.title=i.kind==Kind::Image?L"Image":i.value;
        i.detail=wide(column(q.p,4));
        if(i.kind==Kind::Text) i.detail+=L"  ·  "+std::to_wstring(sqlite3_column_int64(q.p,3))+L" characters";
        out.push_back(std::move(i));
    }
    if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return out;
}
}
