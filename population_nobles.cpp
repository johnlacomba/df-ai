#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"
#include "debug.h"

#include "modules/Items.h"
#include "modules/Units.h"

#include "df/activity_info.h"
#include "df/building_civzonest.h"
#include "df/dipscript_popup.h"
#include "df/entity_buy_prices.h"
#include "df/entity_buy_requests.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/entity_position_responsibility.h"
#include "df/gamest.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/history_event_add_hf_entity_linkst.h"
#include "df/item_type.h"
#include "df/job_material_category.h"
#include "df/meeting_diplomat_info.h"
#include "df/meeting_event.h"
#include "df/meeting_event_type.h"
#include "df/meeting_topic.h"
#include "df/unit.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/squad.h"
#include "df/squad_schedule_order.h"
#include "df/plotinfost.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(game);
REQUIRE_GLOBAL(hist_event_next_id);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

bool Population::unit_hasmilitaryduty(df::unit *u)
{
    if (u->military.squad_id == -1)
    {
        return false;
    }
    // squad::cur_alert_idx removed in Steam DF — squad schedule/alert structure changed
    // TODO: rewrite using new squad schedule API
    // For now, assume any squad member has military duty
    return true;
}

int32_t Population::unit_totalxp(const df::unit *u)
{
    int32_t t = 0;
    if (!u->status.current_soul)
        return t;
    for (auto sk : u->status.current_soul->skills)
    {
        int32_t rat = sk->rating;
        t += 400 * rat + 100 * rat * (rat + 1) / 2 + sk->experience;
    }
    return t;
}

class AssignNoblesExclusive : public ExclusiveCallback
{
    AI & ai;
    df::entity_position_responsibility responsibility;

public:
    AssignNoblesExclusive(AI & ai, df::entity_position_responsibility responsibility) :
        ExclusiveCallback("assign noble position with responsibility " + enum_item_key(responsibility)),
        ai(ai),
        responsibility(responsibility)
    {
    }
    ~AssignNoblesExclusive() {}

