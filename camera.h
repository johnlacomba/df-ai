#pragma once

#include "event_manager.h"

#include <deque>

#include "df/coord.h"

class AI;

const int CAMERA_TIER_COMBAT = 0;
const int CAMERA_TIER_FORT = 1;
const int CAMERA_TIER_CITIZEN = 2;
const int CAMERA_NUM_TIERS = 3;
const int CAMERA_ON_SCREEN_RADIUS = 15;
const int CAMERA_TIER_CAP = 20;
const int CAMERA_DWELL_EVENT = 2;
const int CAMERA_DWELL_CITIZEN = 4;

struct CameraEvent
{
    df::coord pos;
    std::string description;
};

class Camera
{
    AI & ai;
    OnupdateCallback *ontick_handle;
    OnupdateCallback *onupdate_handle;
    OnstatechangeCallback *onstatechange_handle;
    friend class AI;

    std::deque<CameraEvent> tiers[CAMERA_NUM_TIERS];
    int32_t dwell_remaining;
    int32_t dwell_tier;
    int32_t citizen_scan_counter;
    df::coord last_event_coord;

public:
    Camera(AI & ai);
    ~Camera();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    void check_record_status();
    void update_tick(color_ostream & out);
    void update(color_ostream & out);
    std::string status();

    void queue_event(int tier, df::coord pos, const std::string & description);

    int32_t following;
    std::vector<int32_t> following_prev;
    int32_t follow_unit;
    int32_t follow_item;
    bool follow_stop;
    bool movie_started_in_lockstep;
};
