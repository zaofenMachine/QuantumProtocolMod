#include "PlayerCounterState.hpp"
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
    const std::string sample="3;0|a,-1|b,2;0|zero,0;0";
    const auto parsed=parse_player_counter_states(sample,error);
    require(parsed && parsed->size()==4 && serialize_player_counter_states(*parsed)==sample,
        "generic, negative special, explicit zero entry and empty map round trip");
    require((*parsed)[2].special.size()==1 && (*parsed)[3].special.empty(),
        "a zero-valued tag remains distinct from an absent tag");
    require(player_counter_restore_order((*parsed)[1])==std::vector<std::size_t>{1,0},
        "special positives replay before negatives to avoid artificial negative prefixes");
    require(parse_player_counter_states("256|a,-256|b,256",error).has_value(),"bounded replay totals are accepted");
    require(parse_player_counter_states("0|a,-2",error).has_value(),"negative special totals remain representable");
    for (const auto bad : {"","-1","257","0|a,257","0|a,-257","0|a,200|b,57",
                           "0|a,-200|b,-57","0|a,1|A,2","0|b,1|a,2","0|None,1",
                           "0|a b,2","00","0|a,+1","0|a,-0","0|a,1,2","0;","0|"})
        require(!parse_player_counter_states(bad,error),"invalid counter state is rejected");
    std::cout << "Player counter state tests passed\n";
}
