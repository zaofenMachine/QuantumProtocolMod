#pragma once
#include "CheckpointSchema.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace QuantumCheckpoint::qualification {
// Exact shipped-EXE static profiles. See runtime/phase39-default-flags-profiles.json.
// This proves constructor defaults only; callers separately bind complete CardInfo,
// selected definition membership, owner and stable runtime reads. No writer exists here.
struct EffectQualificationProfile {
    std::string_view id;
    std::span<const std::uint8_t> flags;
    bool permits_used_once_replay{false};
};
inline constexpr bool is_supported_qualification_policy(std::string_view policy) {
    return policy == ExactPlayerQualificationPolicy || policy == ExactPlayerQualificationBowOncePolicy;
}
inline constexpr std::optional<int> qualification_proof_schema_for_policy(std::string_view policy) {
    if (policy == ExactPlayerQualificationPolicy) return ExactPlayerQualificationBaselineSchemaVersion;
    if (policy == ExactPlayerQualificationBowOncePolicy) return ExactPlayerQualificationBowOnceSchemaVersion;
    return std::nullopt;
}
// Only the exact published A/B layout versions imply a qualification policy.
// Legacy and unknown future schemas never acquire one by a >= comparison.
inline constexpr std::optional<std::string_view> qualification_policy_for_layout(
    std::string_view kind, int schema_version) {
    if ((kind == ExactPlayerZonesCheckpointKind && schema_version == ExactPlayerZonesQualificationSchemaVersion)
        || (kind == ExactPlayerTrashCheckpointKind && schema_version == ExactPlayerTrashQualificationSchemaVersion)
        || (kind == ExactPlayerFieldCheckpointKind && schema_version == ExactPlayerFieldQualificationSchemaVersion))
        return ExactPlayerQualificationPolicy;
    if ((kind == ExactPlayerZonesCheckpointKind && schema_version == ExactPlayerZonesBowOnceQualificationSchemaVersion)
        || (kind == ExactPlayerTrashCheckpointKind && schema_version == ExactPlayerTrashBowOnceQualificationSchemaVersion)
        || (kind == ExactPlayerFieldCheckpointKind && schema_version == ExactPlayerFieldBowOnceQualificationSchemaVersion))
        return ExactPlayerQualificationBowOncePolicy;
    return std::nullopt;
}
namespace detail {
inline constexpr std::array<std::uint8_t, 0> empty_flags{};
inline constexpr std::array<std::uint8_t, 1> bonded_flags{6};
inline constexpr std::array<std::uint8_t, 1> once_flags{0};
struct Entry {
    std::string_view card;
    std::size_t ordinal;
    std::string_view factory;
    std::string_view tag;
    int type;
    EffectQualificationProfile profile;
};
inline constexpr Entry entries[]{
    {"naturalApple@0",0,"player.esper.fruit.apple","naturalApple",3,{"phase39-default-v1:player.esper.fruit.apple",empty_flags}},
    {"naturalApple@1",0,"player.esper.fruit.apple.u1","naturalApple.u1",3,{"phase39-default-v1:player.esper.fruit.apple.u1",empty_flags}},
    {"naturalLemon@0",0,"player.esper.fruit.lemon","naturalLemon",3,{"phase39-default-v1:player.esper.fruit.lemon",empty_flags}},
    {"naturalLemon@1",0,"player.esper.fruit.lemon","naturalLemon",3,{"phase39-default-v1:player.esper.fruit.lemon",empty_flags}},
    {"naturalCherry@0",0,"player.esper.fruit.cherry","naturalCherry",3,{"phase39-default-v1:player.esper.fruit.cherry",empty_flags}},
    {"naturalCherry@1",0,"player.esper.fruit.cherry.u1","naturalCherry",3,{"phase39-default-v1:player.esper.fruit.cherry.u1",empty_flags}},
    {"spDeckEdit@0",0,"player.special.deckedit","spDeckEdit",3,{"phase39-default-v1:player.special.deckedit",bonded_flags}},
    {"spDeckEdit@0",1,"player.special.deckedit_manual","spDeckEdit_manual",1,{"phase39-default-v1:player.special.deckedit_manual",bonded_flags}},
    {"genericUpdraft@0",0,"player.generic.set1.updraft","genericUpdraft",3,{"phase39-default-v1:player.generic.set1.updraft",empty_flags}},
};
// Separate from A's unchanged table. The full constructor/default/definition
// evidence is runtime/phase40-bow-profile.json, not inferred from CardInfo.
inline constexpr Entry base_bow{
    "sakuraBow@0", 0, "player.aurora.sakura.sakurabow", "sakuraBow", 1,
    {"phase40-bow-once-v2:player.aurora.sakura.sakurabow", once_flags, true}
};
}
inline std::optional<std::size_t> qualification_card_effect_count(
    std::string_view key, std::string_view policy = ExactPlayerQualificationPolicy) {
    if (!is_supported_qualification_policy(policy)) return std::nullopt;
    if (policy == ExactPlayerQualificationBowOncePolicy && key == detail::base_bow.card) return 1;
    std::size_t count = 0;
    for (const auto& entry : detail::entries) if (entry.card == key) ++count;
    if (count == 0) return std::nullopt; // No zero-effect profile has been reviewed.
    return count;
}
inline bool supports_qualification_card_profile(
    std::string_view key, std::string_view policy = ExactPlayerQualificationPolicy) {
    return qualification_card_effect_count(key, policy).has_value();
}
inline std::optional<EffectQualificationProfile> lookup_effect_qualification_profile(
    std::string_view key, std::size_t ordinal, std::string_view factory,
    std::string_view tag, int type, std::string_view policy = ExactPlayerQualificationPolicy) {
    if (!is_supported_qualification_policy(policy)) return std::nullopt;
    if (policy == ExactPlayerQualificationBowOncePolicy) {
        const auto& entry = detail::base_bow;
        if (entry.card == key && entry.ordinal == ordinal && entry.factory == factory &&
            entry.tag == tag && entry.type == type) return entry.profile;
    }
    for (const auto& entry : detail::entries)
        if (entry.card == key && entry.ordinal == ordinal && entry.factory == factory &&
            entry.tag == tag && entry.type == type) return entry.profile;
    return std::nullopt;
}
}
