#include "ai.h"
#include "population.h"
#include "room.h"
#include "debug.h"

#include "modules/Units.h"

#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/building.h"
#include "df/occupation.h"
#include "df/plotinfost.h"
#include "df/world.h"
#include "df/world_site.h"

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

// with a population of 200:
const static int32_t wanted_tavern_keeper = 4;
const static int32_t wanted_tavern_keeper_min = 1;
const static int32_t wanted_tavern_performer = 8;
const static int32_t wanted_tavern_performer_min = 0;
const static int32_t wanted_library_scholar = 16;
const static int32_t wanted_library_scholar_min = 0;
const static int32_t wanted_library_scribe = 2;
const static int32_t wanted_library_scribe_min = 0;
const static int32_t wanted_temple_performer = 4;
const static int32_t wanted_temple_performer_min = 0;

static bool occupation_warned = false;
static bool petition_warned = false;

void Population::update_locations(color_ostream & out)
{
    if (!plotinfo->petitions.empty() && !petition_warned)
    {
        ai.debug(out, stl_sprintf("petition handling deferred (Steam DF not yet implemented): %zu pending", plotinfo->petitions.size()));
        petition_warned = true;
    }

    // occupation assignment deferred — just count needs for future implementation
    // (previously spawned ExclusiveCallback threads that did nothing)
    if (!occupation_warned)
    {
#define INIT_NEED(name) int32_t need_##name = std::max(wanted_##name * int32_t(citizen.size()) / 200, wanted_##name##_min)
        INIT_NEED(tavern_keeper);
        INIT_NEED(tavern_performer);
        INIT_NEED(library_scholar);
        INIT_NEED(library_scribe);
        INIT_NEED(temple_performer);
#undef INIT_NEED

        auto check_location_occupations = [&](room_type::type rtype, location_type::type ltype, auto cast_fn, auto count_fn)
        {
            if (room *r = ai.find_room(rtype, [ltype](room *r) -> bool { return r->location_type == ltype && r->dfbuilding(); }))
            {
                df::building *bld = r->dfbuilding();
                auto site = df::world_site::find(bld->site_id);
                if (site)
                {
                    if (auto loc = cast_fn(binsearch_in_vector(site->buildings, bld->location_id)))
                    {
                        for (auto occ : loc->occupations)
                        {
                            if (occ->unit_id != -1)
                            {
                                count_fn(occ);
                            }
                        }
                    }
                }
            }
        };

        check_location_occupations(room_type::location, location_type::tavern,
            [](df::abstract_building *b) { return virtual_cast<df::abstract_building_inn_tavernst>(b); },
            [&](df::occupation *occ)
            {
                if (occ->type == occupation_type::TAVERN_KEEPER) need_tavern_keeper--;
                else if (occ->type == occupation_type::PERFORMER) need_tavern_performer--;
            });

        check_location_occupations(room_type::location, location_type::library,
            [](df::abstract_building *b) { return virtual_cast<df::abstract_building_libraryst>(b); },
            [&](df::occupation *occ)
            {
                if (occ->type == occupation_type::SCHOLAR) need_library_scholar--;
                else if (occ->type == occupation_type::SCRIBE) need_library_scribe--;
            });

        check_location_occupations(room_type::location, location_type::temple,
            [](df::abstract_building *b) { return virtual_cast<df::abstract_building_templest>(b); },
            [&](df::occupation *occ)
            {
                if (occ->type == occupation_type::PERFORMER) need_temple_performer--;
            });

        bool any_needed = need_tavern_keeper > 0 || need_tavern_performer > 0 ||
            need_library_scholar > 0 || need_library_scribe > 0 || need_temple_performer > 0;
        if (any_needed)
        {
            ai.debug(out, "occupation assignment deferred (Steam DF not yet implemented)");
            occupation_warned = true;
        }
    }
}
