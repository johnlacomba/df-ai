#include "ai.h"
#include "stocks.h"

#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

int32_t Stocks::count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order)
{
    int32_t cnt = 0;
    for (auto mo : world->manager_orders.all)
    {
        if (mo->material_category.whole == matcat.whole && mo->job_type != order)
        {
            cnt += mo->amount_left;
        }
    }
    return cnt;
}

template<typename T>
static bool template_equals(const T *a, const df::manager_order_template *b)
{
    if (a->job_type != b->job_type)
        return false;
    if (a->reaction_name != b->reaction_name)
        return false;
    if (a->item_type != b->item_type)
        return false;
    if (a->item_subtype != b->item_subtype)
        return false;
    if (a->mat_type != b->mat_type)
        return false;
    if (a->mat_index != b->mat_index)
        return false;
    if (a->material_category.whole != b->material_category.whole)
        return false;
    return true;
}

int32_t Stocks::count_manager_orders(color_ostream &, const df::manager_order_template & tmpl)
{
    int32_t amount = 0;

    for (auto mo : world->manager_orders.all)
    {
        if (template_equals(mo, &tmpl))
        {
            amount += mo->amount_left;
        }
    }

    return amount;
}

void Stocks::add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount)
{
    std::ofstream discard;
    add_manager_order(out, tmpl, amount, discard);
}

void Stocks::add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount, std::ostream & reason)
{
    if (amount <= 0)
    {
        return;
    }

    int32_t already_queued = count_manager_orders(out, tmpl);
    amount -= already_queued;

    if (already_queued && amount < 5)
    {
        amount = 0;
    }

    if (amount <= 0)
    {
        reason << "already have manager order: " << AI::describe_job(&tmpl) << " (" << already_queued << " remaining)";
        return;
    }

    for (auto it = world->manager_orders.all.begin(); it != world->manager_orders.all.end(); it++)
    {
        if (template_equals(*it, &tmpl) && (*it)->amount_left == (*it)->amount_total)
        {
            amount += (*it)->amount_left;
            auto *old_order = *it;
            world->manager_orders.all.erase(it);
            delete old_order;
            break;
        }
    }

    int32_t qty = std::min(amount, 9999);

    auto order = new df::manager_order();
    order->id = world->manager_orders.manager_order_next_id++;
    order->job_type = tmpl.job_type;
    order->reaction_name = tmpl.reaction_name;
    order->item_type = tmpl.item_type;
    order->item_subtype = tmpl.item_subtype;
    order->mat_type = tmpl.mat_type;
    order->mat_index = tmpl.mat_index;
    order->material_category = tmpl.material_category;
    order->amount_left = qty;
    order->amount_total = qty;
    order->status.bits.validated = true;
    world->manager_orders.all.push_back(order);

    reason << "add_manager_order (" << qty << "): " << AI::describe_job(&tmpl);
    ai.debug(out, "add_manager_order(" + stl_sprintf("%d", qty) + ") " + AI::describe_job(&tmpl));
}
