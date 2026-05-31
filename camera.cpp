#include "ai.h"
#include "camera.h"
#include "debug.h"

#include <random>
#include <sstream>

#include "modules/Gui.h"
#include "modules/Maps.h"
#include "modules/Units.h"

#include "df/graphic.h"
#include "df/job.h"
#include "df/plotinfost.h"
#include "df/unit.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

Camera::Camera(AI & ai) :
    ai(ai),
    ontick_handle(nullptr),
    onupdate_handle(nullptr),
    onstatechange_handle(nullptr),
    tiers(),
    dwell_until(0),
    dwell_tier(-1),
    last_event_coord(),
    following(-1),
    following_prev(),
    follow_unit(-1),
    follow_item(-1),
    follow_stop(true),
    movie_started_in_lockstep(false)
{
}

void Camera::queue_event(int tier, df::coord pos, const std::string & description)
{
    if (tier < 0 || tier >= CAMERA_NUM_TIERS)
        return;
    if (!pos.isValid())
        return;
    auto *td = Maps::getTileDesignation(pos);
    if (!td || td->bits.hidden)
        return;
    if (static_cast<int>(tiers[tier].size()) >= CAMERA_TIER_CAP)
        tiers[tier].pop_back();
    tiers[tier].push_back({ pos, description });
}

Camera::~Camera()
{
    events.onupdate_unregister(onupdate_handle);
    events.onstatechange_unregister(onstatechange_handle);
}

command_result Camera::startup(color_ostream &)
{
    return CR_OK;
}

command_result Camera::onupdate_register(color_ostream &)
{
    if (config.fps_meter)
    {
        gps->display_frames = 1;
    }
    ontick_handle = events.onupdate_register("df-ai camera (every tick)", 1, 1, [this](color_ostream& out) { update_tick(out); });
    onupdate_handle = events.onupdate_register("df-ai camera", 500, 100, [this](color_ostream & out) { update(out); });
    onstatechange_handle = events.onstatechange_register("fps meter watcher", [this](color_ostream &, state_change_event mode)
    {
        if (config.fps_meter && mode == SC_VIEWSCREEN_CHANGED)
        {
            gps->display_frames = AI::is_dwarfmode_viewscreen() ? 1 : 0;
        }
    });
    check_record_status();
    return CR_OK;
}

void Camera::check_record_status()
{
    // Movie recording deferred (Steam DF removed supermovie fields from interfacest)
}

command_result Camera::onupdate_unregister(color_ostream &)
{
    if (config.fps_meter)
    {
        gps->display_frames = 0;
    }
    if (!config.no_quit && !config.random_embark)
    {
        Gui::getCurViewscreen(true)->breakdown_level = interface_breakdown_types::QUIT;
    }
    events.onupdate_unregister(ontick_handle);
    events.onupdate_unregister(onupdate_handle);
    events.onstatechange_unregister(onstatechange_handle);
    return CR_OK;
}

void Camera::update_tick(color_ostream &)
{
    if (plotinfo->follow_unit != -1 || plotinfo->follow_item != -1)
    {
        follow_stop = false;
        follow_unit = plotinfo->follow_unit;
        follow_item = plotinfo->follow_item;
    }
    else if (follow_stop)
    {
        follow_unit = -1;
        follow_item = -1;
    }
    else
    {
        follow_stop = true;
    }
}