    void Run(color_ostream & out)
    {
        bool bookkeeper = responsibility == entity_position_responsibility::ACCOUNTING;
        auto entity = plotinfo->main.fortress_entity;

        std::vector<df::unit *> candidates;
        for (auto u : world->units.active)
        {
            if (!Units::isCitizen(u) || Units::isChild(u) || Units::isBaby(u))
                continue;
            if (bookkeeper && u->status.labors[unit_labor::MINE])
                continue;
            std::vector<Units::NoblePosition> positions;
            if (u->mood == mood_type::None && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
                candidates.push_back(u);
        }
        if (candidates.empty())
        {
            ai.debug(out, "Cannot assign noble position for " + enum_item_key(responsibility) + ": no candidates");
            return;
        }
        std::sort(candidates.begin(), candidates.end(), [this](df::unit *a, df::unit *b) -> bool
        {
            return ai.pop.unit_totalxp(a) > ai.pop.unit_totalxp(b);
        });

        // find a position definition with this responsibility
        df::entity_position *target_pos = nullptr;
        for (auto pos : entity->positions.own)
        {
            if (pos && pos->responsibilities[responsibility])
            {
                target_pos = pos;
                break;
            }
        }
        if (!target_pos)
        {
            ai.debug(out, "No position definition with responsibility " + enum_item_key(responsibility));
            return;
        }

        // find an existing vacant assignment for this position, or create one
        df::entity_position_assignment *asn = nullptr;
        for (auto a : entity->positions.assignments)
        {
            if (!a)
                continue;
            if (a->position_id != target_pos->id)
                continue;
            auto hf = df::historical_figure::find(a->histfig);
            if (hf && hf->died_year == -1)
            {
                // position held by a living person — already filled
                ai.debug(out, enum_item_key(responsibility) + " position already held by " + AI::describe_unit(df::unit::find(hf->unit_id)));
                return;
            }
            asn = a;
            break;
        }

        if (!asn)
        {
            // no assignment slot exists — create one
            asn = new df::entity_position_assignment();
            int32_t max_id = 0;
            for (auto a : entity->positions.assignments)
            {
                if (a && a->id >= max_id)
                    max_id = a->id + 1;
            }
            asn->id = max_id;
            asn->position_id = target_pos->id;
            asn->histfig = -1;
            asn->histfig2 = -1;
            entity->positions.assignments.push_back(asn);
            ai.debug(out, "Created assignment slot for " + target_pos->name[0]);
        }

        for (auto candidate : candidates)
        {
            if (candidate->hist_figure_id == -1)
                continue;

            auto hf = df::historical_figure::find(candidate->hist_figure_id);
            if (!hf)
                continue;

            ai.debug(out, "Appointing " + AI::describe_unit(candidate) + " as " + target_pos->name[0]);

            // clean up old histfig's position link if assignment was previously held
            if (asn->histfig != -1)
            {
                if (auto old_hf = df::historical_figure::find(asn->histfig))
                {
                    for (size_t i = 0; i < old_hf->entity_links.size(); i++)
                    {
                        if (old_hf->entity_links[i]->getType() != df::histfig_entity_link_type::POSITION)
                            continue;
                        auto pos_link = strict_virtual_cast<df::histfig_entity_link_positionst>(old_hf->entity_links[i]);
                        if (pos_link && pos_link->assignment_id == asn->id && pos_link->entity_id == entity->id)
                        {
                            old_hf->entity_links.erase(old_hf->entity_links.begin() + i);
                            delete pos_link;
                            break;
                        }
                    }
                }
            }

            asn->histfig = candidate->hist_figure_id;

            // create position link on the new histfig
            auto link = df::allocate<df::histfig_entity_link_positionst>();
            if (link)
            {
                link->entity_id = entity->id;
                link->assignment_id = asn->id;
                link->start_year = *cur_year;
                link->link_strength = 100;
                hf->entity_links.push_back(link);
            }

            // generate history event so DF's internal state stays consistent
            auto event = df::allocate<df::history_event_add_hf_entity_linkst>();
            if (event)
            {
                event->id = (*hist_event_next_id)++;
                event->year = *cur_year;
                event->seconds = *cur_year_tick;
                event->civ = entity->id;
                event->histfig = hf->id;
                event->link_type = df::histfig_entity_link_type::POSITION;
                event->position_id = target_pos->id;
                event->appointer_hfid = -1;
                event->promise_to_hfid = -1;
                world->history.events.push_back(event);
            }

            for (int r = 0; r <= ENUM_LAST_ITEM(entity_position_responsibility); r++)
            {
                auto resp = static_cast<df::entity_position_responsibility>(r);
                if (target_pos->responsibilities[resp])
                    entity->assignments_by_type[resp].push_back(asn);
            }

            if (bookkeeper)
                plotinfo->nobles.bookkeeper_settings = static_cast<df::record_precision_level_type>(4);

            return;
        }
        ai.debug(out, "Could not find eligible candidate for " + enum_item_key(responsibility));
    }
};

void Population::update_nobles(color_ostream & out)
{
    check_noble_apartments(out);

    if (!config.manage_nobles)
    {
        return;
    }

    for (auto & asn : plotinfo->main.fortress_entity->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT])
    {
        auto hf = df::historical_figure::find(asn->histfig);
        if (!hf)
            continue;
        auto doctor = df::unit::find(hf->unit_id);
        if (!doctor)
            continue;

        doctor->status.labors[unit_labor::DIAGNOSE] = true;
        doctor->status.labors[unit_labor::SURGERY] = true;
        doctor->status.labors[unit_labor::BONE_SETTING] = true;
        doctor->status.labors[unit_labor::SUTURING] = true;
        doctor->status.labors[unit_labor::DRESSING_WOUNDS] = true;
    }

