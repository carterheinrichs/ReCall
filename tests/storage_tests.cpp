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
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    fs::remove_all(state);
    std::cout << (failures?"Storage checks failed":"Storage checks passed") << '\n';
    return failures?1:0;
}
