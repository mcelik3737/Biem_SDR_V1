#include "TitleBuilder.h"

#include "Database.h"

namespace biem::core {

std::string buildCallTitle(const CallRecord& rec, Database* db) {
    std::string identity;

    if (db && rec.radioId) {
        if (auto label = db->resolveRadioLabel(*rec.radioId); label && !label->empty()) {
            identity = *label;
        }
    }
    if (identity.empty() && db && rec.talkgroupId) {
        if (auto label = db->resolveTalkgroupLabel(*rec.talkgroupId); label && !label->empty()) {
            identity = *label;
        }
    }
    if (identity.empty() && rec.talkgroupId) {
        identity = "TG " + std::to_string(*rec.talkgroupId);
    }
    if (identity.empty() && rec.radioId) {
        identity = "ID " + std::to_string(*rec.radioId);
    }
    if (identity.empty() && !rec.channelLabel.empty()) {
        identity = rec.channelLabel;
    }
    if (identity.empty()) {
        identity = "Bilinmeyen Kanal";
    }
    if (rec.slot) {
        identity += " (Slot " + std::to_string(*rec.slot) + ")";
    }
    return identity;
}

} // namespace biem::core
