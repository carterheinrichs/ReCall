#include "store.h"
#include <sqlite3.h>
#include <fstream>
#include <iostream>

using namespace recall;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } };
    GUID guid{}; CoCreateGuid(&guid); wchar_t name[40]{}; StringFromGUID2(guid,name,40);
    const auto state=fs::temp_directory_path()/(L"ReCall-test-"+std::wstring(name));
    fs::create_directories(state/L"clips_media");
    try {
        {
            const auto legacy=state/L"legacy"; fs::create_directories(legacy/L"clips_media");
            sqlite3* db{}; sqlite3_open(utf8((legacy/L"recall.db").wstring()).c_str(),&db);
            sqlite3_exec(db,"CREATE TABLE clipboard_items(id INTEGER PRIMARY KEY AUTOINCREMENT,type TEXT NOT NULL,content TEXT,char_count INTEGER DEFAULT 0,created_at DATETIME DEFAULT CURRENT_TIMESTAMP)",nullptr,nullptr,nullptr);
            sqlite3_stmt* insert{}; sqlite3_prepare_v2(db,"INSERT INTO clipboard_items(type,content) VALUES('text',?)",-1,&insert,nullptr);
            const auto text=std::wstring(11999,L'x')+L"😀"+std::wstring(20000,L'y'); const auto encoded=utf8(text);
            sqlite3_bind_text(insert,1,encoded.c_str(),-1,SQLITE_TRANSIENT);
            check(sqlite3_step(insert)==SQLITE_DONE,"legacy schema fixture inserted");
            sqlite3_finalize(insert); sqlite3_close(db);
            {
                Store upgraded(legacy,1000); auto rows=upgraded.recent();
                check(rows.size()==1&&rows[0].value.size()==11999,"old database migrates to bounded Unicode previews");
                check(upgraded.load(rows[0].id)->value==text,"schema upgrade preserves full original payload");
            }
            Store reopened(legacy,1000);
            check(reopened.recent().size()==1,"preview migration is idempotent");
            // Older binaries can still insert into the upgraded schema without supplying preview.
            sqlite3_open(utf8((legacy/L"recall.db").wstring()).c_str(),&db);
            sqlite3_exec(db,"INSERT INTO clipboard_items(type,content) VALUES('text','older binary entry')",nullptr,nullptr,nullptr);
            sqlite3_close(db);
            check(reopened.recent(1).front().value==L"older binary entry","missing previews fall back safely after a downgrade");
        }
        auto orphan=state/L"clips_media"/L"orphan.png";
        { std::ofstream f(orphan); f << "orphan"; }
        {
            Store store(state,2);
            check(!fs::exists(orphan),"startup orphan cleanup");
            Item image; image.kind=Kind::Image; image.value=(state/L"clips_media"/L"test.png").wstring();
            { std::ofstream f(image.value); f << "image"; }
            store.insert(image);
            Item text; text.kind=Kind::Text; text.value=L"Zażółć 日本語 😀"; store.insert(text);
            auto rows=store.recent();
            check(rows.size()==2,"two records stored");
            check(rows[0].value==text.value,"UTF-8 round trip");
            check(rows[0].id>rows[1].id,"newest ID breaks timestamp ties");
            text.value=L"final"; store.insert(text);
            rows=store.recent();
            check(rows.size()==2&&rows[0].value==L"final","retention and ordering");
            check(!fs::exists(image.value),"expired image removed from disk");
        }
        {
            Store reopened(state,2);
            auto rows=reopened.recent();
            check(rows.size()==2&&rows[0].value==L"final","database survives reopening");
        }
        sqlite3* db{};
        check(sqlite3_open(utf8((state/L"recall.db").wstring()).c_str(),&db)==SQLITE_OK,"database inspection");
        sqlite3_stmt* statement{};
        sqlite3_prepare_v2(db,"PRAGMA journal_mode",-1,&statement,nullptr);
        check(sqlite3_step(statement)==SQLITE_ROW&&std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement,0)))=="wal","WAL enabled");
        sqlite3_finalize(statement);
        sqlite3_prepare_v2(db,"SELECT char_count FROM clipboard_items WHERE content != 'final'",-1,&statement,nullptr);
        check(sqlite3_step(statement)==SQLITE_ROW&&sqlite3_column_int(statement,0)==12,"character count uses code points");
        sqlite3_finalize(statement); sqlite3_close(db);
        {
            Store searchable(state,1000);
            Item text; text.kind=Kind::Text;
            text.value=L"needle old"; searchable.insert(text);
            for(int i=0;i<120;++i) { text.value=L"filler "+std::to_wstring(i); searchable.insert(text); }
            text.value=L"needle new"; searchable.insert(text);
            auto rows=searchable.recent(10);
            check(rows.size()==10&&rows.front().value==L"needle new","recent query limits results in recency order");
            auto matches=searchable.search(L"needle");
            check(matches.size()==2&&matches.front().value==L"needle new"&&matches.back().value==L"needle old","search includes matches older than cached first page");
            matches=searchable.search(L"needle",1);
            check(matches.size()==1&&matches.front().value==L"needle new","streaming search stops at result limit");
            text.value=L"payload "+std::wstring(20000,L'x'); searchable.insert(text);
            matches=searchable.search(L"payload");
            check(matches.size()==1&&matches.front().value.size()==12000&&matches.front().title.size()==8192,"search returns bounded display text");
            auto loaded=searchable.load(matches.front().id);
            check(loaded&&loaded->value==text.value,"lazy payload load preserves full text");
            rows=searchable.recent(1);
            check(rows.size()==1&&rows.front().value.size()==12000,"unfiltered history is bounded too");
            check(!searchable.load(-1),"expired or missing payload is absent");
            text.value=std::wstring(11999,L'x')+L"😀tail"; searchable.insert(text);
            rows=searchable.recent(1);
            check(rows.front().value.size()==11999,"preview truncation preserves surrogate pairs");
            check(searchable.load(rows.front().id)->value==text.value,"surrogate boundary preserves full payload");
            text.value=std::wstring(8192,L'x')+L"needle"; searchable.insert(text);
            check(searchable.search(L"needle").size()==2,"filter respects the original search prefix limit");
            matches=searchable.search(L"needle",100,[]{return true;});
            check(matches.empty(),"superseded search can cancel while scanning");
        }
        {
            Store usage(state,100);
            check(usage.app_usage().empty(),"usage starts empty on existing database");
            usage.record_app_launch(L"editor"); usage.record_app_launch(L"editor");
            usage.record_app_launch(L"browser"); usage.record_app_launch(L"");
            check(usage.app_usage().size()==2&&usage.app_usage().at(L"editor").launches==2,"launch counts are independent and ignore empty identities");
        }
        {
            Store usage(state,100);
            check(usage.app_usage().at(L"editor").launches==2&&usage.app_usage().at(L"editor").last_used>0,"usage survives reopening the database");
        }
        {
            Store reduced(state,1);
            Item text; text.kind=Kind::Text; text.value=L"only retained entry"; reduced.insert(text);
            auto rows=reduced.recent();
            check(rows.size()==1&&reduced.load(rows.front().id)->value==text.value,"lowering retention removes all excess entries");
            text.value=L"replacement"; reduced.insert(text);
            check(!reduced.load(rows.front().id)&&reduced.recent().size()==1,"retention continues correctly after a bulk trim");
            bool rejected=false; text.value=std::wstring(1,static_cast<wchar_t>(0xD800));
            try { reduced.insert(text); } catch(const std::runtime_error&) { rejected=true; }
            text.value=L"after failed insert"; reduced.insert(text);
            check(rejected&&reduced.recent().size()==1&&reduced.recent().front().value==text.value,"rolled-back insert does not corrupt retention accounting");
        }
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    fs::remove_all(state);
    std::cout << (failures?"Storage checks failed":"Storage checks passed") << '\n';
    return failures?1:0;
}
