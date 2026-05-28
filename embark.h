#pragma once

#include "event_manager.h"
#include "exclusive_callback.h"

class AI;

class EmbarkExclusive : public ExclusiveCallback
{
    AI & ai;
    std::string last_dump_key;

public:
    EmbarkExclusive(AI & ai);
    virtual ~EmbarkExclusive();

    virtual ExclusiveCallback *ReplaceOnScreenChange() { return nullptr; }
    virtual void Run(color_ostream & out);

private:
    void SelectVerticalMenuItem(int32_t *current, int32_t target);
    void SelectHorizontalMenuItem(int32_t *current, int32_t target);

    void DumpScreenInfo(color_ostream & out);

    void ViewTitle(color_ostream & out);
    void ViewLoadGame(color_ostream & out);
    void ViewLoadScreen(color_ostream & out);
    void ViewLoadScreenOptions();
    void ViewNewRegion(color_ostream & out);
    void ViewUpdateRegion(color_ostream & out);
    void ViewChooseStartSite(color_ostream & out);
    void DisplayEmbarkSite(color_ostream & out);
    void ViewSetupDwarfGame(color_ostream & out);
    void ViewTextViewer(color_ostream & out);
};

class RestartWaitExclusive : public ExclusiveCallback
{
    AI & ai;

public:
    RestartWaitExclusive(AI & ai);
    virtual ~RestartWaitExclusive();

    virtual void Run(color_ostream & out);
};
