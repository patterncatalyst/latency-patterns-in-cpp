#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

namespace ch03 {

// A real sortation line does not route by the full barcode: once a barcode
// is decoded, what the diverter actually needs is which of a small, fixed
// set of SKU families the parcel belongs to. That family code is this
// chapter's dense key space: every decode produces a value in
// [0, key_space), which is what both lanes below resolve a chute from.
inline constexpr int key_space = 4096;
inline constexpr int chute_count = 12;
// A 13th, dedicated chute for a parcel whose volumetric weight makes it too
// big for the chute its SKU family would otherwise use (same idea as
// ch01::LoadedScanStage's oversize_chute).
inline constexpr int oversize_chute = chute_count;

// The barcode decode: a checksum loop, the same multiplicative-hash shape
// ch01 and ch02 already use, folded down into the dense key space above.
// This cost is IDENTICAL in both BeforeScanStage and AfterScanStage: this
// chapter targets what happens after the decode, not the decode itself
// (see the chapter prose, "The before path and why it is wasteful").
[[nodiscard]] int decode_key_for_barcode(std::uint64_t barcode) noexcept;

// The one source of truth for which chute a dense key routes to: a fixed,
// deterministic, but otherwise arbitrary assignment, standing in for a real
// sortation manifest (which SKU family ships out of which chute). Called
// only while building a lookup structure below, key_space times total,
// never on a per-parcel path in either lane.
[[nodiscard]] int manifest_chute_for_key(int dense_key) noexcept;

// The one source of truth for a chute's priority weighting, used by the
// oversize-for-weight check both lanes run. AfterScanStage calls this
// chute_count times, once, in its constructor. BeforeScanStage calls it
// chute_count times on EVERY parcel, which is the "recomputing a value
// that could be precomputed" waste this chapter eliminates (see
// before_scan_stage.cpp).
[[nodiscard]] double manifest_zone_multiplier_for_chute(int chute) noexcept;

// Builds the "before" lane's chute-resolution structure: a
// std::unordered_map<int, int> keyed by dense key, populated once from
// manifest_chute_for_key.
[[nodiscard]] std::unordered_map<int, int> build_manifest_map();

// Builds the "after" lane's chute-resolution structure: a
// std::array<int, key_space> indexed directly by dense key, populated once
// from the SAME manifest_chute_for_key as build_manifest_map. The two
// structures always carry identical assignments, so BeforeScanStage and
// AfterScanStage can never disagree about a chute by construction (see
// tests/before_after_divert_test.cpp).
[[nodiscard]] std::array<int, key_space> build_manifest_table();

}  // namespace ch03
