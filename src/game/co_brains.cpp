#include "game/co_brains.h"
#include "game/world.h"
#include "ai/plan.h"
#include "ai/action.h"

namespace bm {

void BrainsComponent::think() {
    if (not wish_.current) {
        pick_and_plan();
    }
    if (wish_.current) {
        follow_the_plan();
    }
}

void BrainsComponent::pick_and_plan() {
    auto wo = ComponentObject::get_world();

    while (not wish_.orders.empty()) {
        ai::Order::Ptr ord = wish_.orders.back();
        auto ctx = ord->ctx_;
        ctx.actor_ = get_parent();

        if (wo->conditions_stand_true(ord->desired_, ctx)) {
            // we do not desire anymore that which came true
            wish_.orders.pop_back();
        } else {
            break;
        }
    }
    if (wish_.orders.empty()) {
        // Idle, no desires. Wait. Loiter. Drink alcohol.
        return;
    }

    ai::Order::Ptr try_order = wish_.orders.front();
    auto &want = try_order->desired_;

    ai::Context ctx = try_order->ctx_; // copy
    ctx.actor_ = get_parent();

    auto actions = ai::propose_plan(
                wo->get_current_situation(want, ctx),
                want,   // want
                ctx);   // context

    // if there is no plan of actions, we don't want it anymore
    if (actions.empty()) {
        // forget this one
        wo->report_impossible(try_order->id_);
        wish_.orders.erase(wish_.orders.begin());
        wish_.current = {};
        return;
    }

//    qDebug() << "brains: have plan!";
    wish_.current = try_order;
    wish_.orders.erase(wish_.orders.begin()); // consumed a plan

    // High level action plan only contains actions. Now try and use context
    // to plan actual activities with destination objects and positions
    ai::flesh_out_a_plan(/*out*/ wish_.plan, actions, ctx);
}

void BrainsComponent::follow_the_plan()
{
    if (not wish_.current) {
        return; // nothing to do
    }

    // if plan is fulfilled
    auto wo            = ComponentObject::get_world();
    ai::Order::Ptr ord = wish_.current;
    ai::Context ctx    = ord->ctx_; // copy
    ctx.actor_         = get_parent();
    if (wo->conditions_stand_true(ord->desired_, ctx)) {
        // we do not desire anymore that which came true
        wo->report_fulfilled(ord->id_);
        wish_.current = {};
        wish_.plan.clear();
        return;
    }

    auto& step = wish_.plan.front();
//    qDebug() << "brains: follow plan" << *get_parent()
//             << " step=" << step.action_;

    switch (step.action_) {
    case ai::ActionType::None: // no action (this should not be in plan either)
        wish_.plan.erase(wish_.plan.begin());
        return;
    case ai::ActionType::Mine: {
            auto dst = step.arg_.get_pos();
            auto ent = get_parent()->as_entity();
            if (adjacent_or_same(ent->get_pos(), dst)) {
                ComponentObject::get_world()->mine_voxel(dst);
//                qDebug() << "brains: Mining: done";
            } else {
//                qDebug() << "brains: Mining: failed";
            }
            // Finish step even if mining failed
            wish_.plan.erase(wish_.plan.begin());
        } break;
    case ai::ActionType::Move: {
            auto dst = step.arg_.get_pos();
            auto ent = get_parent()->as_entity();
            if (not ent->is_moving() && ent->get_move_destination() != dst)
            {
                if (not ent->move_to(dst)) {
                    // no route
                    finish_plan();
                    return;
                }
//                qDebug() << "brains: Move: will move to" << dst;
                break;
            } else {
                // Move finished
                if (adjacent_or_same(ent->get_pos(), dst)) {
//                    qDebug() << "brains: Move: finished";
                    wish_.plan.erase(wish_.plan.begin());
                    break;
                }
                // not moving, but destination is correct - means can't move
                if (not ent->is_moving()) {
                    finish_plan();
                    return;
                }
            }
        } break;
    }

    if (wish_.plan.empty()) {
        finish_plan();
    }
}

void BrainsComponent::finish_plan()
{
    auto wo            = ComponentObject::get_world();
    ai::Order::Ptr ord = wish_.current;
    ai::Context ctx    = ord->ctx_; // copy

    if (wo->conditions_stand_true(ord->desired_, ctx)) {
        wo->report_fulfilled(ord->id_);
    } else {
        wo->report_failed(ord->id_);
    }
    wish_.current = {};
}

void BrainsComponent::want(ai::Order::Ptr desire) {
    wish_.plan.clear();
    wish_.orders.clear();
    wish_.orders.push_back(desire);
}


} // ns bm
