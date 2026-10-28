#include "store.h"
#include "search.h"
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
             "CREATE TABLE IF NOT EXISTS app_usage (app_key TEXT PRIMARY KEY,launches INTEGER NOT NULL,last_used INTEGER NOT NULL);"
             "CREATE TABLE IF NOT EXISTS clipboard_items (id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "type TEXT NOT NULL CHECK(type IN ('text','image')), content TEXT, char_count INTEGER DEFAULT 0,"
             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,preview TEXT);"
             "DROP INDEX IF EXISTS idx_clipboard_created;"
             "CREATE INDEX IF NOT EXISTS idx_clipboard_recent ON clipboard_items(created_at DESC,id DESC);");
        bool migrate=false;
        {
            Statement schema(db_,"SELECT count(*) FROM pragma_table_info('clipboard_items') WHERE name='preview'");
            if(sqlite3_step(schema.p)!=SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
            migrate=sqlite3_column_int(schema.p,0)==0;
        }
        if(migrate) {
            // Atomic, one-time upgrade: browsing never needs to read multi-megabyte payloads.
            exec("BEGIN IMMEDIATE");
            try {
                exec("ALTER TABLE clipboard_items ADD COLUMN preview TEXT;"
                     "UPDATE clipboard_items SET preview=substr(content,1,12000) WHERE type='text';"
                     "COMMIT");
            } catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
        }
        Statement count(db_,"SELECT count(*) FROM clipboard_items");
        if(sqlite3_step(count.p)!=SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
        count_=static_cast<size_t>(sqlite3_column_int64(count.p,0));
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
void Store::record_app_launch(std::wstring_view key) {
    if(key.empty()) return;
    Statement s(db_,"INSERT INTO app_usage VALUES(?,1,unixepoch()) ON CONFLICT(app_key) DO UPDATE SET launches=min(launches+1,1000000000),last_used=unixepoch()");
    s.text(1,utf8(key)); s.done();
}
AppUsageMap Store::app_usage() {
    Statement q(db_,"SELECT app_key,launches,last_used FROM app_usage");
    AppUsageMap out; int rc;
    while((rc=sqlite3_step(q.p))==SQLITE_ROW) out.emplace(wide(column(q.p,0)),AppUsage{sqlite3_column_int64(q.p,1),sqlite3_column_int64(q.p,2)});
    if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return out;
}
void Store::insert(const Item& item) {
    std::vector<fs::path> expired;
    exec("BEGIN IMMEDIATE");
    try {
        Statement s(db_,"INSERT INTO clipboard_items(type,content,char_count,preview) VALUES(?,?,?,?)");
        s.text(1,item.kind==Kind::Image?"image":"text"); s.text(2,utf8(item.value));
        auto preview=std::wstring_view(item.value).substr(0,12000);
        if(!preview.empty()&&preview.back()>=0xD800&&preview.back()<=0xDBFF) preview.remove_suffix(1);
        if(item.kind==Kind::Text) s.text(4,utf8(preview));
        sqlite3_int64 count=0;
        if(item.kind==Kind::Text) for(wchar_t ch:item.value) if(ch<0xDC00||ch>0xDFFF) ++count;
        sqlite3_bind_int64(s.p,3,count); s.done();
        if(count_+1>limit_) {
            // Usually one expired row: seek from the oldest end, independent of history size.
            Statement cutoff(db_,"SELECT id FROM clipboard_items ORDER BY id LIMIT 1 OFFSET ?");
            sqlite3_bind_int64(cutoff.p,1,static_cast<sqlite3_int64>(count_-limit_));
            if(sqlite3_step(cutoff.p)!=SQLITE_ROW) throw std::runtime_error("Cannot find retention cutoff");
            const auto id=sqlite3_column_int64(cutoff.p,0);
            Statement old(db_,"SELECT content FROM clipboard_items WHERE id<=? AND type='image'");
            sqlite3_bind_int64(old.p,1,id);
            int rc; while((rc=sqlite3_step(old.p))==SQLITE_ROW) expired.emplace_back(wide(column(old.p,0)));
            if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
            Statement trim(db_,"DELETE FROM clipboard_items WHERE id<=?");
            sqlite3_bind_int64(trim.p,1,id); trim.done();
        }
        exec("COMMIT"); count_=std::min(count_+1,static_cast<size_t>(limit_));
    } catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
    for(auto& p:expired) if(p.parent_path()==media_) { std::error_code ec; fs::remove(p,ec); }
}
std::vector<Item> Store::recent(size_t limit) {
    return search({},limit);
}
std::vector<Item> Store::search(std::wstring_view query,size_t limit,const std::function<bool()>& cancelled) {
    // Never select full text payloads while browsing or filtering.
    Statement q(db_,"SELECT id,type,CASE WHEN type='image' THEN content ELSE coalesce(preview,substr(content,1,12000)) END,"
        "char_count,created_at FROM clipboard_items ORDER BY created_at DESC,id DESC LIMIT ?");
    sqlite3_bind_int64(q.p,1,query.empty()&&limit?static_cast<sqlite3_int64>(limit):-1);
    std::vector<Item> out;
    int rc;
    while((rc=sqlite3_step(q.p))==SQLITE_ROW) {
        if(cancelled&&cancelled()) return {};
        Item i; i.id=sqlite3_column_int64(q.p,0); i.kind=column(q.p,1)=="image"?Kind::Image:Kind::Text;
        i.value=wide(column(q.p,2));
        if(i.kind==Kind::Text&&i.value.size()>12000) {
            i.value.resize(12000);
            if(i.value.back()>=0xD800&&i.value.back()<=0xDBFF) i.value.pop_back();
        }
        if(!query.empty()) {
            const auto candidate=i.kind==Kind::Image?std::wstring_view(L"Image"):std::wstring_view(i.value).substr(0,8192);
            if(!fuzzy_matches(query,candidate,cancelled)) continue;
        }
        i.title=i.kind==Kind::Image?L"Image":i.value.substr(0,8192);
        i.detail=wide(column(q.p,4));
        if(i.kind==Kind::Text) i.detail+=L"  ·  "+std::to_wstring(sqlite3_column_int64(q.p,3))+L" characters";
        out.push_back(std::move(i));
        if(limit&&out.size()>=limit) return out;
    }
    if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return out;
}
std::optional<Item> Store::load(long long id) {
    Statement q(db_,"SELECT type,content FROM clipboard_items WHERE id=?");
    sqlite3_bind_int64(q.p,1,id);
    const auto rc=sqlite3_step(q.p);
    if(rc==SQLITE_DONE) return {};
    if(rc!=SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    Item item; item.id=id; item.kind=column(q.p,0)=="image"?Kind::Image:Kind::Text;
    item.value=wide(column(q.p,1)); return item;
}
}
