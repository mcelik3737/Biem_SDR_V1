#include "test_util.h"

#include <filesystem>
#include <string>

#include "../src/core/Database.h"

using namespace biem::core;

namespace {

void testInsertAndSearch() {
    std::string dbPath = "test_database_tmp.sqlite3";
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");

    {
        Database db(dbPath);
        db.migrate();

        db.setTalkgroupLabel(100, "Saha Ekibi 1");

        CallRecord rec;
        rec.startUnixTimeMs = 1000000;
        rec.durationMs = 5000;
        rec.frequencyHz = 446006250.0;
        rec.modulation = Modulation::DmrDigital;
        rec.source = CallSource::Sdr;
        rec.talkgroupId = 100;
        rec.radioId = 42;
        rec.slot = 1;
        rec.channelLabel = "Test Kanal";
        rec.audioFilePath = "/tmp/does_not_need_to_exist.wav";

        int64_t id = db.insertCall(rec);
        BIEM_CHECK(id > 0);
        BIEM_CHECK(rec.title == "Saha Ekibi 1 (Slot 1)"); // alias should win over the raw TG number

        auto fetched = db.getById(id);
        BIEM_CHECK(fetched.has_value());
        if (fetched) {
            BIEM_CHECK(fetched->talkgroupId.has_value() && *fetched->talkgroupId == 100);
            BIEM_CHECK(fetched->radioId.has_value() && *fetched->radioId == 42);
            BIEM_CHECK(fetched->title == "Saha Ekibi 1 (Slot 1)");
        }

        Database::SearchFilter byTg;
        byTg.talkgroupId = 100;
        BIEM_CHECK(db.search(byTg).size() == 1);

        Database::SearchFilter noMatch;
        noMatch.talkgroupId = 999;
        BIEM_CHECK(db.search(noMatch).empty());

        Database::SearchFilter byTitle;
        byTitle.titleContains = "Saha";
        BIEM_CHECK(db.search(byTitle).size() == 1);
    }

    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

} // namespace

int main() {
    testInsertAndSearch();
    BIEM_TEST_MAIN_RETURN();
}