    if (ai.find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }))
    {
        int32_t target_doctors = std::max(2, (int32_t)citizen.size() / 10);
        medic.clear();

        for (auto id : citizen)
        {
            auto u = df::unit::find(id);
            if (!u || !Units::isSane(u))
                continue;
            if (u->status.labors[unit_labor::DIAGNOSE] ||
                u->status.labors[unit_labor::SURGERY] ||
                u->status.labors[unit_labor::BONE_SETTING] ||
                u->status.labors[unit_labor::SUTURING] ||
                u->status.labors[unit_labor::DRESSING_WOUNDS])
            {
                medic.insert(id);
            }
        }

        if ((int32_t)medic.size() < target_doctors)
        {
            for (auto id : citizen)
            {
                if ((int32_t)medic.size() >= target_doctors)
                    break;
                if (medic.count(id))
                    continue;
                auto u = df::unit::find(id);
                if (!u || !Units::isSane(u))
                    continue;
                if (u->military.squad_id != -1)
                    continue;
                if (unit_hasmilitaryduty(u))
                    continue;

                u->status.labors[unit_labor::DIAGNOSE] = true;
                u->status.labors[unit_labor::SURGERY] = true;
                u->status.labors[unit_labor::BONE_SETTING] = true;
                u->status.labors[unit_labor::SUTURING] = true;
                u->status.labors[unit_labor::DRESSING_WOUNDS] = true;
                medic.insert(id);
                ai.debug(out, "assigned doctoring labors to " + AI::describe_unit(u));
            }
        }
    }

#define WANT_POS(pos) \
    if (plotinfo->main.fortress_entity->assignments_by_type[entity_position_responsibility::pos].empty()) \
    { \
        events.queue_exclusive(std::make_unique<AssignNoblesExclusive>(ai, entity_position_responsibility::pos)); \
    }

    WANT_POS(RECEIVE_DIPLOMATS);
    WANT_POS(MEET_WORKERS);
    WANT_POS(MANAGE_PRODUCTION);
    WANT_POS(ACCOUNTING);
    if (ai.find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }))
    {
        WANT_POS(HEALTH_MANAGEMENT);
    }
    WANT_POS(TRADE);
    WANT_POS(MILITARY_STRATEGY);
    if (ai.find_room(room_type::jail, [](room *r) -> bool { return r->status == room_status::finished; }))
    {
        WANT_POS(LAW_ENFORCEMENT);
        WANT_POS(EXECUTIONS);
    }
}

void Population::check_noble_apartments(color_ostream & out)
{
    std::set<int32_t> noble_ids;

    for (auto asn : plotinfo->main.fortress_entity->positions.assignments)
    {
        if (!asn)
            continue;
        df::entity_position *pos = binsearch_in_vector(plotinfo->main.fortress_entity->positions.own, asn->position_id);
        if (!pos)
            continue;
        if (pos->required_office > 0 || pos->required_dining > 0 || pos->required_tomb > 0)
        {
            if (df::historical_figure *hf = df::historical_figure::find(asn->histfig))
            {
                noble_ids.insert(hf->unit_id);
            }
        }
    }

    // also ensure nobles with RECEIVE_DIPLOMATS get an office for diplomat meetings
    for (auto *asn : plotinfo->main.fortress_entity->assignments_by_type[entity_position_responsibility::RECEIVE_DIPLOMATS])
    {
        if (asn && asn->histfig != -1)
        {
            if (auto *hf = df::historical_figure::find(asn->histfig))
            {
                noble_ids.insert(hf->unit_id);
            }
        }
    }

    ai.plan.attribute_noblerooms(out, noble_ids);
}

