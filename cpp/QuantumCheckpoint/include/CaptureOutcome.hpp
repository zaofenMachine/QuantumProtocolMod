#pragma once
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
namespace QuantumCheckpoint::capture_outcome {
enum class Part : unsigned { Main, Spawn, Hand, Qualification, Layout, Charge, Turn, Count };
struct Failure { std::string stage, category, reason; };
// MSVC 14.38 can conservatively infer a throwing aggregate constructor from
// nested brace default member initializers. Make the intended construction
// explicit, and prove each nontrivial member below instead of hiding failures.
struct Component {
    Component() noexcept = default;
    bool selected{}, requiredForPlayer{};
    std::string status{}, path, payloadChecksum, sha256, hashStatus{}, error;
};
static_assert(std::is_nothrow_default_constructible_v<std::string>);
static_assert(std::is_nothrow_default_constructible_v<Failure>);
static_assert(std::is_nothrow_default_constructible_v<Component>);
static_assert(std::is_nothrow_default_constructible_v<std::array<Component, static_cast<unsigned>(Part::Count)>>);
static_assert(std::is_nothrow_default_constructible_v<std::vector<Failure>>);
#if defined(_MSC_VER)
// Debug iterator proxies can allocate even inside a declared noexcept STL
// constructor. This diagnostic module only admits the production no-proxy ABI.
static_assert(_ITERATOR_DEBUG_LEVEL == 0);
#endif
struct Outcome {
    Outcome() noexcept = default;
    bool diagnosticComplete{true}, playerSelected{}, invalidationStarted{}, invalidationCompleted{};
    std::string attemptId, origin, attemptedAtUtc, finishedAtUtc, capturedAtUtc, mainPayloadChecksum;
    std::string executableSha256, sourceLevelName, layoutKind, qualificationPolicy;
    std::string qualificationLayoutChecksum, qualificationLayoutKind, qualificationMainChecksum, handRequiredChecksum;
    int waveIndex{-1}, layoutSchema{};
    long long automaticDeadlineTicks{};
    std::array<Component, static_cast<unsigned>(Part::Count)> components{};
    std::vector<Failure> events{};
    Failure firstFailure{}, firstPlayerRefusal{};
    template<class F> void safely(F&& action) noexcept {
        try { action(); } catch (...) { diagnosticComplete = false; }
    }
    void failure(std::string_view stage, std::string_view category, std::string_view reason, bool player) noexcept;
    void event(std::string_view stage, std::string_view category, std::string_view reason) noexcept;
    void select(Part part, bool required, std::string_view path, std::string_view checksum) noexcept;
    void written(Part part) noexcept;
    void failed(Part part, std::string_view reason, bool player) noexcept;
    void withdrawn() noexcept;
    bool player_committed() const noexcept;
    bool all_selected_committed() const noexcept;
    std::string_view classification() const noexcept;
};
std::string serialize(const Outcome& outcome);
// Callback and serialization failures never escape into capture control flow.
bool publish(const Outcome&, const std::function<void(std::string_view)>& writer) noexcept;
}
