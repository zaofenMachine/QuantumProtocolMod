#include "PlayerHealthState.hpp"
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
    const std::string sample="2,5|health,3,-1,4;2,1|aNegative,-4,-1,0|zPositive,3,-1,0;2,2";
    const auto parsed=parse_player_health_states(sample,error);
    require(parsed && parsed->size()==3 && serialize_player_health_states(*parsed)==sample,
            "positive, mixed and expired health modifiers round trip");
    require(player_stat_restore_order((*parsed)[1].modifiers)==std::vector<std::size_t>{1,0},
            "health replay also applies non-negative modifiers first");
    for (const auto invalid : {"2,0|negative,-2,-1,0","2,0|negative,-8,-1,0","0,1|health,1,-1,0",
                              "2,4|health,3,-1,0","2,4|x,1,-1,0|X,1,-1,0","2,3|x,1,-1,8",
                              "2,5|x,3,2,0","2,3|None,1,-1,0","2,2;"})
        require(!parse_player_health_states(invalid,error),"invalid or untested health state is rejected");
    std::cout << "Player health state tests passed\n";
}
