#include "Database.h"

#include <sqlite3.h>

#include <sstream>
#include <stdexcept>

#include "TitleBuilder.h"

namespace biem::core {

namespace {

Modulation modulationFromString(const std::string& s) {
    if (s == "AnalogFM") return Modulation::AnalogFM;
    if (s == "DmrDigital") return Modulation::DmrDigital;
    return Modulation::Unknown;
}

CallSource sourceFromString(const std::string& s) {
    if (s == "NetworkRepeater") return CallSource::NetworkRepeater;
    return CallSource::Sdr;
}

constexpr const char* kCallColumns =
    "id, start_unix_time_ms, duration_ms, frequency_hz, modulation, source, "
    "color_code, slot, talkgroup_id, radio_id, dest_radio_id, "
    "gps_lat, gps_lon, gps_alt, gps_time_ms, sds_text, "
    "channel_label, title, audio_file_path, voice_decoded";

void bindOptInt(sqlite3_stmt* stmt, int idx, const std::optional<int>& v) {
    if (v) sqlite3_bind_int(stmt, idx, *v);
    else sqlite3_bind_null(stmt, idx);
}

void bindOptI64(sqlite3_stmt* stmt, int idx, const std::optional<uint32_t>& v) {
    if (v) sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(*v));
    else sqlite3_bind_null(stmt, idx);
}

void bindOptDouble(sqlite3_stmt* stmt, int idx, const std::optional<double>& v) {
    if (v) sqlite3_bind_double(stmt, idx, *v);
    else sqlite3_bind_null(stmt, idx);
}

void bindOptText(sqlite3_stmt* stmt, int idx, const std::optional<std::string>& v) {
    if (v) sqlite3_bind_text(stmt, idx, v->c_str(), -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, idx);
}

std::optional<int> columnOptInt(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int(stmt, idx);
}

std::optional<uint32_t> columnOptU32(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return std::nullopt;
    return static_cast<uint32_t>(sqlite3_column_int64(stmt, idx));
}

std::optional<double> columnOptDouble(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_double(stmt, idx);
}

std::optional<std::string> columnOptText(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return std::nullopt;
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return std::string(reinterpret_cast<const char*>(txt ? txt : (const unsigned char*)""));
}

std::string columnText(sqlite3_stmt* stmt, int idx) {
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return std::string(reinterpret_cast<const char*>(txt ? txt : (const unsigned char*)""));
}

} // namespace

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Database: failed to open '" + path + "': " + msg);
    }
    // Sensible defaults for a single-writer desktop app that must not lose
    // call records: WAL improves concurrent read-while-recording behaviour.
    execOrThrow("PRAGMA journal_mode=WAL;");
    execOrThrow("PRAGMA foreign_keys=ON;");
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::execOrThrow(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "unknown sqlite error";
        sqlite3_free(errMsg);
        throw std::runtime_error("Database: sqlite3_exec failed: " + msg + " (sql: " + sql + ")");
    }
}

void Database::migrate() {
    execOrThrow(
        "CREATE TABLE IF NOT EXISTS calls ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  start_unix_time_ms INTEGER NOT NULL,"
        "  duration_ms INTEGER NOT NULL,"
        "  frequency_hz REAL NOT NULL,"
        "  modulation TEXT NOT NULL,"
        "  source TEXT NOT NULL,"
        "  color_code INTEGER,"
        "  slot INTEGER,"
        "  talkgroup_id INTEGER,"
        "  radio_id INTEGER,"
        "  dest_radio_id INTEGER,"
        "  gps_lat REAL,"
        "  gps_lon REAL,"
        "  gps_alt REAL,"
        "  gps_time_ms INTEGER,"
        "  sds_text TEXT,"
        "  channel_label TEXT,"
        "  title TEXT,"
        "  audio_file_path TEXT,"
        "  voice_decoded INTEGER NOT NULL DEFAULT 0"
        ");"
    );
    execOrThrow("CREATE INDEX IF NOT EXISTS idx_calls_start_time ON calls(start_unix_time_ms);");
    execOrThrow("CREATE INDEX IF NOT EXISTS idx_calls_talkgroup ON calls(talkgroup_id);");
    execOrThrow("CREATE INDEX IF NOT EXISTS idx_calls_radio ON calls(radio_id);");
    execOrThrow("CREATE INDEX IF NOT EXISTS idx_calls_title ON calls(title);");

    execOrThrow(
        "CREATE TABLE IF NOT EXISTS radio_aliases ("
        "  radio_id INTEGER PRIMARY KEY,"
        "  label TEXT NOT NULL"
        ");"
    );
    execOrThrow(
        "CREATE TABLE IF NOT EXISTS talkgroup_aliases ("
        "  talkgroup_id INTEGER PRIMARY KEY,"
        "  label TEXT NOT NULL"
        ");"
    );
}