void Camera::update(color_ostream &)
{
    if (!config.camera)
    {
        return;
    }

    // dwell timer: hold position until dwell expires or a higher-priority event arrives
    if (*cur_year_tick < dwell_until)
    {
        bool interrupted = false;
        for (int t = 0; t < dwell_tier; t++)
        {
            if (!tiers[t].empty())
            {
                DFAI_DEBUG(camera, 2, "dwell interrupted by tier " << t << " event");
                interrupted = true;
                break;
            }
        }
        if (!interrupted)
            return;
    }

    // consume events: drain highest-priority tier first
    for (int t = 0; t < CAMERA_NUM_TIERS; t++)
    {
        while (!tiers[t].empty())
        {
            CameraEvent ev = tiers[t].front();
            tiers[t].pop_front();

            int32_t vx, vy, vz;
            Gui::getViewCoords(vx, vy, vz);

            int32_t dx = ev.pos.x - vx;
            int32_t dy = ev.pos.y - vy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;

            if (dx <= CAMERA_ON_SCREEN_RADIUS && dy <= CAMERA_ON_SCREEN_RADIUS && ev.pos.z == vz)
            {
                DFAI_DEBUG(camera, 3, "event on-screen, skipping: " << ev.description);
                continue;
            }

            DFAI_DEBUG(camera, 1, "panning to event: " << ev.description << " at (" << ev.pos.x << "," << ev.pos.y << "," << ev.pos.z << ")");
            Gui::revealInDwarfmodeMap(ev.pos, true);
            plotinfo->follow_unit = -1;
            dwell_until = *cur_year_tick + 1000;
            dwell_tier = t;
            last_event_coord = ev.pos;

            world->status.flags.bits.combat = 0;
            world->status.flags.bits.hunting = 0;
            world->status.flags.bits.sparring = 0;
            return;
        }
    }

    // citizen fallback: all queues empty and dwell expired
    // if already following a citizen, keep following them for the full dwell period
    if (following != -1 && plotinfo->follow_unit == following)
    {
        dwell_until = *cur_year_tick + 2000;
        dwell_tier = CAMERA_TIER_CITIZEN;
        return;
    }

    std::vector<df::unit *> citizens;
    for (auto u : world->units.active)
    {
        if (!u->flags1.bits.inactive && Units::isCitizen(u) && !Units::isBaby(u) && !u->flags1.bits.caged)
        {
            auto *td = Maps::getTileDesignation(Units::getPosition(u));
            if (!td || td->bits.hidden)
                continue;
            citizens.push_back(u);
        }
    }

    if (citizens.empty())
    {
        DFAI_DEBUG(camera, 1, "no citizens for fallback");
        return;
    }

    auto score = [](df::unit *u) -> int
    {
        if (!u->job.current_job)
            return 0;
        if (u->job.current_job->job_type == job_type::Sleep)
            return 100;
        switch (ENUM_ATTR(job_type, type, u->job.current_job->job_type))
        {
        case job_type_class::Misc:
            return -20;
        case job_type_class::Digging:
            return -50;
        case job_type_class::Building:
            return -35;
        case job_type_class::Hauling:
            return -40;
        case job_type_class::LifeSupport:
            return -10;
        case job_type_class::TidyUp:
            return -20;
        case job_type_class::Leisure:
            return -20;
        case job_type_class::Gathering:
            return -30;
        case job_type_class::Manufacture:
            return -10;
        case job_type_class::Improvement:
            return -10;
        case job_type_class::Crime:
            return -50;
        case job_type_class::LawEnforcement:
            return -30;
        case job_type_class::StrangeMood:
            return -20;
        case job_type_class::UnitHandling:
            return -30;
        case job_type_class::SiegeWeapon:
            return -50;
        case job_type_class::Medicine:
            return -70;
        default:
            break;
        }
        return 0;
    };

    std::shuffle(citizens.begin(), citizens.end(), ai.rng);
    std::sort(citizens.begin(), citizens.end(), [score](df::unit *a, df::unit *b) -> bool { return score(a) < score(b); });

    if (following != -1)
        following_prev.push_back(following);
    if (following_prev.size() > 3)
    {
        following_prev.erase(following_prev.begin(), following_prev.end() - 3);
    }

    df::unit *target = nullptr;
    for (auto u : citizens)
    {
        if (std::find(following_prev.begin(), following_prev.end(), u->id) == following_prev.end())
        {
            target = u;
            break;
        }
    }
    if (!target)
    {
        target = citizens[std::uniform_int_distribution<size_t>(0, citizens.size() - 1)(ai.rng)];
    }

    following = target->id;
    DFAI_DEBUG(camera, 2, "citizen fallback: " << AI::describe_unit(target));

    if (!*pause_state)
    {
        Gui::revealInDwarfmodeMap(Units::getPosition(target), true);
        plotinfo->follow_unit = following;
        dwell_until = *cur_year_tick + 2000;
        dwell_tier = CAMERA_TIER_CITIZEN;
    }

    world->status.flags.bits.combat = 0;
    world->status.flags.bits.hunting = 0;
    world->status.flags.bits.sparring = 0;
}

void AI::ignore_pause(int32_t x, int32_t y, int32_t z)
{
    if (!config.camera)
    {
        Gui::setViewCoords(x, y, z);
        plotinfo->follow_unit = camera.follow_unit;
        plotinfo->follow_item = camera.follow_item;
        return;
    }

    if (camera.following != -1 && camera.dwell_tier == CAMERA_TIER_CITIZEN)
    {
        if (df::unit *u = df::unit::find(camera.following))
        {
            Gui::revealInDwarfmodeMap(Units::getPosition(u), true);
            plotinfo->follow_unit = camera.following;
            return;
        }
    }

    if (camera.last_event_coord.isValid())
    {
        Gui::revealInDwarfmodeMap(camera.last_event_coord, true);
        plotinfo->follow_unit = -1;
    }
    else
    {
        Gui::setViewCoords(x, y, z);
    }
}

std::string Camera::status()
{
    if (!config.camera)
    {
        return "disabled by config";
    }

    std::ostringstream s;

    size_t total_queued = 0;
    for (int t = 0; t < CAMERA_NUM_TIERS; t++)
        total_queued += tiers[t].size();

    if (*cur_year_tick < dwell_until && dwell_tier < CAMERA_TIER_CITIZEN && last_event_coord.isValid())
    {
        s << "event at (" << last_event_coord.x << "," << last_event_coord.y << "," << last_event_coord.z << ")";
    }
    else if (following != -1)
    {
        s << "following " << AI::describe_unit(df::unit::find(following));
    }
    else
    {
        s << "idle";
    }

    if (total_queued > 0)
        s << " [" << total_queued << " queued]";

    std::string fp;
    for (auto it = following_prev.begin(); it != following_prev.end(); it++)
    {
        if (!fp.empty())
            fp += "; ";
        fp += AI::describe_unit(df::unit::find(*it));
    }
    if (!fp.empty())
        s << " (prev: " << fp << ")";

    return s.str();
}
