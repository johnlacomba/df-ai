#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "plan.h"
#include "population.h"

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Units.h"

#include "df/building_civzonest.h"
#include "df/d_init.h"
#include "df/entity_position.h"
#include "df/unit.h"
#include "df/entity_position_assignment.h"
#include "df/entity_position_responsibility.h"
#include "df/gamest.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/meeting_diplomat_info.h"
#include "df/plotinfost.h"
#include "df/popup_message.h"
#include "df/report.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(d_init);
REQUIRE_GLOBAL(game);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

void AI::unpause()
{
    if (!world->status.popups.empty())
    {
        for (auto popup : world->status.popups)
        {
            delete popup;
        }
        world->status.popups.clear();
    }

    if (game && game->main_interface.announcement_alert.open)
    {
        game->main_interface.announcement_alert.open = false;
        game->main_interface.announcement_alert.viewing_alert = nullptr;
    }

    if (game && game->main_interface.diplomacy.open)
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::SELECT);
        if (*pause_state)
        {
            Gui::getCurViewscreen(true)->feed_key(interface_key::D_PAUSE);
        }
        return;
    }

    if (game && game->main_interface.petitions.open)
    {
        game->main_interface.petitions.open = false;
    }

    if (*pause_state)
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::D_PAUSE);
    }
    ignore_pause(last_good_x, last_good_y, last_good_z);
}

void AI::handle_pause_event(color_ostream & out, df::report *announce)
{
    std::string fulltext = announce->text;
    auto idx = std::find(world->status.announcements.rbegin(), world->status.announcements.rend(), announce);
    while (announce->flags.bits.continuation)
    {
        idx++;
        if (idx == world->status.announcements.rend())
            break;
        announce = *idx;
        fulltext = announce->text + " " + fulltext;
    }
    if (announce->repeat_count)
    {
        fulltext += stl_sprintf(" x%d", announce->repeat_count + 1);
    }
    debug(out, "pause: " + fulltext);

    switch (announce->type)
    {
    case announcement_type::MEGABEAST_ARRIVAL:
    {
        if (!tag_enemies(out))
        {
            debug(out, "[ERROR] could not find megabeast");
        }
        break;
    }
    case announcement_type::UNDEAD_ATTACK:
        if (!tag_enemies(out))
        {
            debug(out, "[ERROR] could not find enemy combatants");
        }
        break;
    case announcement_type::BERSERK_CITIZEN:
    case announcement_type::CAVE_COLLAPSE:
        break;
    case announcement_type::DIG_CANCEL_DAMP:
    case announcement_type::DIG_CANCEL_WARM:
        ignore_pause(last_good_x, last_good_y, last_good_z);
        break;
    case announcement_type::BIRTH_CITIZEN:
    case announcement_type::BIRTH_ANIMAL:
        break;
    case announcement_type::D_MIGRANTS_ARRIVAL:
    case announcement_type::D_MIGRANT_ARRIVAL:
    case announcement_type::MIGRANT_ARRIVAL:
    case announcement_type::NOBLE_ARRIVAL:
    case announcement_type::FORT_POSITION_SUCCESSION:
        plan.make_map_walkable(out);
        break;
    case announcement_type::DIPLOMAT_ARRIVAL:
    case announcement_type::LIAISON_ARRIVAL:
    case announcement_type::TRADE_DIPLOMAT_ARRIVAL:
    {
        debug(out, "pause: diplomat/liaison arrived, ensuring map walkability");
        plan.make_map_walkable(out);

        auto entity = plotinfo->main.fortress_entity;
        debug(out, stl_sprintf("[DIAG] dip_meeting_info count: %zu, meeting_requests count: %zu",
            plotinfo->dip_meeting_info.size(), plotinfo->meeting_requests.size()));

        bool found_receive_diplomats = false;
        for (auto asn : entity->positions.assignments)
        {
            if (!asn || asn->histfig == -1)
                continue;
            auto pos = binsearch_in_vector(entity->positions.own, asn->position_id);
            if (!pos)
                continue;
            if (pos->responsibilities[entity_position_responsibility::RECEIVE_DIPLOMATS])
            {
                found_receive_diplomats = true;
                auto hf = df::historical_figure::find(asn->histfig);
                auto u = hf ? df::unit::find(hf->unit_id) : nullptr;
                debug(out, "[DIAG] RECEIVE_DIPLOMATS held by: " + (u ? AI::describe_unit(u) : "unknown unit") +
                    ", position: " + pos->name[0] +
                    ", required_office: " + std::to_string(pos->required_office));

                if (u)
                {
                    bool has_office = false;
                    for (auto bld : u->owned_buildings)
                    {
                        if (auto zone = virtual_cast<df::building_civzonest>(bld))
                        {
                            if (zone->type == civzone_type::Office)
                            {
                                has_office = true;
                                debug(out, stl_sprintf("[DIAG] noble has Office civzone id=%d at (%d,%d,%d)",
                                    zone->id, zone->centerx, zone->centery, zone->z));
                            }
                        }
                    }
                    if (!has_office)
                        debug(out, "[DIAG] WARNING: noble has NO Office civzone in owned_buildings");
                }
            }
        }
        if (!found_receive_diplomats)
        {
            debug(out, "[DIAG] WARNING: no position assignment with RECEIVE_DIPLOMATS found!");
            debug(out, stl_sprintf("[DIAG] assignments_by_type[RECEIVE_DIPLOMATS] count: %zu",
                entity->assignments_by_type[entity_position_responsibility::RECEIVE_DIPLOMATS].size()));
        }
        break;
    }
    case announcement_type::DIPLOMAT_LEFT_UNHAPPY:
    {
        auto entity = plotinfo->main.fortress_entity;
        debug(out, stl_sprintf("[DIAG] diplomat left unhappy. dip_meeting_info: %zu, meeting_requests: %zu, diplomacy.open: %d",
            plotinfo->dip_meeting_info.size(), plotinfo->meeting_requests.size(),
            game ? (int)game->main_interface.diplomacy.open : -1));
        for (auto asn : entity->positions.assignments)
        {
            if (!asn || asn->histfig == -1) continue;
            auto pos = binsearch_in_vector(entity->positions.own, asn->position_id);
            if (pos && pos->responsibilities[entity_position_responsibility::RECEIVE_DIPLOMATS])
            {
                auto hf = df::historical_figure::find(asn->histfig);
                auto u = hf ? df::unit::find(hf->unit_id) : nullptr;
                debug(out, "[DIAG] at departure, RECEIVE_DIPLOMATS held by: " + (u ? AI::describe_unit(u) : "?") +
                    ", pos: " + pos->name[0]);
                if (u)
                {
                    bool has_office = false;
                    for (auto bld : u->owned_buildings)
                        if (auto zone = virtual_cast<df::building_civzonest>(bld))
                            if (zone->type == civzone_type::Office)
                                has_office = true;
                    debug(out, stl_sprintf("[DIAG] has_office=%d, owned_buildings count=%zu", has_office, u->owned_buildings.size()));
                }
            }
        }
        break;
    }
    case announcement_type::CARAVAN_ARRIVAL:
    case announcement_type::FIRST_CARAVAN_ARRIVAL:
    case announcement_type::MONARCH_ARRIVAL:
    case announcement_type::HASTY_MONARCH:
    case announcement_type::SATISFIED_MONARCH:
    case announcement_type::MOUNTAINHOME:
    case announcement_type::FOOD_WARNING:
    case announcement_type::STRANGE_MOOD:
    case announcement_type::MOOD_BUILDING_CLAIMED:
    case announcement_type::ARTIFACT_BEGUN:
    case announcement_type::MADE_ARTIFACT:
    case announcement_type::FEATURE_DISCOVERY:
    case announcement_type::STRUCK_DEEP_METAL:
    case announcement_type::TRAINING_FULL_REVERSION:
    case announcement_type::NAMED_ARTIFACT:
    case announcement_type::DEITY_PRONOUNCEMENT:
    case announcement_type::EMBARK_MESSAGE:
        break;
    default:
    {
        const static std::string prefix("AMBUSH");
        std::string type(ENUM_KEY_STR(announcement_type, announce->type));
        if (std::mismatch(prefix.begin(), prefix.end(), type.begin()).first == prefix.end())
        {
            debug(out, "pause: an ambush!");
            if (!tag_enemies(out))
            {
                debug(out, "[ERROR] could not find enemy combatants");
            }
        }
        else
        {
            debug(out, "pause: unhandled pausing event " + type);
        }
        break;
    }
    }

    if (d_init->announcements.flags[announce->type].bits.DO_MEGA)
    {
        timeout_sameview([this](color_ostream &) { unpause(); });
    }
    else
    {
        unpause();
    }
}

