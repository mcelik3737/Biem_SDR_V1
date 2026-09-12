#include "Bptc196x96.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "DmrConstants.h"
#include "Hamming139.h"
#include "Hamming1511.h"

namespace biem::dsp::dmr {

namespace {

constexpr int kRows = 13; // Hamming(13,9) column length
constexpr int kCols = 15; // Hamming(15,11) row length
static_assert(kRows * kCols + 1 == kBptcTotalBits, "grid + 1 reserved bit must equal 196");
constexpr int kReservedCandidateCount = 3; // 9*11 - 96 == 3, see header comment

bool isDataRow(int row0Indexed) {
    int pos1Indexed = row0Indexed + 1;
    for (int p : Hamming139::kDataPositions) {
        if (p == pos1Indexed) return true;
    }
    return false;
}

bool isDataCol(int col0Indexed) {
    int pos1Indexed = col0Indexed + 1;
    for (int p : Hamming1511::kDataPositions) {
        if (p == pos1Indexed) return true;
    }
    return false;
}

// The 9 data-rows x 11 data-cols = 99 candidate cells, row-major order.
std::vector<std::pair<int, int>> buildCandidateList() {
    std::vector<std::pair<int, int>> candidates;
    candidates.reserve(99);
    for (int r = 0; r < kRows; ++r) {
        if (!isDataRow(r)) continue;
        for (int c = 0; c < kCols; ++c) {
            if (isDataCol(c)) candidates.emplace_back(r, c);
        }
    }
    return candidates;
}

} // namespace

std::array<uint8_t, 196> Bptc196x96::interleave(const std::array<uint8_t, 196>& input) {
    std::array<uint8_t, 196> out{};
    for (int a = 0; a < kBptcTotalBits; ++a) {
        int dest = (a * kBptcInterleaveMultiplier) % kBptcTotalBits;
        out[dest] = input[a];
    }
    return out;
}

std::array<uint8_t, 196> Bptc196x96::deinterleave(const std::array<uint8_t, 196>& input) {
    std::array<uint8_t, 196> out{};
    for (int a = 0; a < kBptcTotalBits; ++a) {
        int src = (a * kBptcInterleaveMultiplier) % kBptcTotalBits;
        out[a] = input[src];
    }
    return out;
}

bool Bptc196x96::decode(const std::array<uint8_t, 196>& raw196, std::array<uint8_t, 96>& payloadOut) {
    std::array<uint8_t, 196> grid = deinterleave(raw196);

    // Row-correction pass over ALL 13 rows (not just data rows): for a
    // correctly-constructed linear product code the parity rows satisfy
    // the row code too, by linearity - see header comment. Running
    // correction over all rows/columns (not just "data" ones) is standard
    // iterative product-code decoding.
    for (int r = 0; r < kRows; ++r) {
        std::array<uint8_t, 15> row{};
        for (int c = 0; c < kCols; ++c) row[c] = grid[r * kCols + c];
        std::array<uint8_t, 11> unused{};
        Hamming1511::decode(row, unused);
        for (int c = 0; c < kCols; ++c) grid[r * kCols + c] = row[c];
    }

    for (int c = 0; c < kCols; ++c) {
        std::array<uint8_t, 13> col{};
        for (int r = 0; r < kRows; ++r) col[r] = grid[r * kCols + c];
        std::array<uint8_t, 9> unused{};
        Hamming139::decode(col, unused);
        for (int r = 0; r < kRows; ++r) grid[r * kCols + c] = col[r];
    }

    auto candidates = buildCandidateList();
    if (candidates.size() != static_cast<size_t>(kBptcPayloadBits) + kReservedCandidateCount) {
        return false; // internal table mismatch - see kReservedCandidateCount comment
    }
    for (int i = 0; i < kBptcPayloadBits; ++i) {
        auto [r, c] = candidates[static_cast<size_t>(i) + kReservedCandidateCount];
        payloadOut[i] = grid[r * kCols + c];
    }
    return true;
}

std::array<uint8_t, 196> Bptc196x96::encode(const std::array<uint8_t, 96>& payload) {
    std::array<uint8_t, 196> grid{}; // grid[195] (the extra reserved bit) stays 0

    auto candidates = buildCandidateList(); // size 99, if tables are consistent
    // The first kReservedCandidateCount candidate cells are left at 0
    // ("reserved" placeholder bits - see header comment); payload bits
    // fill the rest in order.
    for (size_t i = kReservedCandidateCount; i < candidates.size(); ++i) {
        size_t payloadIdx = i - kReservedCandidateCount;
        if (payloadIdx >= payload.size()) break;
        auto [r, c] = candidates[i];
        grid[r * kCols + c] = payload[payloadIdx];
    }

    // Phase 1: row parity, DATA ROWS ONLY. Parity rows are intentionally
    // left untouched here - they're fully determined by phase 2 below, so
    // there is no cell that both phases try to write (see header comment
    // on why running row-encode over ALL rows here would conflict with
    // phase 2 at the 4x4 "corner" cells).
    for (int r = 0; r < kRows; ++r) {
        if (!isDataRow(r)) continue;
        std::array<uint8_t, 11> rowData{};
        for (int i = 0; i < 11; ++i) rowData[i] = grid[r * kCols + (Hamming1511::kDataPositions[i] - 1)];
        std::array<uint8_t, 15> codeword = Hamming1511::encode(rowData);
        for (int c = 0; c < kCols; ++c) grid[r * kCols + c] = codeword[c];
    }

    // Phase 2: column parity for ALL 15 columns. Every column's 9 data-row
    // values are now known (filled either from payload directly, for
    // parity-columns whose data-rows were untouched by phase 1's per-row
    // write of parity-COLUMN positions... to be precise: phase 1 wrote all
    // 15 columns for each data row, so by this point every (data-row,
    // any-column) cell is populated). This fills in the 4 parity-row cells
    // of every column, including the 4x4 corner block.
    for (int c = 0; c < kCols; ++c) {
        std::array<uint8_t, 9> colData{};
        for (int i = 0; i < 9; ++i) colData[i] = grid[(Hamming139::kDataPositions[i] - 1) * kCols + c];
        std::array<uint8_t, 13> codeword = Hamming139::encode(colData);
        for (int r = 0; r < kRows; ++r) grid[r * kCols + c] = codeword[r];
    }

    return interleave(grid);
}

} // namespace biem::dsp::dmr
