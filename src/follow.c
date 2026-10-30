#include "follow.h"

focus_act_t focus_act(bool settling, bool recheck, bool is_stale, bool cur_has_win, bool user_call) {
    if (user_call && !is_stale) return FOCUS_FOLLOW;
    if (settling || is_stale) return FOCUS_IGNORE;
    if (recheck) return cur_has_win ? FOCUS_REFOCUS : FOCUS_STALE;
    return FOCUS_FOLLOW;
}

bool input_is_call(double at, double settle_start, double now) {
    return at > settle_start && at <= now && now - at <= CALL_INPUT_S;
}

bool input_is_quick_call(double key_at, double click_at, double now) {
    return key_at > 0 && key_at > click_at && key_at <= now && now - key_at <= QUICK_CALL_S;
}

follow_act_t follow_check(bool still_focused, bool now_here, bool settling, bool cur_left, bool user_call) {
    if (!still_focused || now_here) return FOLLOW_DROP;
    if (settling && !user_call) return FOLLOW_WAIT;
    return cur_left ? FOLLOW_LEFT : FOLLOW_SWITCH;
}

bool cur_left(bool tracked_here, bool known_minimized, bool alive, ax_read_t read, bool ax_minimized) {
    if (!tracked_here || known_minimized) return false;
    if (!alive) return true;
    return read == READ_INVALID || read == READ_BUSY || (read == READ_OK && ax_minimized);
}
