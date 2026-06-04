#include "ai.h"
#include "population.h"
#include "room.h"
#include "debug.h"

#include "modules/Units.h"

#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/agreement.h"
#include "df/agreement_details.h"
#include "df/agreement_details_data_citizenship.h"
#include "df/agreement_details_data_location.h"
#include "df/agreement_details_data_residency.h"
#include "df/agreement_details_type.h"
#include "df/agreement_party.h"
#include "df/building.h"
#include "df/historical_figure.h"
#include "df/occupation.h"
#include "df/plotinfost.h"
#include "df/unit.h"
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

void Population::update_locations(color_ostream & out)
{
    if (!plotinfo->petitions.empty())
    {
        std::vector<int32_t> to_process(plotinfo->petitions.begin(), plotinfo->petitions.end());
        for (int32_t agr_id : to_process)
        {
            auto agr = df::agreement::find(agr_id);
            if (!agr || agr->details.empty())
                continue;

            auto detail = agr->details[0];
            std::string desc;
            bool should_accept = false;

            switch (detail->type)
            {
            case agreement_details_type::Residency:
            {
                desc = "residency";
                should_accept = true;
                auto &parties = agr->parties;
                auto res = detail->data.Residency;
                if (res && res->applicant >= 0 && res->applicant < (int32_t)parties.size() && parties[res->applicant])
                {
                    auto &hfids = parties[res->applicant]->histfig_ids;
                    if (!hfids.empty())
                    {
                        auto hf = df::historical_figure::find(hfids[0]);
                        auto u = hf ? df::unit::find(hf->unit_id) : nullptr;
                        if (u)
                            desc = "residency from " + AI::describe_unit(u);
                    }
                }
                break;
            }
            case agreement_details_type::Citizenship:
            {
                desc = "citizenship";
                should_accept = true;
                auto &parties = agr->parties;
                auto cit = detail->data.Citizenship;
                if (cit && cit->applicant >= 0 && cit->applicant < (int32_t)parties.size() && parties[cit->applicant])
                {
                    auto &hfids = parties[cit->applicant]->histfig_ids;
                    if (!hfids.empty())
                    {
                        auto hf = df::historical_figure::find(hfids[0]);
                        auto u = hf ? df::unit::find(hf->unit_id) : nullptr;
                        if (u)
                            desc = "citizenship from " + AI::describe_unit(u);
                    }
                }
                break;
            }
            case agreement_details_type::Location:
            {
                auto loc = detail->data.Location;
                if (loc)
                {
                    std::string loc_type = loc->type == abstract_building_type::TEMPLE ? "temple" : "guildhall";
                    desc = stl_sprintf("%s (tier %d)", loc_type.c_str(), loc->tier);
                }
                else
                {
                    desc = "location";
                }
                should_accept = true;
                break;
            }
            default:
                desc = "type " + std::to_string(static_cast<int>(detail->type));
                should_accept = false;
                break;
            }

            if (should_accept)
            {
                agr->flags.bits.petition_not_accepted = false;
                plotinfo->petitions.erase(
                    std::remove(plotinfo->petitions.begin(), plotinfo->petitions.end(), agr_id),
                    plotinfo->petitions.end());
                plotinfo->continuing_agreement_id.push_back(agr_id);
                ai.debug(out, "[PETITION] accepted petition: " + desc);
            }
            else
            {
                ai.debug(out, "[PETITION] ignoring petition: " + desc);
            }
        }
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