void AI::statechanged(color_ostream & out, state_change_event st)
{
    if (st == SC_PAUSED)
    {
        auto la = std::find_if(world->status.announcements.rbegin(), world->status.announcements.rend(), [](df::report *a) -> bool
        {
            return d_init->announcements.flags[a->type].bits.PAUSE;
        });

        if (la != world->status.announcements.rend())
        {
            if ((*la)->year == *cur_year && (*la)->time == *cur_year_tick)
            {
                last_pause_id = (*la)->id;
                last_pause_repeats = (*la)->repeat_count;
                handle_pause_event(out, *la);
                return;
            }
            if ((*la)->id == last_pause_id && (*la)->repeat_count != last_pause_repeats)
            {
                last_pause_repeats = (*la)->repeat_count;
                handle_pause_event(out, *la);
                return;
            }
        }

        if (game && game->main_interface.diplomacy.open)
        {
            debug(out, "pause during diplomacy meeting, advancing dialog");
            Gui::getCurViewscreen(true)->feed_key(interface_key::SELECT);
            if (*pause_state)
            {
                Gui::getCurViewscreen(true)->feed_key(interface_key::D_PAUSE);
            }
            return;
        }

        // check for pending diplomat meetings — diplomat events in Steam DF
        // don't always set the PAUSE announcement flag, so we detect them here
        for (auto dipev : plotinfo->dip_meeting_info)
        {
            if (dipev && !dipev->flags.bits.failure && !dipev->flags.bits.success)
            {
                debug(out, "pause: pending diplomat meeting detected, unpausing to let update_diplomacy handle it");
                unpause();
                return;
            }
        }

        debug(out, "pause without an event");
        unpause();
    }
    else if (st == SC_VIEWSCREEN_CHANGED)
    {
        df::viewscreen *curview = Gui::getCurViewscreen(true);
        if (auto hack = dfhack_viewscreen::try_cast(curview))
        {
            std::string focus = hack->getFocusString();
            if (focus == "lua/status_overlay")
            {
                debug(out, "dismissing gui/extended-status overlay");
                Screen::dismiss(hack);
            }
            else if (focus == "lua/warn-starving")
            {
                debug(out, "exit warn-starving dialog");
                timeout_sameview([](color_ostream &)
                {
                    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                });
            }
            else if (seen_focus.insert(focus).second)
            {
                debug(out, "ignoring DFHack overlay (handled by embark loop): " + focus);
            }
        }
        else if (const virtual_identity *ident = virtual_identity::get(curview))
        {
            std::string cvname = ident->getName();
            if (seen_cvname.insert(cvname).second)
            {
                debug(out, "[ERROR] paused in unknown viewscreen " + cvname);
            }
        }
    }
}
