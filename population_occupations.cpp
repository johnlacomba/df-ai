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

class AssignOccupationExclusive : public ExclusiveCallback
{
    AI & ai;
    int32_t location_id;
    df::occupation_type occupation;

    static df::abstract_building *get_location(int32_t location_id)
    {
        auto site = plotinfo->main.fortress_site;
        if (!site)
            return nullptr;

        return binsearch_in_vector(site->buildings, location_id);
    }
    static std::string get_location_name(int32_t location_id)
    {
        auto location = get_location(location_id);
        if (!location)
            return "(unknown location)";

        auto name = location->getName();
        if (!name)
            return "(unnamed " + enum_item_key(location->getType()) + ")";

        return AI::describe_name(*name, true);
    }

public:
    AssignOccupationExclusive(AI & ai, int32_t location_id, df::occupation_type occupation) :
        ExclusiveCallback("assign new " + enum_item_key(occupation) + " at " + get_location_name(location_id)),
        ai(ai),
        location_id(location_id),
        occupation(occupation)
    {
    }

    void Run(color_ostream & out)
    {
        ai.debug(out, "occupation assignment deferred (Steam DF UI not yet implemented): " + enum_item_key(occupation) + " at " + get_location_name(location_id));
    }
};

class CheckPetitionsExclusive : public ExclusiveCallback
{
    AI & ai;

public:
    CheckPetitionsExclusive(AI & ai) :
        ExclusiveCallback("check petitions"),
        ai(ai)
    {
    }

    void Run(color_ostream & out)
    {
        ai.debug(out, "petition handling deferred (Steam DF UI not yet implemented)");
    }
};

void Population::update_locations(color_ostream &)
{
    if (!plotinfo->petitions.empty())
    {
        events.queue_exclusive(std::make_unique<CheckPetitionsExclusive>(ai));
    }

#define INIT_NEED(name) int32_t need_##name = std::max(wanted_##name * int32_t(citizen.size()) / 200, wanted_##name##_min)
    INIT_NEED(tavern_keeper);
    INIT_NEED(tavern_performer);
    INIT_NEED(library_scholar);
    INIT_NEED(library_scribe);
    INIT_NEED(temple_performer);
#undef INIT_NEED

    if (room *tavern = ai.find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern && r->dfbuilding(); }))
    {
        df::building *bld = tavern->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_inn_tavernst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::TAVERN_KEEPER)
                    {
                        need_tavern_keeper--;
                    }
                    else if (occ->type == occupation_type::PERFORMER)
                    {
                        need_tavern_performer--;
                    }
                }
            }
            if (need_tavern_keeper > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::TAVERN_KEEPER));
            }
            if (need_tavern_performer > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::PERFORMER));
            }
        }
    }

    if (room *library = ai.find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::library && r->dfbuilding(); }))
    {
        df::building *bld = library->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_libraryst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::SCHOLAR)
                    {
                        need_library_scholar--;
                    }
                    else if (occ->type == occupation_type::SCRIBE)
                    {
                        need_library_scribe--;
                    }
                }
            }
            if (need_library_scholar > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::SCHOLAR));
            }
            if (need_library_scribe > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::SCRIBE));
            }
        }
    }

    if (room *temple = ai.find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::temple && r->dfbuilding(); }))
    {
        df::building *bld = temple->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_templest>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::PERFORMER)
                    {
                        need_temple_performer--;
                    }
                }
            }
            if (need_temple_performer > 0)
            {
                events.queue_exclusive(std::make_unique<AssignOccupationExclusive>(ai, loc->id, occupation_type::PERFORMER));
            }
        }
    }
}
