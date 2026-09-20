#include "PlayerHealthState.hpp"
#include "CheckpointSchema.hpp"
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

    const PlayerHealthState ordinary{1,1,{}};
    const PlayerHealthState ordinary_buff{1,2,{{"mageOctavia_init",1,-1,0}}};
    const PlayerHealthState grown{2,2,{}};
    const PlayerHealthState grown_buff{2,3,{{"mageOctavia_init",1,-1,0}}};
    for (const int schema : {1,2,3})
    {
        require(validate_player_hand_health_for_definition(ordinary,1,schema,error)
                    && validate_player_hand_health_for_definition(ordinary_buff,1,schema,error),
                "all HAND schemas retain definition base HP with optional nonnegative modifiers");
        require(validate_player_hand_health_for_definition(grown,1,schema,error) == (schema == 3)
                    && validate_player_hand_health_for_definition(grown_buff,1,schema,error) == (schema == 3),
                "only HAND schema 3 permits base gains, alone or before HEALTH modifiers");
        require(!validate_player_hand_health_for_definition(ordinary,2,schema,error),
                "no HAND schema permits a base below the selected definition");
        require(validate_player_hand_health_for_definition({100000,100000,{}},100000,schema,error),
                "the existing positive base/maximum upper boundary remains inclusive");
        require(!validate_player_hand_health_for_definition((*parsed)[1],2,schema,error),
                "valid FIELD mixed modifiers remain outside every HAND schema");
    }
    require(validate_player_health_state((*parsed)[1],error),
            "the HAND-only policy does not narrow the existing FIELD health grammar");
    require(validate_player_hand_health_for_definition({100000,100000,{}},1,3,error),
            "schema 3 permits a bounded positive base gain at the existing scalar ceiling");
    for (const int definition : {-1,0,100001})
        require(!validate_player_hand_health_for_definition(grown,definition,3,error),
                "an invalid definition value cannot authorize a HAND base gain");
    for (const auto& invalid : std::vector<PlayerHealthState>{
             {0,1,{{"health",1,-1,0}}}, {-1,1,{{"health",2,-1,0}}},
             {100001,100001,{}}, {100000,100001,{{"health",1,-1,0}}},
             {2,4,{{"health",1,-1,0}}}, {2,1,{{"health",-1,-1,0}}},
             {2,3,{{"health",1,-1,8}}}})
        require(!validate_player_hand_health_for_definition(invalid,1,3,error),
                "HAND base replay retains positive, bounded, consistent and nonnegative HEALTH state");
    for (const int schema : {-1,0,ExactPlayerHandHealthSchemaVersion + 1})
        require(!validate_player_hand_health_for_definition(ordinary,1,schema,error),
                "unknown HAND schema versions cannot inherit dynamic base coverage");
    std::cout << "Player health state tests passed\n";
}