int64_t Database::insertCall(CallRecord& rec) {
    if (rec.title.empty()) {
        rec.title = buildCallTitle(rec, this);
    }

    static const std::string sql =
        "INSERT INTO calls (start_unix_time_ms, duration_ms, frequency_hz, modulation, source,"
        " color_code, slot, talkgroup_id, radio_id, dest_radio_id,"
        " gps_lat, gps_lon, gps_alt, gps_time_ms, sds_text,"
        " channel_label, title, audio_file_path, voice_decoded)"
        " VALUES (?,?,?,?,?, ?,?,?,?,?, ?,?,?,?,?, ?,?,?,?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::insertCall prepare failed: ") + sqlite3_errmsg(db_));
    }

    int i = 1;
    sqlite3_bind_int64(stmt, i++, rec.startUnixTimeMs);
    sqlite3_bind_int64(stmt, i++, rec.durationMs);
    sqlite3_bind_double(stmt, i++, rec.frequencyHz);
    sqlite3_bind_text(stmt, i++, toString(rec.modulation), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, toString(rec.source), -1, SQLITE_STATIC);
    bindOptInt(stmt, i++, rec.colorCode);
    bindOptInt(stmt, i++, rec.slot);
    bindOptI64(stmt, i++, rec.talkgroupId);
    bindOptI64(stmt, i++, rec.radioId);
    bindOptI64(stmt, i++, rec.destRadioId);
    if (rec.gps) {
        sqlite3_bind_double(stmt, i++, rec.gps->latitude);
        sqlite3_bind_double(stmt, i++, rec.gps->longitude);
        bindOptDouble(stmt, i++, rec.gps->altitudeMeters);
        sqlite3_bind_int64(stmt, i++, rec.gps->unixTimeMs);
    } else {
        sqlite3_bind_null(stmt, i++);
        sqlite3_bind_null(stmt, i++);
        sqlite3_bind_null(stmt, i++);
        sqlite3_bind_null(stmt, i++);
    }
    bindOptText(stmt, i++, rec.sdsText);
    sqlite3_bind_text(stmt, i++, rec.channelLabel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, i++, rec.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, i++, rec.audioFilePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, i++, rec.voiceDecoded ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("Database::insertCall step failed: ") + sqlite3_errmsg(db_));
    }

    rec.id = sqlite3_last_insert_rowid(db_);
    return rec.id;
}

CallRecord Database::readRow(sqlite3_stmt* stmt) const {
    CallRecord rec;
    rec.id = sqlite3_column_int64(stmt, 0);
    rec.startUnixTimeMs = sqlite3_column_int64(stmt, 1);
    rec.durationMs = sqlite3_column_int64(stmt, 2);
    rec.frequencyHz = sqlite3_column_double(stmt, 3);
    rec.modulation = modulationFromString(columnText(stmt, 4));
    rec.source = sourceFromString(columnText(stmt, 5));
    rec.colorCode = columnOptInt(stmt, 6);
    rec.slot = columnOptInt(stmt, 7);
    rec.talkgroupId = columnOptU32(stmt, 8);
    rec.radioId = columnOptU32(stmt, 9);
    rec.destRadioId = columnOptU32(stmt, 10);

    auto lat = columnOptDouble(stmt, 11);
    auto lon = columnOptDouble(stmt, 12);
    if (lat && lon) {
        GpsFix fix;
        fix.latitude = *lat;
        fix.longitude = *lon;
        fix.altitudeMeters = columnOptDouble(stmt, 13);
        fix.unixTimeMs = sqlite3_column_type(stmt, 14) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 14);
        rec.gps = fix;
    }
    rec.sdsText = columnOptText(stmt, 15);
    rec.channelLabel = columnText(stmt, 16);
    rec.title = columnText(stmt, 17);
    rec.audioFilePath = columnText(stmt, 18);
    rec.voiceDecoded = sqlite3_column_int(stmt, 19) != 0;
    return rec;
}

