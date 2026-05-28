#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Units.h"

#include "df/squad.h"
#include "df/plotinfost.h"
#include "df/world.h"

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

void Population::update_military(color_ostream &)
{
}

bool Population::military_random_squad_attack_unit(color_ostream &, df::unit *, const std::string &)
{
    return false;
}

bool Population::military_all_squads_attack_unit(color_ostream &, df::unit *, const std::string &)
{
    return false;
}

bool Population::military_squad_attack_unit(color_ostream &, df::squad *, df::unit *, const std::string &)
{
    return false;
}

bool Population::military_cancel_attack_order(color_ostream &, df::unit *, const std::string &)
{
    return false;
}

bool Population::military_cancel_attack_order(color_ostream &, df::squad *, df::unit *, const std::string &)
{
    return false;
}