void Population::update_diplomacy(color_ostream & out)
{
    if (!game)
        return;

    std::vector<df::meeting_diplomat_info *> pending;
    for (auto dipev : plotinfo->dip_meeting_info)
    {
        if (dipev && !dipev->flags.bits.failure && !dipev->flags.bits.success)
            pending.push_back(dipev);
    }

    static int32_t last_diplo_trace_tick = -1;
    bool diplo_trace = (*cur_year_tick - last_diplo_trace_tick >= 400) || last_diplo_trace_tick == -1;

    if (pending.empty())
    {
        if (diplo_trace && !plotinfo->dip_meeting_info.empty())
        {
            ai.debug(out, stl_sprintf("[DIPLO] all %zu meetings already resolved (success/failure)", plotinfo->dip_meeting_info.size()));
            last_diplo_trace_tick = *cur_year_tick;
        }
        return;
    }

    if (diplo_trace)
    {
        ai.debug(out, stl_sprintf("[DIPLO] %zu pending meetings", pending.size()));
        last_diplo_trace_tick = *cur_year_tick;
    }

    if (game->main_interface.diplomacy.open)
        return;

    auto entity = plotinfo->main.fortress_entity;

    df::unit *noble_unit = nullptr;
    for (auto asn : entity->positions.assignments)
    {
        if (!asn || asn->histfig == -1)
            continue;
        auto pos = binsearch_in_vector(entity->positions.own, asn->position_id);
        if (!pos || !pos->responsibilities[entity_position_responsibility::RECEIVE_DIPLOMATS])
            continue;
        auto hf = df::historical_figure::find(asn->histfig);
        noble_unit = hf ? df::unit::find(hf->unit_id) : nullptr;
        break;
    }

    static int32_t last_error_tick = -1;

    if (!noble_unit)
    {
        if (*cur_year_tick - last_error_tick >= 1200 || last_error_tick == -1)
        {
            ai.debug(out, "[DIPLO] no noble with RECEIVE_DIPLOMATS — cannot conduct meeting");
            last_error_tick = *cur_year_tick;
        }
        return;
    }

    df::building_civzonest *office = nullptr;
    for (auto bld : noble_unit->owned_buildings)
    {
        auto zone = virtual_cast<df::building_civzonest>(bld);
        if (zone && zone->type == civzone_type::Office)
        {
            office = zone;
            break;
        }
    }

    if (!office)
    {
        static int32_t last_no_office_tick = -1;
        if (*cur_year_tick - last_no_office_tick >= 1200 || last_no_office_tick == -1)
        {
            ai.debug(out, "[DIPLO] noble " + AI::describe_unit(noble_unit) + " has no office — meeting may proceed without one");
            last_no_office_tick = *cur_year_tick;
        }
    }

    // throttle routine status logs — log once per state change or every ~200 ticks
    static std::map<int32_t, int8_t> last_logged_state;
    static int32_t last_status_tick = -1;
    bool do_status_log = (*cur_year_tick - last_status_tick >= 200) || last_status_tick == -1;

    for (auto dipev : pending)
    {
        auto diplomat_hf = df::historical_figure::find(dipev->diplomat_id);
        auto diplomat_unit = diplomat_hf ? df::unit::find(diplomat_hf->unit_id) : nullptr;

        if (!diplomat_unit || !Units::isAlive(diplomat_unit))
        {
            if (diplo_trace)
            {
                ai.debug(out, stl_sprintf("[DIPLO] skipping meeting: diplomat_id=%d hf=%s unit=%s",
                    dipev->diplomat_id,
                    diplomat_hf ? "found" : "NOT FOUND",
                    diplomat_unit ? (Units::isAlive(diplomat_unit) ? "alive" : "DEAD") : "NOT FOUND"));
            }
            continue;
        }

        int state_val = static_cast<int>(diplomat_unit->meeting.state);
        bool state_changed = !last_logged_state.count(diplomat_unit->id) ||
            last_logged_state[diplomat_unit->id] != state_val;

        if (state_changed || do_status_log)
        {
            const char *state_names[] = { "SelectNoble", "FollowNoble", "DoMeeting", "LeaveMap" };
            const char *state_name = (state_val >= 0 && state_val <= 3) ? state_names[state_val] : "unknown";

            if (office)
            {
                ai.debug(out, "[DIPLO] diplomat " + AI::describe_unit(diplomat_unit) +
                    stl_sprintf(" state=%s(%d) target_role=%d pos=(%d,%d,%d) noble_office=(%d,%d,%d)",
                        state_name, state_val,
                        static_cast<int>(diplomat_unit->meeting.target_role),
                        diplomat_unit->pos.x, diplomat_unit->pos.y, diplomat_unit->pos.z,
                        office->centerx, office->centery, office->z));
            }
            else
            {
                ai.debug(out, "[DIPLO] diplomat " + AI::describe_unit(diplomat_unit) +
                    stl_sprintf(" state=%s(%d) target_role=%d pos=(%d,%d,%d) (no office)",
                        state_name, state_val,
                        static_cast<int>(diplomat_unit->meeting.target_role),
                        diplomat_unit->pos.x, diplomat_unit->pos.y, diplomat_unit->pos.z));
            }
            last_logged_state[diplomat_unit->id] = static_cast<int8_t>(state_val);
            last_status_tick = *cur_year_tick;
        }

        if (office)
        {
            bool has_activity = false;
            for (auto act : plotinfo->activities)
            {
                if (act && act->unit_actor == diplomat_unit->id)
                {
                    has_activity = true;
                    if (act->place != office->id)
                    {
                        act->place = office->id;
                        ai.debug(out, "[DIPLO] corrected activity place to current office");
                    }
                    break;
                }
            }

            if (!has_activity && state_val <= 1)
            {
                auto act = df::allocate<df::activity_info>();
                if (act)
                {
                    act->unit_actor = diplomat_unit->id;
                    act->unit_noble = noble_unit->id;
                    act->place = office->id;
                    act->flags.whole = 0;
                    plotinfo->activities.push_back(act);
                    ai.debug(out, "[DIPLO] created meeting activity: " +
                        AI::describe_unit(diplomat_unit) + " at office " +
                        stl_sprintf("(%d,%d,%d)", office->centerx, office->centery, office->z));
                }
            }
        }

        // only complete the meeting once the diplomat reaches DoMeeting state
        if (state_val == 2)
        {
            auto civ_entity = df::historical_entity::find(dipev->civ_id);

            int32_t anvil_count = ai.stocks.count_free[stock_item::anvil];
            int32_t anvil_needed = ai.stocks.num_needed(stock_item::anvil);
            if (anvil_count < anvil_needed && civ_entity)
            {
                auto *buy_req = df::allocate<df::entity_buy_requests>();
                if (buy_req)
                {
                    buy_req->item_type.push_back(df::item_type::ANVIL);
                    buy_req->item_subtype.push_back(-1);
                    buy_req->mat_types.push_back(-1);
                    buy_req->mat_indices.push_back(-1);
                    df::job_material_category cat;
                    cat.whole = 0;
                    buy_req->mat_cats.push_back(cat);
                    buy_req->priority.push_back(4);

                    auto *buy_prices = df::allocate<df::entity_buy_prices>();
                    if (buy_prices)
                    {
                        buy_prices->items = buy_req;
                        buy_prices->price.push_back(192);

                        auto *event = df::allocate<df::meeting_event>();
                        if (event)
                        {
                            event->type = df::meeting_event_type::ExportAgreement;
                            event->topic = df::meeting_topic::ExportAgreement;
                            event->buy_prices = buy_prices;
                            event->sell_prices = nullptr;
                            event->year = *cur_year;
                            event->ticks = *cur_year_tick;
                            event->topic_parm = -1;
                            event->quota_total = -1;
                            event->quota_remaining = -1;
                            civ_entity->meeting_events.push_back(event);
                            ai.debug(out, stl_sprintf("[DIPLO] requested anvils from caravan (have %d, need %d)",
                                anvil_count, anvil_needed));
                        }
                        else
                        {
                            delete buy_req;
                            delete buy_prices;
                        }
                    }
                    else
                    {
                        delete buy_req;
                    }
                }
            }

            // remove dipscript_popups for this diplomat so DF doesn't
            // wait for the player to click through the dialog
            for (size_t pi = 0; pi < plotinfo->dipscript_popups.size(); )
            {
                auto *popup = plotinfo->dipscript_popups[pi];
                if (popup && popup->meeting_holder_actor == diplomat_unit->id)
                {
                    plotinfo->dipscript_popups.erase(plotinfo->dipscript_popups.begin() + pi);
                    delete popup;
                }
                else
                {
                    pi++;
                }
            }

            dipev->flags.bits.success = true;
            ai.debug(out, "[DIPLO] completed diplomacy meeting for " +
                AI::describe_unit(diplomat_unit) +
                (civ_entity ? " (civ: " + std::to_string(civ_entity->id) + ")" : ""));
            return;
        }
    }
}