std::optional<CallRecord> Database::getById(int64_t id) {
    std::string sql = std::string("SELECT ") + kCallColumns + " FROM calls WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::getById prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, id);
    std::optional<CallRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = readRow(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<CallRecord> Database::search(const SearchFilter& filter) {
    std::ostringstream sql;
    sql << "SELECT " << kCallColumns << " FROM calls WHERE 1=1";
    if (filter.fromUnixTimeMs) sql << " AND start_unix_time_ms >= ?";
    if (filter.toUnixTimeMs) sql << " AND start_unix_time_ms <= ?";
    if (filter.titleContains) sql << " AND title LIKE ?";
    if (filter.talkgroupId) sql << " AND talkgroup_id = ?";
    if (filter.radioId) sql << " AND radio_id = ?";
    if (filter.slot) sql << " AND slot = ?";
    if (filter.source) sql << " AND source = ?";
    sql << " ORDER BY start_unix_time_ms DESC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::search prepare failed: ") + sqlite3_errmsg(db_));
    }

    int i = 1;
    if (filter.fromUnixTimeMs) sqlite3_bind_int64(stmt, i++, *filter.fromUnixTimeMs);
    if (filter.toUnixTimeMs) sqlite3_bind_int64(stmt, i++, *filter.toUnixTimeMs);
    std::string likePattern;
    if (filter.titleContains) {
        likePattern = "%" + *filter.titleContains + "%";
        sqlite3_bind_text(stmt, i++, likePattern.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (filter.talkgroupId) sqlite3_bind_int64(stmt, i++, static_cast<sqlite3_int64>(*filter.talkgroupId));
    if (filter.radioId) sqlite3_bind_int64(stmt, i++, static_cast<sqlite3_int64>(*filter.radioId));
    if (filter.slot) sqlite3_bind_int(stmt, i++, *filter.slot);
    if (filter.source) sqlite3_bind_text(stmt, i++, toString(*filter.source), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, i++, filter.limit);
    sqlite3_bind_int(stmt, i++, filter.offset);

    std::vector<CallRecord> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(readRow(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

void Database::setRadioLabel(uint32_t radioId, const std::string& label) {
    static const std::string sql =
        "INSERT INTO radio_aliases(radio_id, label) VALUES(?, ?)"
        " ON CONFLICT(radio_id) DO UPDATE SET label = excluded.label";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::setRadioLabel prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(radioId));
    sqlite3_bind_text(stmt, 2, label.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("Database::setRadioLabel step failed: ") + sqlite3_errmsg(db_));
    }
}

void Database::setTalkgroupLabel(uint32_t talkgroupId, const std::string& label) {
    static const std::string sql =
        "INSERT INTO talkgroup_aliases(talkgroup_id, label) VALUES(?, ?)"
        " ON CONFLICT(talkgroup_id) DO UPDATE SET label = excluded.label";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::setTalkgroupLabel prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(talkgroupId));
    sqlite3_bind_text(stmt, 2, label.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("Database::setTalkgroupLabel step failed: ") + sqlite3_errmsg(db_));
    }
}

std::optional<std::string> Database::resolveRadioLabel(uint32_t radioId) {
    static const std::string sql = "SELECT label FROM radio_aliases WHERE radio_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::resolveRadioLabel prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(radioId));
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<std::string> Database::resolveTalkgroupLabel(uint32_t talkgroupId) {
    static const std::string sql = "SELECT label FROM talkgroup_aliases WHERE talkgroup_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database::resolveTalkgroupLabel prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(talkgroupId));
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace biem::core
