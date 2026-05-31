#include "ai.h"
#include "population.h"
#include "plan.h"
#include "debug.h"

#include "modules/Units.h"

#include "df/entity_position_assignment.h"
#include "df/unit.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/squad.h"
#include "df/squad_schedule_order.h"
#include "df/plotinfost.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year_tick);
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

        for (auto asn : plotinfo->main.fortress_entity->positions.assignments)
        {
            if (!asn)
                continue;

            auto position = binsearch_in_vector(plotinfo->main.fortress_entity->positions.own, asn->position_id);
            if (!position || !position->responsibilities[responsibility])
                continue;

            auto hf = df::historical_figure::find(asn->histfig);
            if (hf && hf->died_year == -1)
                continue;

            for (auto candidate : candidates)
            {
                if (candidate->hist_figure_id == -1)
                    continue;

                ai.debug(out, "Appointing " + AI::describe_unit(candidate) + " as " + position->name[0]);
                asn->histfig = candidate->hist_figure_id;

                if (bookkeeper)
                    plotinfo->nobles.bookkeeper_settings = static_cast<df::record_precision_level_type>(4);

                return;
            }
        }
        ai.debug(out, "Could not find position for " + enum_item_key(responsibility));
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

    WANT_POS(MANAGE_PRODUCTION);
    WANT_POS(ACCOUNTING);
    if (ai.find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }))
    {
        WANT_POS(HEALTH_MANAGEMENT);
    }
    WANT_POS(TRADE);
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

    ai.plan.attribute_noblerooms(out, noble_ids);
}
