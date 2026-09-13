#include "PlayerAttackState.hpp"
#include <cstdlib>
#include <iostream>

using namespace QuantumCheckpoint;
static void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

int main()
{
    std::string error{};
    const std::string sample = "2,7|probe,3,-1,0|probeFlags,2,-1,6;2,0|weakness,-5,0,4;1,1";
    const auto parsed = parse_player_attack_states(sample, error);
    require(parsed && parsed->size() == 3 && (*parsed)[0].modifiers[1].flags == 6,
            "multiple cards, modifiers, bit masks and zero clamp parse");
    require(serialize_player_attack_states(*parsed) == sample, "attack records round trip canonically");
    for (const auto invalid : {"", "2,2;", "2,2|", "2,3", "2,-1", "-2,0", "02,2", "+2,2", "2,2,2",
                              "2,3|a,1,-1,8", "2,3|a,1,-1,-1", "2,3|None,1,-1,0", "2,3|a b,1,-1,0",
                              "2,3|a,1,-2,0", "2,5|a,3,2,0", "2,3|a,2147483648,-1,0",
                              "2,4|a,1,-1,0|A,1,-1,0", "2,4|b,1,-1,0|a,1,-1,0",
                              "2,3|a,1,-1,0,extra", "2,3|a,1,-1,0|a,0,-1,0"})
        require(!parse_player_attack_states(invalid, error), "unsafe or ambiguous attack record rejected");
    require(parse_player_attack_states("2,3|x,1,1,7", error).has_value(), "valid limit and all known flags accepted");
    std::vector<PlayerAttackState> too_many(11, PlayerAttackState{2,2,{}});
    require(!parse_player_attack_states(serialize_player_attack_states(too_many), error), "more than ten field slots rejected");
    require(!parse_player_attack_states(std::string(65537, '0'), error), "oversized attack record rejected");
    std::cout << "Player attack state tests passed\n";
}
