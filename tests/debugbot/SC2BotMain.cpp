#include <iostream>
#include "sc2api/sc2_api.h"
#include "sc2api/sc2_args.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_manage_process.h"
#include "sc2utils/sc2_arg_parser.h"

#include "LadderInterface.h"

using namespace sc2;

class Bot : public Agent {
public:
    virtual void OnGameStart() final {
        std::cout << "Hello, World!" << std::endl;
    }

    virtual void OnStep() final {
        if (enemy_main_base.x == -1.0f && scout) {
            const ObservationInterface* observation = Observation();
            Units enemy_units = observation->GetUnits(Unit::Alliance::Enemy);
            if (enemy_units.size() > 0) {
                std::vector<Point2D> enemy_locs = observation->GetGameInfo().enemy_start_locations;
                float distance = std::numeric_limits<float>::max();
                for (int i = 0; i < enemy_locs.size(); ++i) {
                    float d = DistanceSquared2D(enemy_units[0]->pos, enemy_locs[i]);
                    if (d < distance) {
                        distance = d;
                        enemy_main_base = enemy_locs[i];
                    }
                }
            }
        }


        TryBuildSupplyDepot();

        TryBuildBarracks();

        TryBuildRefinery();
    }

    virtual void OnUnitCreated(const Unit*) final {
    }

    virtual void OnUnitIdle(const Unit* unit) final {
        switch (unit->unit_type.ToType()) {
        case UNIT_TYPEID::TERRAN_COMMANDCENTER: {
            if (CountUnitType(UNIT_TYPEID::TERRAN_SCV) < 20) {
                Actions()->UnitCommand(unit, ABILITY_ID::TRAIN_SCV);
            }
            break;
        }
        case UNIT_TYPEID::TERRAN_SCV: {
            const Unit* mineral_target = FindNearest(unit->pos, UNIT_TYPEID::NEUTRAL_MINERALFIELD);
            if (!mineral_target) {
                break;
            }
            Actions()->UnitCommand(unit, ABILITY_ID::SMART, mineral_target);
            break;
        }
        case UNIT_TYPEID::TERRAN_BARRACKS: {
            Actions()->UnitCommand(unit, ABILITY_ID::BUILD_REACTOR_BARRACKS);
            Actions()->UnitCommand(unit, ABILITY_ID::TRAIN_MARINE);
            break;
        }
        case UNIT_TYPEID::TERRAN_MARINE: {
            if (CountUnitType(UNIT_TYPEID::TERRAN_MARINE) > 20) {
                Actions()->UnitCommand(unit, ABILITY_ID::ATTACK_ATTACK, enemy_main_base);
            }
            break;
        }
        default: {
            break;
        }
        }
    }

private:
    bool TryBuildStructure(ABILITY_ID ability_type_for_structure, const Unit *target = nullptr,
        UNIT_TYPEID unit_type = UNIT_TYPEID::TERRAN_SCV) {
        const ObservationInterface* observation = Observation();

        // If a unit already is building a supply structure of this type, do nothing.
        // Also get an scv to build the structure.
        const Unit* unit_to_build = nullptr;
        Units units = observation->GetUnits(Unit::Alliance::Self);
        for (const auto& unit : units) {
            if (unit->tag == scout) {
                continue;
            }
            for (const auto& order : unit->orders) {
                if (order.ability_id == ability_type_for_structure) {
                    return false;
                }
            }
            if (unit->unit_type == unit_type) {
                unit_to_build = unit;
            }
        }

        float rx = GetRandomScalar();
        float ry = GetRandomScalar();

        if (!target) {
            Actions()->UnitCommand(unit_to_build,
                ability_type_for_structure,
                Point2D(unit_to_build->pos.x + rx * 15.0f, unit_to_build->pos.y + ry * 15.0f));
            if (!scouting) {

                const GameInfo& game_info = Observation()->GetGameInfo();
                std::cout << "size: " << game_info.enemy_start_locations.size() << std::endl;
                for (int i = 0; i < game_info.enemy_start_locations.size(); ++i) {
                    Actions()->UnitCommand(unit_to_build, ABILITY_ID::ATTACK, game_info.enemy_start_locations[i], true);
                }
                if (unit_to_build->orders.size() != 0) {
                    scout = unit_to_build->tag;
                    std::cout << "now tag: " << scout << std::endl;
                    scouting = true;
                }

            }
            return true;
        }
        else {
            Actions()->UnitCommand(unit_to_build,
                ability_type_for_structure,
                target);
            return true;
        }

    }

    bool TryBuildSupplyDepot() {
        const ObservationInterface* observation = Observation();

        // If we are not supply capped, don't build a supply depot.
        if (observation->GetFoodUsed() <= observation->GetFoodCap() - 2)
            return false;

        // Try and build a depot. Find a random SCV and give it the order.
        return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT);
    }

    bool TryBuildBarracks() {
        const ObservationInterface* observation = Observation();

        if (CountUnitType(UNIT_TYPEID::TERRAN_SUPPLYDEPOT) < 1) {
            return false;
        }

        if (CountUnitType(UNIT_TYPEID::TERRAN_BARRACKS) > 0 && CountUnitType(UNIT_TYPEID::TERRAN_SUPPLYDEPOT) < 2) {
            return false;
        }

        if (CountUnitType(UNIT_TYPEID::TERRAN_BARRACKS) > 3) {
            return false;
        }

        return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS);
    }

    bool TryBuildRefinery() {
        const ObservationInterface* observation = Observation();

        if (CountUnitType(UNIT_TYPEID::TERRAN_REFINERY) > 0) {
            return false;
        }
        if (CountUnitType(UNIT_TYPEID::TERRAN_BARRACKS) < 1) {
            return false;
        }

        const Unit* unit_to_build = nullptr;
        Units units = observation->GetUnits(Unit::Alliance::Self);
        const Unit* refinery_target;
        Point2D refine_location;
        for (const auto& unit : units) {
            if (unit->unit_type.ToType() == UNIT_TYPEID::TERRAN_COMMANDCENTER) {
                refinery_target = FindNearest(unit->pos, UNIT_TYPEID::NEUTRAL_VESPENEGEYSER);
                break;
            }
        }

        if (!refinery_target) {
            return false;
        }

        return TryBuildStructure(ABILITY_ID::BUILD_REFINERY, refinery_target);
    }

    const Unit* FindNearest(const Point2D& start, UNIT_TYPEID building) {
        Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
        float distance = std::numeric_limits<float>::max();
        const Unit* target = nullptr;
        for (const auto& u : units) {
            if (u->unit_type == building) {
                float d = DistanceSquared2D(u->pos, start);
                if (d < distance) {
                    distance = d;
                    target = u;
                }
            }
        }
        return target;
    }

    size_t CountUnitType(UNIT_TYPEID unit_type) {
        return Observation()->GetUnits(Unit::Alliance::Self, IsUnit(unit_type)).size();
    }

    Tag scout = NullTag;
    bool scouting = false;
    Point2D enemy_main_base = Point2D(-1.0f, -1.0f);
};

int main(int argc, char* argv[]) {
    Coordinator coordinator;
    coordinator.LoadSettings(argc, argv);

    Bot bot1;
    Bot bot2;
    coordinator.SetParticipants({ CreateParticipant(sc2::Race::Terran, &bot1),
                                 CreateComputer(sc2::Race::Random) });

    coordinator.LaunchStarcraft();
    coordinator.StartGame(sc2::kMapCactusValleyLE);

    while (coordinator.Update()) {
    }
    return 0;
}
