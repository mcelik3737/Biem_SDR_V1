#pragma once

#include <string>

#include "CallRecord.h"

namespace biem::core {

class Database; // fwd decl - see Database.h

// Builds the human-facing "Call Title" for a CallRecord, per the rule the
// call was recorded against: prefer a known alias for the source radio ID,
// then a known alias for the talkgroup/group, then fall back to the raw
// numeric TG/ID, then the configured channel label. `db` may be null (no
// alias lookup, e.g. in a unit test) - it is only read, never modified.
//
// Aliases are managed via Database::setRadioLabel / setTalkgroupLabel (see
// docs/ROADMAP.md "acik sorular" #1 - this is the default behavior chosen
// absent a different rule from the user; a UI/CLI to manage aliases exists
// so it doesn't require touching code to add a mapping).
std::string buildCallTitle(const CallRecord& rec, Database* db);

} // namespace biem::core
