#pragma once
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
};
namespace detail {
inline constexpr std::array<std::uint8_t, 0> empty_flags{};
inline constexpr std::array<std::uint8_t, 1> bonded_flags{6};
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
}
inline std::optional<std::size_t> qualification_card_effect_count(std::string_view key) {
    std::size_t count = 0;
    for (const auto& entry : detail::entries) if (entry.card == key) ++count;
    if (count == 0) return std::nullopt; // No zero-effect profile has been reviewed.
    return count;
}
inline bool supports_qualification_card_profile(std::string_view key) {
    return qualification_card_effect_count(key).has_value();
}
inline std::optional<EffectQualificationProfile> lookup_effect_qualification_profile(
    std::string_view key, std::size_t ordinal, std::string_view factory,
    std::string_view tag, int type) {
    for (const auto& entry : detail::entries)
        if (entry.card == key && entry.ordinal == ordinal && entry.factory == factory &&
            entry.tag == tag && entry.type == type) return entry.profile;
    return std::nullopt;
}
}
