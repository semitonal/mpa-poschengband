/****************************************************************
 * The Weaponmaster
 ****************************************************************/

#include "angband.h"

int shoot_hack = SHOOT_NONE;
int shoot_count = 0;
int shoot_item = 0;

#define _MAX_TARGETS 100

/****************************************************************
 * Private Helpers
 ****************************************************************/

static bool _check_direct_shot(int tx, int ty)
{
    bool result = FALSE;
    u16b path[512];
    int ct = project_path(path, 50, py, px, ty, tx, PROJECT_PATH); /* We don't know the length ... just project from source to target, please! */
    int x, y, i;

    for (i = 0; i < ct; i++)
    {
        x = GRID_X(path[i]);
        y = GRID_Y(path[i]);

        /* Reached target! Yay! */
        if (x == tx && y == ty)
        {
            result = TRUE;
            break;
        }

        /* Stopped by walls/doors */
        if (!cave_have_flag_bold(y, x, FF_PROJECT) && !cave[y][x].m_idx) break;

        /* Monster in the way of target */
        if (cave[y][x].m_idx) break;
    }

    return result;
}

static bool _check_speciality_equip(void);
static bool _check_speciality_aux(object_type *o_ptr);

static int _find_ammo_slot(void)
{
    int i;

    for (i = 0; i < INVEN_PACK; i++)
    {
        if (inventory[i].tval == p_ptr->shooter_info.tval_ammo) return i;
    }
    if (p_ptr->unlimited_quiver) return INVEN_UNLIMITED_QUIVER;
    return -1;
}

static int _get_nearest_target_los(void)
{
    int result = 0;
    int dis = AAF_LIMIT + 1;
    int i;
    monster_type *m_ptr = NULL;
    int rng = 0;
    
    if (p_ptr->shooter_info.slot)
        rng = bow_range(equip_obj(p_ptr->shooter_info.slot));
    
    for (i = m_max - 1; i >= 1; i--)
    {
        m_ptr = &m_list[i];
        if (!m_ptr->r_idx
        ) continue;
        if (m_ptr->smart & SM_FRIENDLY) continue;
        if (m_ptr->smart & SM_PET) continue;
        if (m_ptr->cdis > rng) continue;
        if (!m_ptr->ml) continue;
        if (!los(py, px, m_ptr->fy, m_ptr->fx)) continue;

        if (m_ptr->cdis < dis)
        {
            result = i;
            dis = m_ptr->cdis;
        }
    }

    return result;
}

static int _get_greater_many_shot_targets(int *targets, int max)
{
    int result = 0;
    int i;
    monster_type *m_ptr = NULL;
    int rng = 0;
    
    if (p_ptr->shooter_info.slot)
        rng = bow_range(equip_obj(p_ptr->shooter_info.slot));

    /* shoot *all* line of sight monsters */    
    for (i = m_max - 1; i >= 1; i--)
    {
        m_ptr = &m_list[i];
        if (!m_ptr->r_idx) continue;
        if (m_ptr->smart & SM_FRIENDLY) continue;
        if (m_ptr->smart & SM_PET) continue;
        if (m_ptr->cdis > rng) continue;
        if (!m_ptr->ml) continue;
        if (!los(py, px, m_ptr->fy, m_ptr->fx)) continue;
        if (result >= max) break;

        targets[result] = i;
        result++;
    }

    return result;
}

static int _get_many_shot_targets(int *targets, int max)
{
    int result = 0;
    int i;
    monster_type *m_ptr = NULL;
    int in_sight[_MAX_TARGETS];
    int ct = 0;
    int rng = 0;
    
    if (p_ptr->shooter_info.slot)
        rng = bow_range(equip_obj(p_ptr->shooter_info.slot));

    /* pass 1: get line of sight monsters */    
    for (i = m_max - 1; i >= 1; i--)
    {
        m_ptr = &m_list[i];
        if (!m_ptr->r_idx) continue;
        if (m_ptr->smart & SM_FRIENDLY) continue;
        if (m_ptr->smart & SM_PET) continue;
        if (m_ptr->cdis > rng) continue;
        if (!m_ptr->ml) continue;
        if (!los(py, px, m_ptr->fy, m_ptr->fx)) continue;
        if (ct >= _MAX_TARGETS) break;

        in_sight[ct] = i;
        ct++;
    }

    /* pass 2: for each monster in los, build a path from the player to the
       monster and make sure there are no other intervening monsters */
    for (i = 0; i < ct; i++)
    {
        m_ptr = &m_list[in_sight[i]];
        if (_check_direct_shot(m_ptr->fx, m_ptr->fy))
        {
            if (result > max) break;
            targets[result] = in_sight[i];
            result++;
        }
    }

    return result;
}

static bool _fire(int power)
{
    bool result = FALSE;
    shoot_hack = power;
    shoot_count = 0;
    command_cmd = 'f'; /* Hack for inscriptions (e.g. '@f1') */
    result = do_cmd_fire();
    shoot_hack = SHOOT_NONE;
    shoot_count = 0;

    return result;
}

/* Weaponmasters have toggle based abilities */
static int _get_toggle(void)
{
    return p_ptr->magic_num1[0];
}

static int _set_toggle(s32b toggle)
{
    int result = p_ptr->magic_num1[0];

    if (toggle == result) return result;
    p_ptr->magic_num1[0] = toggle;

    p_ptr->redraw |= PR_STATUS;
    p_ptr->update |= PU_BONUS;
    handle_stuff();

    return result;
}

static bool _do_blow(int type)
{
    if (!_check_speciality_equip())
    {
        msg_print("Failed! You do not feel comfortable with your weapon.");
        return FALSE;
    }
    return do_blow(type);
}

/****************************************************************
 * Private Spells
 ****************************************************************/
static void _fire_spell(int which, int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your shooter.");
            return;
        }
        if (_fire(which))
            var_set_bool(res, TRUE);
        break;
    case SPELL_ENERGY:
        var_set_int(res, energy_use);    /* already set correctly by do_cmd_fire() */
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _toggle_spell(int which, int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        if (_get_toggle() == which)
            _set_toggle(TOGGLE_NONE);
        else
            _set_toggle(which);
        var_set_bool(res, TRUE);
        break;
    case SPELL_ENERGY:
        if (_get_toggle() != which)
            var_set_int(res, 0);    /* no charge for dismissing a technique */
        else
            var_set_int(res, 100);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _judge_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Judge");
        break;
    case SPELL_DESC:
        var_set_string(res, "Identifies a favored item.");
        break;
    case SPELL_CAST:
        if (p_ptr->lev >= 45)
            var_set_bool(res, identify_fully(_check_speciality_aux));
        else
            var_set_bool(res, ident_spell(_check_speciality_aux));
        break;
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

/****************************************************************
 * Axemaster
 ****************************************************************/
static void _crusaders_strike_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Crusaders Strike");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attack an adjacent opponent with a single blow. You regain hp.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_CRUSADERS_STRIKE));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _power_attack_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Power Attack");
        break;
    case SPELL_DESC:
        var_set_string(res, "You lose accuracy but gain damage.");
        break;
    default:
        _toggle_spell(TOGGLE_POWER_ATTACK, cmd, res);
        break;
    }
}

static void _vicious_strike_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Vicious Strike");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attacks an opponent very powerfully, but you become greatly exposed by the effort.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (_do_blow(WEAPONMASTER_VICIOUS_STRIKE))
        {
            set_tim_vicious_strike(p_ptr->tim_vicious_strike + 10, FALSE);
            var_set_bool(res, TRUE);
        }
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

/****************************************************************
 * Bowmaster
 ****************************************************************/
static void _arrow_of_slaying_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Arrow of Slaying");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attempt to kill a monster a well aimed shot.");
        break;
    default:
        _fire_spell(SHOOT_NEEDLE, cmd, res);
    }
}

static void _disintegration_arrow_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Disintegration Arrow");
        break;
    case SPELL_DESC:
        var_set_string(res, "Shoot a single arrow at a chosen opponent. Not even intervening walls can stop this shot!");
        break;
    default:
        _fire_spell(SHOOT_DISINTEGRATE, cmd, res);
    }
}

static void _piercing_arrow_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Piercing Arrow");
        break;
    case SPELL_DESC:
        var_set_string(res, "If an arrow hits opponent, it pierces and can also hit next opponent in same direction (requires another attack roll), up to 5 opponents. Each successive pierce suffers a cumulative penalty to hit.");
        break;
    default:
        _toggle_spell(TOGGLE_PIERCING_ARROW, cmd, res);
        break;
    }
}

static void _readied_shot_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Readied Shot");
        break;
    case SPELL_DESC:
        var_set_string(res, "Fire an arrow (choose which at time of casting) in response at enemy who damages you. Active until triggered.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your shooter.");
            return;
        }
        if (_get_toggle() == TOGGLE_READIED_SHOT)
            _set_toggle(TOGGLE_NONE);
        else
        {
            /* Prompt for ammo to use, but disallow choosing from the floor since
               the player will be moving */
            item_tester_tval = p_ptr->shooter_info.tval_ammo;
            if (!get_item(&shoot_item, 
                          "Choose ammo for this technique.", 
                          "You have nothing to fire.", (USE_INVEN | USE_QUIVER)))
            {
                flush();
                return;
            }
            _set_toggle(TOGGLE_READIED_SHOT);
        }
        var_set_bool(res, TRUE);
        break;
    case SPELL_ENERGY:
        if (_get_toggle() != TOGGLE_READIED_SHOT)
            var_set_int(res, 0);    /* no charge for dismissing a technique */
        else
            var_set_int(res, 100);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _tranquilizing_arrow_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Tranquilizing Arrow");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attempt to put a monster to sleep with a well aimed shot.");
        break;
    default:
        _fire_spell(SHOOT_TRANQUILIZE, cmd, res);
    }
}

static void _volley_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Volley");
        break;
    case SPELL_DESC:
        var_set_string(res, "Launch a single arrow at chosen foe. This arrow will be shot over the heads of any intervening monsters.");
        break;
    default:
        _fire_spell(SHOOT_VOLLEY, cmd, res);
    }
}

/****************************************************************
 * Clubmaster
 ****************************************************************/
static void _combat_expertise_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Defensive Stance");
        break;
    case SPELL_DESC:
        var_set_string(res, "You lose accuracy but gain armor class.");
        break;
    default:
        _toggle_spell(TOGGLE_COMBAT_EXPERTISE, cmd, res);
        break;
    }
}

static void _cunning_strike_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Cunning Strike");
        break;
    case SPELL_DESC:
        var_set_string(res, "Number of attacks are cut in half, but blows are more likely to confuse, stun, knock out your opponent.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_CUNNING_STRIKE));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _smite_evil_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Smite Evil");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attack powerfully at an evil monster.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_SMITE_EVIL));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static int _smash_ground_dam(void)
{
    int result = 0;
    int hand;
    /* Modified from Samurai "Crack" */
    for (hand = 0; hand < MAX_HANDS; hand++)
    {
        if (p_ptr->weapon_info[hand].wield_how != WIELD_NONE)
        {
            object_type *o_ptr = equip_obj(p_ptr->weapon_info[hand].slot);
            int          damage, dd, ds;

            if (!o_ptr) continue;

            dd = o_ptr->dd + p_ptr->weapon_info[hand].to_dd;
            ds = o_ptr->ds + p_ptr->weapon_info[hand].to_ds;
    
            damage = dd * (ds + 1) * 50;
            damage += o_ptr->to_d * 100;
            damage *= NUM_BLOWS(hand)/100;
            result += damage / 100;
        }
    }
    return result;
}
static void _smash_ground_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Smash Ground");
        break;
    case SPELL_DESC:
        var_set_string(res, "Produces a loud, stunning noise.");
        break;
    case SPELL_INFO:
        var_set_string(res, info_damage(0, 0, _smash_ground_dam()));
        break;
    case SPELL_CAST:
    {
        int dam = _smash_ground_dam();
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        msg_print("You smash your weapon mightily on the ground.");
        project(0, 8, py, px, device_power(dam*2), GF_SOUND, PROJECT_KILL | PROJECT_ITEM, -1);

        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

/***** THROW WEAPON SPELL **********/
typedef struct {
    int item;
    object_type *o_ptr;
    int mult;
    int tdis;
    int tx;
    int ty;
    bool come_back;
    bool fail_catch;
} _club_toss_info;
static void _club_toss_imp(_club_toss_info * info);

static bool _club_toss(int hand)
{
    int dir;
    _club_toss_info info;
    int back_chance;
    
    /* Setup info for the toss */
    info.item = p_ptr->weapon_info[hand].slot;
    info.o_ptr = equip_obj(p_ptr->weapon_info[hand].slot);

    /* Toss mechanics stolen from Samurai Boomerang ... see do_cmd_throw_aux() */
    back_chance = randint1(30)+20+((int)(adj_dex_th[p_ptr->stat_ind[A_DEX]]) - 128);
    back_chance += 4+randint1(5);

    info.come_back = FALSE;
    info.fail_catch = FALSE;
    if (back_chance > 30 && !one_in_(100))
    {
        info.come_back = TRUE;
        if (back_chance <= 37 && !p_ptr->blind)
            info.fail_catch = TRUE;
    }
    
    if (!_check_speciality_aux(info.o_ptr)) return FALSE;
    if (object_is_cursed(info.o_ptr))
    {
        msg_print("Hmmm, it seems to be cursed.");
        return FALSE;
    }

    /* Pick a target */
    info.mult = 1 + NUM_BLOWS(hand)/100;
    if (p_ptr->mighty_throw) info.mult++;
    {
        int mul, div;
        mul = 10 + 2 * (info.mult - 1);
        div = (info.o_ptr->weight > 10) ? info.o_ptr->weight : 10;
        div /= 2;
        info.tdis = (adj_str_blow[p_ptr->stat_ind[A_STR]] + 20) * mul / div;
        if (info.tdis > mul) info.tdis = mul;

        project_length = info.tdis + 1;
        if (!get_aim_dir(&dir)) return FALSE;

        info.tx = px + 99 * ddx[dir];
        info.ty = py + 99 * ddy[dir];

        if ((dir == 5) && target_okay())
        {
            info.tx = target_col;
            info.ty = target_row;
        }

        project_length = 0;

        if (info.tx == px && info.ty == py) return FALSE;
    }

    /* Toss */
    _club_toss_imp(&info);

    /* Handle Inventory */
    if (!info.come_back || info.fail_catch)
    {
        object_type copy;

        if (info.fail_catch)
            msg_print("But you can't catch!");

        object_copy(&copy, info.o_ptr);
        copy.number = 1;

        inven_item_increase(info.item, -1);
        inven_item_describe(info.item);
        inven_item_optimize(info.item);
        

        if (!info.come_back)
            drop_near(&copy, 0, info.ty, info.tx);
        else
            drop_near(&copy, 0, py, px);
        
        p_ptr->redraw |= PR_EQUIPPY;
        p_ptr->update |= PU_BONUS;

        calc_android_exp();
        handle_stuff();
    }

    return TRUE;
}

static void _club_toss_imp(_club_toss_info * info)
{
    char o_name[MAX_NLEN];
    u16b path[512];
    int msec = delay_factor * delay_factor * delay_factor;
    int y, x, ny, nx, dd, tdam;
    int cur_dis, ct;
    int chance;

    chance = p_ptr->skill_tht + ((p_ptr->shooter_info.to_h + info->o_ptr->to_h) * BTH_PLUS_ADJ);
    chance *= 2; /* mimicking code for ninja shuriken */

    object_desc(o_name, info->o_ptr, OD_OMIT_PREFIX);
    ct = project_path(path, info->tdis, py, px, info->ty, info->tx, PROJECT_PATH);

    y = py;
    x = px;

    for (cur_dis = 0; cur_dis < ct; )
    {
        /* Peek ahead at the next square in the path */
        ny = GRID_Y(path[cur_dis]);
        nx = GRID_X(path[cur_dis]);

        /* Stopped by walls/doors/forest ... but allow hitting your target, please! */
        if (!cave_have_flag_bold(ny, nx, FF_PROJECT)
         && !cave[ny][nx].m_idx) 
        {
            break;
        }

        /* The player can see the (on screen) missile */
        if (panel_contains(ny, nx) && player_can_see_bold(ny, nx))
        {
            char c = object_char(info->o_ptr);
            byte a = object_attr(info->o_ptr);

            /* Draw, Hilite, Fresh, Pause, Erase */
            print_rel(c, a, ny, nx);
            move_cursor_relative(ny, nx);
            Term_fresh();
            Term_xtra(TERM_XTRA_DELAY, msec);
            lite_spot(ny, nx);
            Term_fresh();
        }

        /* The player cannot see the missile */
        else
        {
            /* Pause anyway, for consistancy */
            Term_xtra(TERM_XTRA_DELAY, msec);
        }

        /* Save the new location */
        x = nx;
        y = ny;

        /* Advance the distance */
        cur_dis++;

        /* Monster here, Try to hit it */
        if (cave[y][x].m_idx)
        {
            cave_type *c_ptr = &cave[y][x];
            monster_type *m_ptr = &m_list[c_ptr->m_idx];
            monster_race *r_ptr = &r_info[m_ptr->r_idx];
            bool visible = m_ptr->ml;

            if (test_hit_fire(chance - cur_dis, MON_AC(r_ptr, m_ptr), m_ptr->ml))
            {
                bool fear = FALSE;

                if (!visible)
                    msg_format("The %s finds a mark.", o_name);
                else
                {
                    char m_name[80];
                    monster_desc(m_name, m_ptr, 0);
                    msg_format("The %s hits %s.", o_name, m_name);
                    if (m_ptr->ml)
                    {
                        if (!p_ptr->image) monster_race_track(m_ptr->ap_r_idx);
                        health_track(c_ptr->m_idx);
                    }
                }

                /***** The Damage Calculation!!! *****/
                dd = info->o_ptr->dd;
                tdam = damroll(dd, info->o_ptr->ds);                
                tdam = tot_dam_aux(info->o_ptr, tdam, m_ptr, 0, 0, TRUE);
                tdam = critical_throw(info->o_ptr->weight, info->o_ptr->to_h, tdam);
                tdam += info->o_ptr->to_d;
                tdam *= info->mult;
                tdam += p_ptr->to_d_m;
                if (tdam < 0) tdam = 0;
                tdam = mon_damage_mod(m_ptr, tdam, FALSE);

                if (mon_take_hit(c_ptr->m_idx, tdam, &fear, extract_note_dies(real_r_ptr(m_ptr))))
                {
                    /* Dead monster */
                }
                else
                {
                    message_pain(c_ptr->m_idx, tdam);
                    if (tdam > 0)
                        anger_monster(m_ptr);

                    if (tdam > 0 && m_ptr->cdis > 1 && allow_ticked_off(r_ptr))
                    {
                        if (mut_present(MUT_PEERLESS_SNIPER))
                        {
                        }
                        else
                        {
                            m_ptr->anger_ct++;
                        }
                    }

                    if (fear && m_ptr->ml)
                    {
                        char m_name[80];
                        sound(SOUND_FLEE);
                        monster_desc(m_name, m_ptr, 0);
                        msg_format("%^s flees in terror!", m_name);
                    }

                    {
                        char m_name[80];
                        int odds = 5;
                        monster_desc(m_name, m_ptr, 0);
                
                        if (one_in_(odds))
                        {
                            if (r_ptr->flags3 & RF3_NO_CONF)
                            {
                                if (is_original_ap_and_seen(m_ptr)) r_ptr->r_flags3 |= RF3_NO_CONF;
                                msg_format("%^s is unaffected.", m_name);
                            }
                            else if (mon_save_p(m_ptr->r_idx, A_STR))
                            {
                                msg_format("%^s is unaffected.", m_name);
                            }
                            else
                            {
                                msg_format("%^s appears confused.", m_name);
                                set_monster_confused(c_ptr->m_idx, MON_CONFUSED(m_ptr) + 10 + randint0(p_ptr->lev) / 5);
                            }
                        }

                        if (p_ptr->lev >= 20 && one_in_(odds))
                        {
                            if (r_ptr->flags3 & RF3_NO_SLEEP)
                            {
                                if (is_original_ap_and_seen(m_ptr)) r_ptr->r_flags3 |= RF3_NO_SLEEP;
                                msg_format("%^s is unaffected.", m_name);
                            }
                            else if (mon_save_p(m_ptr->r_idx, A_STR))
                            {
                                msg_format("%^s is unaffected.", m_name);
                            }
                            else
                            {
                                msg_format("%^s is knocked out.", m_name);
                                set_monster_csleep(c_ptr->m_idx, MON_CSLEEP(m_ptr) + 500);
                            }
                        }

                        if (p_ptr->lev >= 45 && one_in_(odds))
                        {
                            if (r_ptr->flags3 & RF3_NO_STUN)
                            {
                                if (is_original_ap_and_seen(m_ptr)) r_ptr->r_flags3 |= RF3_NO_STUN;
                                msg_format("%^s is unaffected.", m_name);
                            }
                            else if (mon_save_p(m_ptr->r_idx, A_STR))
                            {
                                msg_format("%^s is unaffected.", m_name);
                            }
                            else
                            {
                                msg_format("%^s is stunned.", m_name);
                                set_monster_stunned(c_ptr->m_idx, MAX(MON_STUNNED(m_ptr), 2));
                            }
                        }
                    }
                }
            }

            /* Stop looking */
            break;
        }
    }

    if (info->come_back)
    {
        int i;
        for (i = cur_dis; i >= 0; i--)
        {
            y = GRID_Y(path[i]);
            x = GRID_X(path[i]);
            if (panel_contains(y, x) && player_can_see_bold(y, x))
            {
                char c = object_char(info->o_ptr);
                byte a = object_attr(info->o_ptr);

                /* Draw, Hilite, Fresh, Pause, Erase */
                print_rel(c, a, y, x);
                move_cursor_relative(y, x);
                Term_fresh();
                Term_xtra(TERM_XTRA_DELAY, msec);
                lite_spot(y, x);
                Term_fresh();
            }
            else
            {
                /* Pause anyway, for consistancy */
                Term_xtra(TERM_XTRA_DELAY, msec);
            }
        }
        msg_format("Your %s comes back to you.", o_name);
    }
    else
    {
        /* Record the actual location of the toss so we can drop the object here if required */
        info->tx = x;
        info->ty = y;
    }
}

static void _throw_weapon_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Throw Weapon");
        break;
    case SPELL_DESC:
        var_set_string(res, "Throws your leading weapon, which might return to you.");
        break;
    case SPELL_CAST:
    {
        int hand;
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        for (hand = 0; hand < MAX_HANDS; hand++)
        {
            if (p_ptr->weapon_info[hand].wield_how != WIELD_NONE)
            {
                if (_club_toss(hand))
                    var_set_bool(res, TRUE);
                else
                    break;
            }
        }
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _trade_blows_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Trade Blows");
        break;
    case SPELL_DESC:
        var_set_string(res, "When using this technique, you are somewhat exposed. However, you will retaliate whenever a monster hits you.");
        break;
    default:
        _toggle_spell(TOGGLE_TRADE_BLOWS, cmd, res);
        break;
    }
}

/****************************************************************
 * Crossbowmaster
 ****************************************************************/
static void _elemental_bolt_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Elemental Bolt");
        break;
    case SPELL_DESC:
        var_set_string(res, "Shoot a single bolt which explodes in a shower of the elements if it hits.");
        break;
    default:
        _fire_spell(SHOOT_ELEMENTAL, cmd, res);
    }
}

static void _exploding_bolt_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Exploding Bolt");
        break;
    case SPELL_DESC:
        var_set_string(res, "Your bolts will explode on impact, but your firing rate is decreased.");
        break;
    default:
        _toggle_spell(TOGGLE_EXPLODING_BOLT, cmd, res);
        break;
    }
}

static void _knockback_bolt_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Knockback");
        break;
    case SPELL_DESC:
        var_set_string(res, "Shoot a single bolt which propels your enemy backwards if it hits.");
        break;
    default:
        _fire_spell(SHOOT_KNOCKBACK, cmd, res);
    }
}

static void _overdraw_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Overdraw");
        break;
    case SPELL_DESC:
        var_set_string(res, "You shoot with extra might, but your accuracy is diminished.");
        break;
    default:
        _toggle_spell(TOGGLE_OVERDRAW, cmd, res);
        break;
    }
}

static void _rapid_reload_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Rapid Reload");
        break;
    case SPELL_DESC:
        var_set_string(res, "Your shots per round are increased, but you lose armor class and accuracy in your haste.");
        break;
    default:
        _toggle_spell(TOGGLE_RAPID_RELOAD, cmd, res);
        break;
    }
}

static void _shattering_bolt_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Shattering Bolt");
        break;
    case SPELL_DESC:
        var_set_string(res, "Shoot a single bolt which fragments on impact for extra damage.");
        break;
    default:
        _fire_spell(SHOOT_SHATTER, cmd, res);
    }
}

/****************************************************************
 * Daggermaster
 ****************************************************************/
/* DAGGER TOSS: Code spiked from do_cmd_throw with heavy mods */
typedef struct {
    int item;
    object_type *o_ptr;
    int mult;
    int tdis;
    int tx;
    int ty;
    bool come_back;
    bool fail_catch;
    bool flying_dagger;
} _dagger_toss_info;

static void _dagger_toss_imp(_dagger_toss_info * info_ptr);

static bool _dagger_toss(int hand)
{
    int dir;
    _dagger_toss_info info;
    int back_chance;
    int oops = 100;
    
    /* Setup info for the toss */
    info.item = p_ptr->weapon_info[hand].slot;
    info.o_ptr = equip_obj(p_ptr->weapon_info[hand].slot);
    info.come_back = FALSE;
    info.flying_dagger = FALSE;
    if (_get_toggle() == TOGGLE_FLYING_DAGGER_STANCE)
        info.flying_dagger = TRUE;

    if (!_check_speciality_aux(info.o_ptr)) return FALSE;
    if (object_is_cursed(info.o_ptr))
    {
        msg_print("Hmmm, it seems to be cursed.");
        return FALSE;
    }

    /* Toss mechanics stolen (with mods) from Samurai Boomerang ... see do_cmd_throw_aux() */
    back_chance = randint1(30)+20+((int)(adj_dex_th[p_ptr->stat_ind[A_DEX]]) - 128);

    if (info.flying_dagger)
    {
        back_chance += 4+randint1(5);
        oops = oops * 3/2;
    }

    info.come_back = FALSE;
    info.fail_catch = FALSE;
    if (back_chance > 30 && !one_in_(oops))
    {
        info.come_back = TRUE;
        if (p_ptr->blind || p_ptr->image || p_ptr->confused || one_in_(oops))
            info.fail_catch = TRUE;
        else
        {
            oops = 37;
            if (p_ptr->stun)
                oops += 10;
            if (back_chance <= oops)
                info.fail_catch = TRUE;
        }
    }

    /* Pick a target */
    info.mult = NUM_BLOWS(hand)/100;
    if (p_ptr->mighty_throw) info.mult++;
    {
        int mul, div;
        mul = 10 + 2 * (info.mult - 1);
        div = (info.o_ptr->weight > 10) ? info.o_ptr->weight : 10;
        div /= 2;
        info.tdis = (adj_str_blow[p_ptr->stat_ind[A_STR]] + 20) * mul / div;
        if (info.tdis > mul) info.tdis = mul;

        project_length = info.tdis + 1;
        if (!get_aim_dir(&dir)) return FALSE;

        info.tx = px + 99 * ddx[dir];
        info.ty = py + 99 * ddy[dir];

        if ((dir == 5) && target_okay())
        {
            info.tx = target_col;
            info.ty = target_row;
        }

        project_length = 0;

        if (info.tx == px && info.ty == py) return FALSE;
    }

    /* Toss */
    _dagger_toss_imp(&info);

    /* Handle Inventory */
    if (!info.come_back || info.fail_catch)
    {
        object_type copy;

        if (!info.come_back)
        {
            char o_name[MAX_NLEN];
            object_desc(o_name, info.o_ptr, OD_NAME_ONLY);
            msg_format("Your %s fails to return!", o_name);
        }

        if (info.fail_catch)
            msg_print("But you can't catch!");

        if (TRUE) /* This is a showstopper, so force the player to notice! */
        {
            msg_print("Press <color:y>Space</color> to continue.");
            flush();
            for (;;)
            {
                char ch = inkey();
                if (ch == ' ') break;
            }
            msg_line_clear();
        }

        object_copy(&copy, info.o_ptr);
        copy.number = 1;

        inven_item_increase(info.item, -1);
        inven_item_describe(info.item);
        inven_item_optimize(info.item);
        

        if (!info.come_back)
            drop_near(&copy, 0, info.ty, info.tx);
        else
            drop_near(&copy, 0, py, px);
        
        p_ptr->redraw |= PR_EQUIPPY;
        p_ptr->update |= PU_BONUS;
        calc_android_exp();
        handle_stuff();
    }
    return TRUE;
}

static void _dagger_toss_imp(_dagger_toss_info * info)
{
    char o_name[MAX_NLEN];
    u16b path[512];
    int msec = delay_factor * delay_factor * delay_factor;
    int y, x, ny, nx, dd, tdam;
    int cur_dis, ct;
    int chance;

    chance = p_ptr->skill_tht + ((p_ptr->shooter_info.to_h + info->o_ptr->to_h) * BTH_PLUS_ADJ);
    chance *= 2; /* mimicking code for ninja shuriken */

    object_desc(o_name, info->o_ptr, OD_NAME_ONLY);
    ct = project_path(path, info->tdis, py, px, info->ty, info->tx, PROJECT_PATH);

    y = py;
    x = px;

    for (cur_dis = 0; cur_dis < ct; )
    {
        /* Peek ahead at the next square in the path */
        ny = GRID_Y(path[cur_dis]);
        nx = GRID_X(path[cur_dis]);

        /* Stopped by walls/doors/forest ... but allow hitting your target, please! */
        if (!cave_have_flag_bold(ny, nx, FF_PROJECT)
         && !cave[ny][nx].m_idx) 
        {
            break;
        }

        /* The player can see the (on screen) missile */
        if (panel_contains(ny, nx) && player_can_see_bold(ny, nx))
        {
            char c = object_char(info->o_ptr);
            byte a = object_attr(info->o_ptr);

            /* Draw, Hilite, Fresh, Pause, Erase */
            print_rel(c, a, ny, nx);
            move_cursor_relative(ny, nx);
            Term_fresh();
            Term_xtra(TERM_XTRA_DELAY, msec);
            lite_spot(ny, nx);
            Term_fresh();
        }

        /* The player cannot see the missile */
        else
        {
            /* Pause anyway, for consistancy */
            Term_xtra(TERM_XTRA_DELAY, msec);
        }

        /* Save the new location */
        x = nx;
        y = ny;

        /* Advance the distance */
        cur_dis++;

        /* Monster here, Try to hit it */
        if (cave[y][x].m_idx)
        {
            cave_type *c_ptr = &cave[y][x];
            monster_type *m_ptr = &m_list[c_ptr->m_idx];
            monster_race *r_ptr = &r_info[m_ptr->r_idx];
            bool visible = m_ptr->ml;

            if (test_hit_fire(chance - cur_dis, MON_AC(r_ptr, m_ptr), m_ptr->ml))
            {
                bool fear = FALSE;

                if (!visible)
                    msg_format("The %s finds a mark.", o_name);
                else
                {
                    char m_name[80];
                    monster_desc(m_name, m_ptr, 0);
                    msg_format("The %s hits %s.", o_name, m_name);
                    if (m_ptr->ml)
                    {
                        if (!p_ptr->image) monster_race_track(m_ptr->ap_r_idx);
                        health_track(c_ptr->m_idx);
                    }
                }

                /***** The Damage Calculation!!! *****/
                dd = info->o_ptr->dd;
                if (info->flying_dagger)
                    dd += p_ptr->lev/15;
                tdam = damroll(dd, info->o_ptr->ds);                
                tdam = tot_dam_aux(info->o_ptr, tdam, m_ptr, 0, 0, TRUE);
                tdam = critical_throw(info->o_ptr->weight, info->o_ptr->to_h, tdam);
                tdam += info->o_ptr->to_d;
                tdam *= info->mult;
                tdam += p_ptr->shooter_info.to_d;
                if (tdam < 0) tdam = 0;
                tdam = mon_damage_mod(m_ptr, tdam, FALSE);

                if (mon_take_hit(c_ptr->m_idx, tdam, &fear, extract_note_dies(real_r_ptr(m_ptr))))
                {
                    /* Dead monster */
                }
                else
                {
                    message_pain(c_ptr->m_idx, tdam);
                    if (tdam > 0)
                        anger_monster(m_ptr);

                    if (tdam > 0 && m_ptr->cdis > 1 && allow_ticked_off(r_ptr))
                    {
                        if (mut_present(MUT_PEERLESS_SNIPER))
                        {
                        }
                        else
                        {
                            m_ptr->anger_ct++;
                        }
                    }

                    if (fear && m_ptr->ml)
                    {
                        char m_name[80];
                        sound(SOUND_FLEE);
                        monster_desc(m_name, m_ptr, 0);
                        msg_format("%^s flees in terror!", m_name);
                    }
                }
            }

            /* Stop looking */
            break;
        }
    }

    if (info->come_back)
    {
        int i;
        for (i = cur_dis; i >= 0; i--)
        {
            y = GRID_Y(path[i]);
            x = GRID_X(path[i]);
            if (panel_contains(y, x) && player_can_see_bold(y, x))
            {
                char c = object_char(info->o_ptr);
                byte a = object_attr(info->o_ptr);

                /* Draw, Hilite, Fresh, Pause, Erase */
                print_rel(c, a, y, x);
                move_cursor_relative(y, x);
                Term_fresh();
                Term_xtra(TERM_XTRA_DELAY, msec);
                lite_spot(y, x);
                Term_fresh();
            }
            else
            {
                /* Pause anyway, for consistancy */
                Term_xtra(TERM_XTRA_DELAY, msec);
            }
        }
        msg_format("Your %s comes back to you.", o_name);
    }
    else
    {
        /* Record the actual location of the toss so we can drop the object here if required */
        info->tx = x;
        info->ty = y;
    }
}

static void _dagger_toss_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Dagger Toss");
        break;
    case SPELL_DESC:
        var_set_string(res, "Throws your equipped weapons at target monster.");
        break;
    case SPELL_CAST:
    {
        int hand;
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        for (hand = 0; hand < MAX_HANDS; hand++)
        {
            if (p_ptr->weapon_info[hand].wield_how != WIELD_NONE)
            {
                if (_dagger_toss(hand))
                    var_set_bool(res, TRUE);
                else
                    break;
            }
        }
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _flying_dagger_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Flying Dagger Stance");
        break;
    case SPELL_DESC:
        var_set_string(res, "When using this technique, you gain great prowess with the Dagger Toss. Thrown weapons return more often and damage is greatly increased. However, this stance leaves you somewhat exposed to your enemies.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        if (_get_toggle() == TOGGLE_FLYING_DAGGER_STANCE)
            _set_toggle(TOGGLE_NONE);
        else
            _set_toggle(TOGGLE_FLYING_DAGGER_STANCE);
        var_set_bool(res, TRUE);
        break;
    case SPELL_ENERGY:
        if (_get_toggle() != TOGGLE_FLYING_DAGGER_STANCE)
            var_set_int(res, 0);    /* no charge for dismissing a technique */
        else
            var_set_int(res, 100);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _elusive_strike_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Elusive Strike");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attack an adjacent monster and blink to a new location in the line of sight of your current location.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_ELUSIVE_STRIKE));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _frenzy_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Frenzy Stance");
        break;
    case SPELL_DESC:
        var_set_string(res, "In this posture, you attack foes with great power in melee. However, your rage exposes you to your enemies!");
        break;
    default:
        _toggle_spell(TOGGLE_FRENZY_STANCE, cmd, res);
        break;
    }
}

static void _shadow_stance_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Shadow Stance");
        break;
    case SPELL_DESC:
        var_set_string(res, "When using this technique, you walk quickly and stealthily. On attacking a foe, you will swap positions.");
        break;
    default:
        _toggle_spell(TOGGLE_SHADOW_STANCE, cmd, res);
        break;
    }
}

/****************************************************************
 * Diggermaster
 ****************************************************************/
static void _tunnel_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Tunnel");
        break;
    case SPELL_DESC:
        var_set_string(res, "Creates a tunnel down to the next level.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        if (!cave_valid_bold(py, px))
        {
            msg_print("You need room to dig!");
        }
        else
        {
            msg_print("You tunnel downwards ...");
            stair_creation(TRUE);
            var_set_bool(res, TRUE);
        }
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _calamity_of_the_living_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Calamity of the Living");
        break;
    case SPELL_DESC:
        var_set_string(res, "Causes an earthquake or destruction.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        if (p_ptr->lev >= 50 || one_in_(3))
        {
            destroy_area(py, px, 12 + randint1(4), 4 * p_ptr->lev);
        }
        else
        {
            msg_print("The ground rumbles!");
            earthquake(py, px, 10);
        }
        var_set_bool(res, TRUE);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static bool _object_is_corpse_or_skeleton(object_type *o_ptr)
{
    if (o_ptr->tval == TV_CORPSE) return TRUE;
    if (o_ptr->tval == TV_SKELETON) return TRUE;
    return FALSE;
}

static void _bury_dead_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Bury Dead");
        break;
    case SPELL_DESC:
        var_set_string(res, "You gain temporary enchantments by burying a corpse or skeleton.");
        break;
    case SPELL_CAST:
    {
        int item;
        char o_name[MAX_NLEN];
        object_type *o_ptr;
        object_type copy;
        int turns;

        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }

        item_tester_hook = _object_is_corpse_or_skeleton;
        if (!get_item(&item, "Bury which corpse? ", "You have nothing to bury.", (USE_INVEN | USE_FLOOR))) return;

        if (item >= 0)
            o_ptr = &inventory[item];
        else
            o_ptr = &o_list[0 - item];

        /* TV_CORPSE, SV_CORPSE = Corpse
           TV_CORPSE, SV_SKELETON = Skeleton
           TV_SKELETON, ??? = Skeleton */
        if (o_ptr->tval == TV_CORPSE && o_ptr->sval == SV_CORPSE)
            turns = 40;
        else
            turns = 15;

        object_copy(&copy, o_ptr);
        copy.number = 1;
        object_desc(o_name, &copy, OD_NAME_ONLY);
        msg_format("You dig a hasty grave and toss in %s.", o_name);

        if (item >= 0)
        {
            inven_item_increase(item, -1);
            inven_item_describe(item);
            inven_item_optimize(item);
        }
        else
        {
            floor_item_increase(0 - item, -1);
            floor_item_describe(0 - item);
            floor_item_optimize(0 - item);
        }

        set_blessed(p_ptr->blessed + turns, FALSE);
        if (p_ptr->lev >= 15)
            set_hero(p_ptr->hero + turns, FALSE);
        if (p_ptr->lev >= 30)
            set_fast(p_ptr->fast + turns, FALSE);
        if (p_ptr->lev >= 40)
            set_resist_magic(p_ptr->resist_magic + turns, FALSE);
        
        if (p_ptr->lev >= 15)
            set_oppose_cold(p_ptr->oppose_cold + turns, FALSE);
        if (p_ptr->lev >= 25)
            set_oppose_pois(p_ptr->oppose_pois + turns, FALSE);
        if (p_ptr->lev >= 30)
            set_tim_hold_life(p_ptr->tim_hold_life + turns, FALSE);
        if (p_ptr->lev >= 35)
            set_tim_res_nether(p_ptr->tim_res_nether + turns, FALSE);
    
        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _barricade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Barricade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Creates some nearby rubble.");
        break;
    case SPELL_CAST:
    {
        int y, x, dir, cdir;
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        
        if (p_ptr->lev >= 45)
        {
            for (dir = 0; dir < 8; dir++)
            {
                y = py + ddy_ddd[dir];
                x = px + ddx_ddd[dir];

                if (!in_bounds(y, x)) continue;
                if (!cave_naked_bold(y, x)) continue;
                cave_set_feat(y, x, feat_rubble);
            }
        }
        else
        {
            if (!get_rep_dir2(&dir)) return;
            if (dir == 5) return;

            for (cdir = 0;cdir < 8; cdir++)
                if (cdd[cdir] == dir) break;

            if (cdir == 8) return;

            y = py + ddy_cdd[cdir];
            x = px + ddx_cdd[cdir];

            if (in_bounds(y, x) && cave_naked_bold(y, x))
                cave_set_feat(y, x, feat_rubble);

            if (p_ptr->lev >= 35)
            {
                y = py + ddy_cdd[(cdir + 7) % 8];
                x = px + ddx_cdd[(cdir + 7) % 8];
                if (in_bounds(y, x) && cave_naked_bold(y, x))
                    cave_set_feat(y, x, feat_rubble);

                y = py + ddy_cdd[(cdir + 1) % 8];
                x = px + ddx_cdd[(cdir + 1) % 8];
                if (in_bounds(y, x) && cave_naked_bold(y, x))
                    cave_set_feat(y, x, feat_rubble);
            }
        }
        p_ptr->update |= (PU_BONUS | PU_FLOW);
        p_ptr->redraw |= PR_MAP;
        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _strength_of_the_undertaker_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Strength of Undertaker");
        break;
    case SPELL_DESC:
        var_set_string(res, "You gain additional strength based on the quality of your digger.");
        break;
    default:
        _toggle_spell(TOGGLE_STRENGTH_OF_THE_UNDERTAKER, cmd, res);
        break;
    }
}

static void _stoicism_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Stoicism");
        break;
    case SPELL_DESC:
        var_set_string(res, "You gain additional constitution and stealth based on the quality of your digger.");
        break;
    default:
        _toggle_spell(TOGGLE_STOICISM, cmd, res);
        break;
    }
}

static void _industrious_mortician_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Industrious Mortician");
        break;
    case SPELL_DESC:
        var_set_string(res, "You gain additional attacks and speed when using this technique based on the quality of your digger.");
        break;
    default:
        _toggle_spell(TOGGLE_INDUSTRIOUS_MORTICIAN, cmd, res);
        break;
    }
}

/****************************************************************
 * Polearmmaster
 ****************************************************************/
static void _many_strike_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Many Strike");
        break;
    case SPELL_DESC:
        var_set_string(res, "Your attacks scatter widely among surrounding foes.");
        break;
    default:
        _toggle_spell(TOGGLE_MANY_STRIKE, cmd, res);
        break;
    }
}

static void _piercing_strike_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Piercing Strike");
        break;
    case SPELL_DESC:
        var_set_string(res, "When you attack a foe, successful hits will pierce the opponent attacking additional monsters.");
        break;
    default:
        _toggle_spell(TOGGLE_PIERCING_STRIKE, cmd, res);
        break;
    }
}

static void _trip_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Trip");
        break;
    case SPELL_DESC:
        var_set_string(res, "When you attack a foe, successful hits will attempt to trip up your opponent.");
        break;
    default:
        _toggle_spell(TOGGLE_TRIP, cmd, res);
        break;
    }
}

static void _reach_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Reach");
        break;
    case SPELL_DESC:
        var_set_string(res, "This spell extends the range of your melee attack.");
        break;
    case SPELL_CAST:
        if (_check_speciality_equip())
        {
            int dir = 5;
            bool b = FALSE;

            project_length = 2 + p_ptr->lev/40;
            if (get_aim_dir(&dir))
            {
                project_hook(GF_ATTACK, dir, HISSATSU_2, PROJECT_STOP | PROJECT_KILL);
                b = TRUE;
            }
            var_set_bool(res, b);
        }
        else
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            var_set_bool(res, FALSE);
        }
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _knock_back_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Knock Back");
        break;
    case SPELL_DESC:
        var_set_string(res, "A successful attack will push your opponent back a square.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_KNOCK_BACK));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _reaping_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Reaping");
        break;
    case SPELL_DESC:
        var_set_string(res, "A successful strike will damage all adjacent opponents granting you wraithform.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_REAPING));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

/****************************************************************
 * Shieldmaster
 ****************************************************************/
static void _desperation_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Desperation");
        break;
    case SPELL_DESC:
        var_set_string(res, "You regain hp by disenchanting your current weapon.");
        break;
    case SPELL_CAST:
    {
        int slot, ds, hp;
        object_type *o_ptr = NULL;
        char o_name[MAX_NLEN];

        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do need a shield.");
            return;
        }
        slot = equip_random_slot(object_is_melee_weapon);
        if (!slot)
        {
            msg_print("Failed! You do need a weapon to disenchant.");
            return;
        }
        o_ptr = equip_obj(slot);
        ds = o_ptr->to_h + o_ptr->to_d;
        object_desc(o_name, o_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);

        if (ds > 0)
        {
            hp = damroll(7, ds);
            hp_player(hp);

            if (!object_is_artifact(o_ptr) || one_in_(2))
            {
                if (o_ptr->to_h > 0) o_ptr->to_h--;
                if (o_ptr->to_d > 0) o_ptr->to_d--;
                msg_format("Your %s is disenchanted.", o_name);
            }
            else
            {
                msg_format("Your %s resists disenchantment.", o_name);
            }
        }
        else
        {
            msg_format("Your %s is too weak to help you any more.", o_name);
        }
        
        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _sanctuary_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Sanctuary");
        break;
    case SPELL_DESC:
        var_set_string(res, "You become invulnerable until you damage a monster.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do need a shield.");
            return;
        }
        set_sanctuary(TRUE);
        var_set_bool(res, TRUE);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _shield_bash_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Shield Bash");
        break;
    case SPELL_DESC:
        var_set_string(res, "You fight with your shield rather than your weapon.");
        break;
    default:
        _toggle_spell(TOGGLE_SHIELD_BASH, cmd, res);
        break;
    }
}

static void _bulwark_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Bulwark");
        break;
    case SPELL_DESC:
        var_set_string(res, "All melee damage that you receive is reduced.");
        break;
    default:
        _toggle_spell(TOGGLE_BULWARK, cmd, res);
        break;
    }
}

static void _shield_revenge_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Revenge");
        break;
    case SPELL_DESC:
        var_set_string(res, "Monsters are damaged whenever they hurt you.");
        break;
    default:
        _toggle_spell(TOGGLE_SHIELD_REVENGE, cmd, res);
        break;
    }
}

/****************************************************************
 * Slingmaster
 ****************************************************************/
static void _bouncing_pebble_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Bouncing Pebble");
        break;
    case SPELL_DESC:
        var_set_string(res, "Fires a pebble or shot at an opponent. If you hit, the pebble or shot will ricochet in a random direction.");
        break;
    default:
        _fire_spell(SHOOT_BOUNCE, cmd, res);
    }
}

static void _greater_many_shot_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Greater Many Shot");
        break;
    case SPELL_DESC:
        var_set_string(res, "Fires pebbles at all visible monsters.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your shooter.");
            return;
        }
        else
        {
            int i, item;
            int tgts[_MAX_TARGETS];
            int ct = _get_greater_many_shot_targets(tgts, _MAX_TARGETS);
            object_type *bow = equip_obj(p_ptr->shooter_info.slot);

            item_tester_tval = p_ptr->shooter_info.tval_ammo;
            if (!get_item(&item, 
                          "Fire which ammo? ", 
                          "You have nothing to fire.", (USE_INVEN | USE_QUIVER)))
            {
                flush();
                return;
            }

            shoot_hack = SHOOT_ALL;

            for (i = 0; i < ct; i++)
            {
                int tgt = tgts[i];
                int tx = m_list[tgt].fx;
                int ty = m_list[tgt].fy;

                do_cmd_fire_aux2(item, bow, px, py, tx, ty);
            }

            shoot_hack = SHOOT_NONE;
            var_set_bool(res, TRUE);
        }
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _many_shot_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Many Shot");
        break;
    case SPELL_DESC:
        var_set_string(res, "Fires pebbles at all visible monsters. You need to have a direct line of fire to each target, though.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your shooter.");
            return;
        }
        else
        {
            int i, item;
            int tgts[_MAX_TARGETS];
            int ct = _get_many_shot_targets(tgts, _MAX_TARGETS);
            object_type *bow = equip_obj(p_ptr->shooter_info.slot);

            item_tester_tval = p_ptr->shooter_info.tval_ammo;
            if (!get_item(&item, 
                          "Fire which ammo? ", 
                          "You have nothing to fire.", (USE_INVEN | USE_QUIVER)))
            {
                flush();
                return;
            }

            shoot_hack = SHOOT_MANY;

            for (i = 0; i < ct; i++)
            {
                int tgt = tgts[i];
                int tx = m_list[tgt].fx;
                int ty = m_list[tgt].fy;

                do_cmd_fire_aux2(item, bow, px, py, tx, ty);
            }

            shoot_hack = SHOOT_NONE;
            var_set_bool(res, TRUE);
        }
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _rapid_shot_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Rapid Shot");
        break;
    case SPELL_DESC:
        var_set_string(res, "When using this technique, all of your shots will launch against a single opponent.");
        break;
    default:
        _toggle_spell(TOGGLE_RAPID_SHOT, cmd, res);
        break;
    }
}

static void _shot_on_the_run_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Shoot on the Run");
        break;
    case SPELL_DESC:
        var_set_string(res, "When using this technique, you automatically fire at the closest opponent every time you move.");
        break;
    case SPELL_CAST:
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your shooter.");
            return;
        }
        if (_get_toggle() == TOGGLE_SHOT_ON_THE_RUN)
            _set_toggle(TOGGLE_NONE);
        else
        {
            /* Prompt for ammo to use, but disallow choosing from the floor since
               the player will be moving */
            item_tester_tval = p_ptr->shooter_info.tval_ammo;
            if (!get_item(&shoot_item, 
                          "Choose ammo for this technique.", 
                          "You have nothing to fire.", (USE_INVEN | USE_QUIVER)))
            {
                flush();
                return;
            }
            _set_toggle(TOGGLE_SHOT_ON_THE_RUN);
            /* _move_player() will handle the gritty details */
        }
        var_set_bool(res, TRUE);
        break;
    case SPELL_ENERGY:
        if (_get_toggle() != TOGGLE_SHOT_ON_THE_RUN)
            var_set_int(res, 0);    /* no charge for dismissing a technique */
        else
            var_set_int(res, 100);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

/****************************************************************
 * Staffmaster
 ****************************************************************/
bool _design_monkey_clone(void)
{
    int hand, i;
    monster_race *r_ptr = &r_info[MON_MONKEY_CLONE];
    int dd = 10;
    int ds = 10;
    int dam = 0;
    int tdam = 0;
    int blows = 0;
    int acc = 0;

    if (r_ptr->cur_num == 1)
    {
        msg_print("You may only have one Monkey Clone at a time!");
        return FALSE;
    }

    r_ptr->hdice = 10;
    r_ptr->hside = p_ptr->mhp / 15;
    r_ptr->ac = p_ptr->ac + p_ptr->to_a;
    r_ptr->speed = (byte)p_ptr->pspeed;

    /* Combat */
    for (hand = 0; hand < MAX_HANDS; hand++)
    {
        if (p_ptr->weapon_info[hand].wield_how != WIELD_NONE)
        {
            object_type *o_ptr = equip_obj(p_ptr->weapon_info[hand].slot);
            int          dd, ds;

            if (!o_ptr) continue;

            dd = o_ptr->dd + p_ptr->weapon_info[hand].to_dd;
            ds = o_ptr->ds + p_ptr->weapon_info[hand].to_ds;

            tdam += NUM_BLOWS(hand) * 
                (dd * (ds + 1)/2 + p_ptr->weapon_info[hand].to_d + o_ptr->to_d)/100;
    
            blows += NUM_BLOWS(hand)/100;
            acc = hit_chance(hand, o_ptr->to_h, 150);
        }
    }

    dam = tdam / MIN(4, blows);
    dd = 10;
    ds = dam/5;
    if (ds < 1) 
    {
        dd = 1;
        ds = 1;
    }

    r_ptr->level = (60*acc + 5200)/(300 - 3*acc); /* Don't ask ... */

    for (i = 0; i < 4 && i < blows; i++)
    {
        r_ptr->blow[i].method = 0;
    }

    for (i = 0; i < 4; i++)
    {
        r_ptr->blow[i].method = RBM_HIT;
        r_ptr->blow[i].effect = RBE_HURT;
        r_ptr->blow[i].d_dice = dd;
        r_ptr->blow[i].d_side = ds;
    }

    /* Resistances */
    r_ptr->flagsr = 0;
    r_ptr->flags3 = 0;
    r_ptr->flags2 = 0;
    r_ptr->flags7 = 0;

    if (p_ptr->resist[RES_ACID]) r_ptr->flagsr |= RFR_RES_ACID;
    if (p_ptr->resist[RES_ELEC]) r_ptr->flagsr |= RFR_RES_ELEC;
    if (p_ptr->resist[RES_FIRE]) r_ptr->flagsr |= RFR_RES_FIRE;
    if (p_ptr->resist[RES_COLD]) r_ptr->flagsr |= RFR_RES_COLD;
    if (p_ptr->resist[RES_POIS]) r_ptr->flagsr |= RFR_RES_POIS;
    if (p_ptr->resist[RES_LITE]) r_ptr->flagsr |= RFR_RES_LITE;
    if (p_ptr->resist[RES_DARK]) r_ptr->flagsr |= RFR_RES_DARK;
    if (p_ptr->resist[RES_NETHER]) r_ptr->flagsr |= RFR_RES_NETH;
    if (p_ptr->resist[RES_SHARDS]) r_ptr->flagsr |= RFR_RES_SHAR;
    if (p_ptr->resist[RES_SOUND]) r_ptr->flagsr |= RFR_RES_SOUN;
    if (p_ptr->resist[RES_CHAOS]) r_ptr->flagsr |= RFR_RES_CHAO;
    if (p_ptr->resist[RES_NEXUS]) r_ptr->flagsr |= RFR_RES_NEXU;
    if (p_ptr->resist[RES_DISEN]) r_ptr->flagsr |= RFR_RES_DISE;

    if (p_ptr->resist[RES_CONF]) r_ptr->flags3 |= RF3_NO_CONF;
    if (p_ptr->resist[RES_FEAR]) r_ptr->flags3 |= RF3_NO_FEAR;
    if (p_ptr->free_act) r_ptr->flags3 |= RF3_NO_SLEEP;
    if (p_ptr->sh_cold) r_ptr->flags3 |= RF3_AURA_COLD;

    if (p_ptr->reflect) r_ptr->flags2 |= RF2_REFLECTING;
    if (p_ptr->regenerate) r_ptr->flags2 |= RF2_REGENERATE;
    if (p_ptr->sh_fire) r_ptr->flags2 |= RF2_AURA_FIRE;
    if (p_ptr->sh_elec) r_ptr->flags2 |= RF2_AURA_ELEC;
    if (p_ptr->pass_wall) r_ptr->flags2 |= RF2_PASS_WALL;
    r_ptr->flags2 |= RF2_OPEN_DOOR;
    r_ptr->flags2 |= RF2_CAN_SPEAK;

    r_ptr->flags7 |= RF7_CAN_SWIM;
    if (p_ptr->levitation) r_ptr->flags7 |= RF7_CAN_FLY;

    return TRUE;
}

static void _monkey_king_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Monkey King's Technique");
        break;
    case SPELL_DESC:
        var_set_string(res, "Create a clone of yourself, but at great cost.");
        break;
    case SPELL_CAST:
    {
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        if (_design_monkey_clone() && summon_named_creature(0, py, px, MON_MONKEY_CLONE, PM_FORCE_PET))
            var_set_bool(res, TRUE);
        break;
    }
    case SPELL_COST_EXTRA:
        var_set_int(res, (p_ptr->mhp + 2)/3);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

void _circle_kick(void)
{
    int i;
    int dd = 1;
    int ds = p_ptr->lev;
    int bonus = p_ptr->to_h_m;
    int chance = p_ptr->skills.thn + (bonus * BTH_PLUS_ADJ);
    int slot = equip_find_object(TV_BOOTS, SV_ANY);

    if (slot)
        dd = equip_obj(slot)->ac;
    
    for (i = 0; i < 8; i++)
    {    
        int           dir = cdd[i];
        int           y = py + ddy[dir];
        int           x = px + ddx[dir];
        cave_type    *c_ptr = &cave[y][x];
        monster_type *m_ptr = &m_list[c_ptr->m_idx];

        if (c_ptr->m_idx && (m_ptr->ml || cave_have_flag_bold(y, x, FF_PROJECT)))
        {
        monster_race *r_ptr = &r_info[m_ptr->r_idx];
        char          m_name[MAX_NLEN];

            monster_desc(m_name, m_ptr, 0);
            
            if (test_hit_norm(chance, MON_AC(r_ptr, m_ptr), m_ptr->ml))
            {
                int dam = damroll(dd, ds) + p_ptr->to_d_m;

                sound(SOUND_HIT);
                msg_format("You kick %s.", m_name);

                if (!(r_ptr->flags3 & RF3_NO_STUN))
                {
                    if (MON_STUNNED(m_ptr))
                        msg_format("%s is more dazed.", m_name);
                    else
                        msg_format("%s is dazed.", m_name);

                    set_monster_stunned(c_ptr->m_idx, MON_STUNNED(m_ptr) + 1);
                }
                else
                    msg_format("%s is not affected.", m_name);


                dam = mon_damage_mod(m_ptr, dam, FALSE);

                if (dam > 0)
                {
                    bool fear;
                    mon_take_hit(c_ptr->m_idx, dam, &fear, NULL);

                    anger_monster(m_ptr);
                }
                retaliation_count = 0; /* AURA_REVENGE */
                touch_zap_player(c_ptr->m_idx);
            }
            else
            {
                sound(SOUND_MISS);
                msg_format("You miss %s.", m_name);
            }
        }
    }
}

static void _circle_kick_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Circle Kick");
        break;
    case SPELL_DESC:
        var_set_string(res, "Kicks all adjacent opponents, stunning them. Damage depends on your boots!");
        break;
    case SPELL_INFO:
    {
        int ds = p_ptr->lev;
        int dd = 0;
        int slot = equip_find_object(TV_BOOTS, SV_ANY);

        if (slot)
            dd = equip_obj(slot)->ac;

        var_set_string(res, info_damage(dd, ds, p_ptr->to_d_m));
        break;
    }
    case SPELL_CAST:
    {
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        _circle_kick();
        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

 static bool _vault_attack(void)
{
    int tx, ty;
    int tm_idx = 0;
    u16b path_g[32];
    int path_n, i;
    bool moved = FALSE;
    int flg = PROJECT_THRU | PROJECT_KILL;
    int dir;

    project_length = 3;

    if (!get_aim_dir(&dir)) return FALSE;

    tx = px + project_length * ddx[dir];
    ty = py + project_length * ddy[dir];

    if ((dir == 5) && target_okay())
    {
        tx = target_col;
        ty = target_row;
    }

    if (in_bounds(ty, tx)) tm_idx = cave[ty][tx].m_idx;

    path_n = project_path(path_g, project_length, py, px, ty, tx, flg);
    project_length = 0;

    if (!path_n) return FALSE;

    ty = py;
    tx = px;

    /* Scrolling the cave would invalidate our path! */
    if (!dun_level && !p_ptr->wild_mode && !p_ptr->inside_arena && !p_ptr->inside_battle)
        wilderness_scroll_lock = TRUE;

    for (i = 0; i < path_n; i++)
    {
        cave_type *c_ptr;
        bool can_enter = FALSE;
        int ny = GRID_Y(path_g[i]);
        int nx = GRID_X(path_g[i]);
        
        c_ptr = &cave[ny][nx];
        can_enter = !c_ptr->m_idx && player_can_enter(c_ptr->feat, 0);

        if (can_enter)
        {
            ty = ny;
            tx = nx;
            continue;
        }

        if (!c_ptr->m_idx)
        {
            msg_print("Failed!");
            break;
        }

        /* Move player before updating the monster */
        if (!player_bold(ty, tx)) move_player_effect(ty, tx, MPE_FORGET_FLOW | MPE_HANDLE_STUFF | MPE_DONT_PICKUP);
        moved = TRUE;
        
        update_mon(c_ptr->m_idx, TRUE);

        if (tm_idx != c_ptr->m_idx)
        {
            /* Just like "Acrobatic Charge." Attempts to displace monsters on route. */
            set_monster_csleep(c_ptr->m_idx, 0);
            move_player_effect(ny, nx, MPE_FORGET_FLOW | MPE_HANDLE_STUFF | MPE_DONT_PICKUP);
            ty = ny;
            tx = nx;
            continue;
        }
        py_attack(ny, nx, 0);
        break;
    }

    if (!moved && !player_bold(ty, tx)) move_player_effect(ty, tx, MPE_FORGET_FLOW | MPE_HANDLE_STUFF | MPE_DONT_PICKUP);

    if (!dun_level && !p_ptr->wild_mode && !p_ptr->inside_arena && !p_ptr->inside_battle)
    {
        wilderness_scroll_lock = FALSE;
        wilderness_move_player(px, py);
    }
    return TRUE;
}

static void _vault_attack_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Vault Attack");
        break;
    case SPELL_DESC:
        var_set_string(res, "Charge and attack a nearby opponent in a single move.");
        break;
    case SPELL_INFO:
        var_set_string(res, info_range(3));
        break;
    case SPELL_CAST:
    {
        var_set_bool(res, FALSE);
        if (!_check_speciality_equip())
        {
            msg_print("Failed! You do not feel comfortable with your weapon.");
            return;
        }
        if (_vault_attack())
            var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static void _flurry_of_blows_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Flurry of Blows");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attack a single opponent with a great number of blows, exhausting yourself in the process.");
        break;
    case SPELL_CAST:
        var_set_bool(res, _do_blow(WEAPONMASTER_FLURRY));
        break;
    case SPELL_ENERGY:
        var_set_int(res, 100 + ENERGY_NEED());
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}


/****************************************************************
 * Swordmaster
 ****************************************************************/
static void _burning_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Burning Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Damage from your blade becomes fire damage and never misses.");
        break;
    default:
        _toggle_spell(TOGGLE_BURNING_BLADE, cmd, res);
        break;
    }
}

static void _ice_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Ice Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Damage from your blade becomes frost damage and slows your opponent.");
        break;
    default:
        _toggle_spell(TOGGLE_ICE_BLADE, cmd, res);
        break;
    }
}

static void _thunder_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Thunder Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Damage from your blade becomes lightning damage and stuns your opponent.");
        break;
    default:
        _toggle_spell(TOGGLE_THUNDER_BLADE, cmd, res);
        break;
    }
}

static void _blood_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Blood Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Your blade thirsts for the living.");
        break;
    default:
        _toggle_spell(TOGGLE_BLOOD_BLADE, cmd, res);
        break;
    }
}

static void _holy_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Holy Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Your blade fights powerful against the forces of evil.");
        break;
    default:
        _toggle_spell(TOGGLE_HOLY_BLADE, cmd, res);
        break;
    }
}

static void _order_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Order Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "Your blade becomes a weapon of order, always dealing maximal damage.");
        break;
    default:
        _toggle_spell(TOGGLE_ORDER_BLADE, cmd, res);
        break;
    }
}

static void _wild_blade_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Wild Blade");
        break;
    case SPELL_DESC:
        var_set_string(res, "This is too crazy to describe. You wouldn't believe me anyway!");
        break;
    default:
        _toggle_spell(TOGGLE_WILD_BLADE, cmd, res);
        break;
    }
}


/****************************************************************
 * Spell Table and Exports
 ****************************************************************/

#define _MAX_OBJECTS_PER_SPECIALITY 32
#define _MAX_SPECIALITIES           11
#define _MAX_SPELLS_PER_SPECIALITY  10

int weaponmaster_get_toggle(void)
{
    /* exposed for prtstatus() in xtra1.c 
       this is easier than rewriting the status code so that classes can maintain it!
    */
    int result = TOGGLE_NONE;
    if (p_ptr->pclass == CLASS_WEAPONMASTER)
        result = _get_toggle();
    return result;
}

void weaponmaster_set_toggle(int toggle)
{
    if (p_ptr->pclass == CLASS_WEAPONMASTER)
        _set_toggle(toggle);
}
  
#define _WEAPONMASTER_MELEE   1
#define _WEAPONMASTER_SHIELDS 2
#define _WEAPONMASTER_BOWS    3

typedef struct {
    byte tval;
    byte sval;
} _object_kind;

typedef struct {
    cptr name;
    cptr help;
    int kind;
    int stats[MAX_STATS];
    skills_t base_skills;
    skills_t extra_skills;
    _object_kind objects[_MAX_OBJECTS_PER_SPECIALITY];    /* There is always a sentinel at the end */
    spell_info spells[_MAX_SPELLS_PER_SPECIALITY];        /* There is always a sentinel at the end */
    _object_kind birth_obj;
} _speciality;


/*  p_ptr->psubclass indexes into _specialities.
    This index is persisted in savefiles and are chosen
    by the player at startup, so moving things around is 
    unwise unless you put code to fix up old savefiles 
    in load.c.
*/
static _speciality _specialities[_MAX_SPECIALITIES] = {
    { "Axes",
      "The mighty axe! Your blows will cleave with damage unsurpassed. "
      "Specializing in axes gives great offensive prowess, especially when "
      "your axe is wielded with two hands. However, this speciality offers "
      "little in the way of utility. Kill quickly as your life depends on it!",
      _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      {+3, -2, -1, -2, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  23,  31,   1,  14,   2, 70, 25},
      {   9,   7,  10,   0,   0,   0, 30, 11},
      { { TV_POLEARM, SV_BATTLE_AXE },
        { TV_POLEARM, SV_BEAKED_AXE },
        { TV_POLEARM, SV_BROAD_AXE },
        { TV_POLEARM, SV_LOCHABER_AXE },
        { TV_POLEARM, SV_GREAT_AXE },
        { 0, 0 },
      },
      { { 10,   0,  0, _power_attack_spell },
        { 15,  10,  0, berserk_spell },
        { 25,  20, 50, _judge_spell },
        { 25,  12,  0, _crusaders_strike_spell },
        { 35,  25, 50, massacre_spell },
        { 40,  25,  0, _vicious_strike_spell },
        { -1,   0,  0, NULL },
      },
      { TV_POLEARM, SV_BROAD_AXE },
    },
    { "Bows",
      "You will shoot to kill! The bowmaster gains techniques to enhance shooting, "
      "including more rapid firing, the ability to volley shots over the heads of "
      "intervening monsters, reduced ammo destruction, the ability to kill with a "
      "single arrow and much more. As a shooter, your missile prowess will be quite "
      "formidable, though your melee will be somewhat lacking.",
      _WEAPONMASTER_BOWS,
    /*  S   I   W   D   C   C */
      { 0,  0,  0, +2, -1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  30,  30,  29,   4,  23,  13, 48, 72},
      {   8,  10,  10,   0,   0,   0, 13, 28},
      { { TV_BOW, SV_SHORT_BOW },
        { TV_BOW, SV_LONG_BOW },
        { TV_BOW, SV_NAMAKE_BOW },
        { 0, 0 },
      },
      { {  5,   0,  0, _readied_shot_spell },
        { 10,   5,  0, _volley_spell },
        { 25,  20, 50, _judge_spell },
        { 25,  10,  0, _tranquilizing_arrow_spell },
        { 30,   0,  0, _piercing_arrow_spell },
        { 35,  20,  0, _arrow_of_slaying_spell },
        { 40,  30,  0, _disintegration_arrow_spell },
        { -1,   0,  0, NULL },
      },
      { TV_BOW, SV_SHORT_BOW },
    },
    { "Clubs",
        "You will seek to club your opponents senseless! This speciality gains passive "
        "status effects against monsters, such as confusion, knock out and stunning. Also, "
        "you will gain some limited utility techniques. At high levels, your weapons will "
        "become more likely to score devastating, crushing blows against your hapless enemies.",
        _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      {+2, -1, -1, -2, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  25,  35,   1,  14,   2, 65, 60},
      {   9,  10,  12,   0,   0,   0, 20, 18},
        { { TV_HAFTED, SV_BALL_AND_CHAIN },
          { TV_HAFTED, SV_CLUB },
          { TV_HAFTED, SV_FLAIL },
          { TV_HAFTED, SV_GREAT_HAMMER },
          { TV_HAFTED, SV_LEAD_FILLED_MACE },
          { TV_HAFTED, SV_MACE },
          { TV_HAFTED, SV_MACE_OF_DISRUPTION },
          { TV_HAFTED, SV_MORNING_STAR },
          { TV_HAFTED, SV_TWO_HANDED_FLAIL },
          { TV_HAFTED, SV_WAR_HAMMER },
          { TV_HAFTED, SV_GROND },
          { TV_HAFTED, SV_NAMAKE_HAMMER },
          { 0, 0 },
        },
        { {  5,   0,  0, _combat_expertise_spell },
          { 10,   5,  0, _throw_weapon_spell },
          { 15,  10,  0, _cunning_strike_spell },
          { 25,  20, 50, _judge_spell },
          { 25,  15, 40, _smash_ground_spell },
          { 30,  25,  0, _smite_evil_spell },
          { 35,   0,  0, _trade_blows_spell },
          { -1,   0,  0, NULL },
        },
        { TV_HAFTED, SV_CLUB },
    },
    { "Crossbows",
      "The crossbowmaster shoots deadly bolts for great damage. Their bolts may explode "
      "powerfully damaging nearby monsters. Also, they may shoot so hard as to knock "
      "their opponents backwards!",
      _WEAPONMASTER_BOWS,
    /*  S   I   W   D   C   C */
      {+1, -1, -1, +1, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  30,  30,  29,   3,  18,  10, 48, 72},
      {   8,   9,   9,   0,   0,   0, 13, 28},
      { { TV_BOW, SV_LIGHT_XBOW },
        { TV_BOW, SV_HEAVY_XBOW },
        { 0, 0 },
      },
      { {  5,   0,  0, _rapid_reload_spell },
        { 10,  10,  0, _shattering_bolt_spell },
        { 25,  20, 50, _judge_spell },
        { 25,  15,  0, _knockback_bolt_spell },
        { 30,   0,  0, _exploding_bolt_spell },
        { 35,  30,  0, _elemental_bolt_spell },
        { 40,   0,  0, _overdraw_spell },
        { -1,   0,  0, NULL },
      },
      { TV_BOW, SV_LIGHT_XBOW },
    },
    { "Daggers",
      "A knife in the back! This speciality favors dual wielding and rogue-like behavior. "
      "The daggermaster can even assume the posture of The Flying Dagger which greatly "
      "enhances their low level dagger toss capability. Indeed, their prowess with the "
      "dagger toss is legendary and appears almost magical! At high levels, you will also "
      "gain formidable melee prowess with the Frenzy Stance. Finally, daggermasters have "
      "very strong short ranged teleport techniques that synergize well with their toss "
      "abilities.",
      _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      { 0, +1,  0, +3, -1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  30,  32,  31,   5,  30,  20, 60, 66},
      {  12,  10,  10,   0,   0,   0, 18, 20},
      { { TV_SWORD, SV_BASILLARD },
        { TV_SWORD, SV_BROKEN_DAGGER },
        { TV_SWORD, SV_DAGGER },
        { TV_SWORD, SV_FALCON_SWORD },
        { TV_SWORD, SV_MAIN_GAUCHE },
        { TV_SWORD, SV_NINJATO },
        { TV_SWORD, SV_RAPIER },
        { TV_SWORD, SV_SABRE },
        { TV_SWORD, SV_TANTO },
        { TV_SWORD, SV_DRAGON_FANG },
        { 0, 0 },
      },
      {
        {  5,   5,  0, _dagger_toss_spell },
        { 10,   5, 40, strafing_spell },
        { 15,   0,  0, _flying_dagger_spell },
        { 25,  20, 50, _judge_spell },
        { 30,  10,  0, _elusive_strike_spell },
        { 35,   0,  0, _shadow_stance_spell },
        { 45,   0,  0, _frenzy_spell },
        { -1,   0,  0, NULL },
      },
      { TV_SWORD, SV_DAGGER },
    },
    { "Polearms",
      "You don a grim face before setting out to reap your harvest of death. You will swing "
      "your weapon wide often affecting multiple surrounding opponents.",
      _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      {+2, -1, -1,  0, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  23,  31,   1,  14,   2, 68, 25},
      {  10,   8,  10,   0,   0,   0, 28, 11},
      {
        { TV_POLEARM, SV_AWL_PIKE },
        { TV_POLEARM, SV_BROAD_SPEAR },
        { TV_POLEARM, SV_DEATH_SCYTHE },
        { TV_POLEARM, SV_HALBERD },
        { TV_POLEARM, SV_FAUCHARD },
        { TV_POLEARM, SV_GLAIVE },
        { TV_POLEARM, SV_GUISARME },
        { TV_POLEARM, SV_LUCERNE_HAMMER },
        { TV_POLEARM, SV_NAGINATA },
        { TV_POLEARM, SV_PIKE },
        { TV_POLEARM, SV_SCYTHE },
        { TV_POLEARM, SV_SCYTHE_OF_SLICING },
        { TV_POLEARM, SV_SPEAR },
        { TV_POLEARM, SV_TRIDENT },
        { TV_POLEARM, SV_LANCE },
        { TV_POLEARM, SV_HEAVY_LANCE },
        { TV_POLEARM, SV_TRIFURCATE_SPEAR },
        { 0, 0 },
      },
      {
        {  5,   0,  0, _many_strike_spell },
        { 10,   5,  0, _reach_spell },
        { 15,  15,  0, _knock_back_spell },
        { 25,  20, 50, _judge_spell },
        { 25,   0,  0, _piercing_strike_spell },
        { 35,   0,  0, _trip_spell },
        { 40,  40,  0, _reaping_spell },
        { -1,   0,  0, NULL },
      },
      { TV_POLEARM, SV_SPEAR },
    },
    { "Shields",
      "Specializing in shields gives excellent powers of defense and retaliation. In addition, "
      "you can even choose to melee with your shield rather than a normal weapon, bashing your "
      "opponents senseless. This form of combat is known as Shield Bashing and is unique to this "
      "speciality.",
      _WEAPONMASTER_SHIELDS,
    /*  S   I   W   D   C   C */
      {+2,  0, +1,  0, +2,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  24,  40,   1,  12,   2, 68, 25},
      {  10,   9,  12,   0,   0,   0, 21, 11},
      {
        { TV_SHIELD, SV_DRAGON_SHIELD },
        { TV_SHIELD, SV_KNIGHT_SHIELD },
        { TV_SHIELD, SV_LARGE_LEATHER_SHIELD },
        { TV_SHIELD, SV_LARGE_METAL_SHIELD },
        { TV_SHIELD, SV_MIRROR_SHIELD },
        { TV_SHIELD, SV_SMALL_LEATHER_SHIELD },
        { TV_SHIELD, SV_SMALL_METAL_SHIELD },
        { 0, 0 },
      },
      {
        { 10,  0,  0, _shield_bash_spell },
        { 15, 10,  0, _desperation_spell },
        { 25,  20, 50, _judge_spell },
        { 30,  0,  0, _bulwark_spell },
        { 35, 50,  0, _sanctuary_spell },
        { 40,  0,  0, _shield_revenge_spell },
        { -1,  0,  0, NULL },
      },
      { TV_SHIELD, SV_SMALL_LEATHER_SHIELD },
    },
    { "Slings",
      "Watch out, Goliath! As a master of slings you will shoot pebbles with uncanny speed. Your "
      "shots may even ricochet off other monsters to score multiple hits. As with other archery "
      "specializations, your ammo will break less often and, at high levels you will gain access "
      "to an 'unlimited quiver' which allows you to shoot an infinite amount of (average) ammo.",
      _WEAPONMASTER_BOWS,
    /*  S   I   W   D   C   C */
      {-1, +1, +1, +3, -1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  30,  30,  29,   4,  23,  13, 48, 72},
      {   8,  10,  10,   0,   0,   0, 13, 28},
      { { TV_BOW, SV_SLING },
        { 0, 0 },
      },
      {
        {  5,   5,  0, _bouncing_pebble_spell },
        { 15,  15,  0, _many_shot_spell },
        { 25,  20, 50, _judge_spell },
        { 25,   0,  0, _shot_on_the_run_spell },
        { 30,  15,  0, _greater_many_shot_spell },
        { 35,   0,  0, _rapid_shot_spell },
        { -1,   0,  0, NULL },
      },
      { TV_BOW, SV_SLING },
    },
    { "Staves",
      "Monkey King! You will battle opponents with a flurry of blows from your mighty "
      "staff and will be prepared to counter the attacks of your enemies. You can vault into "
      "battle and circle kick enemies for stunning effects, attacking them with your boots! "
      "You may even eventually clone yourself at great cost.",
      _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      { 0,  0,  0, +2, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  25,  31,   2,  14,   4, 63, 25},
      {  10,   9,  10,   0,   0,   0, 26, 11},
      {
        { TV_HAFTED, SV_BO_STAFF },
        { TV_HAFTED, SV_JO_STAFF },
        { TV_HAFTED, SV_QUARTERSTAFF },
        { TV_HAFTED, SV_WIZSTAFF },
        { TV_HAFTED, SV_THREE_PIECE_ROD },
        { 0, 0 },
      },
      {
        { 15, 15, 0, _vault_attack_spell },
        { 25, 20,50, _judge_spell },
        { 25, 25, 0, _circle_kick_spell },
        { 40,  0, 0, _monkey_king_spell },
        { 45, 80, 0, _flurry_of_blows_spell },
        { -1,  0, 0, NULL },
      },
      { TV_HAFTED, SV_QUARTERSTAFF },
    },
    { "Swords",
      "You will become a true swordmaster! Mastery of the blade will augment "
      "your weapon with elemental, vorpal or vampiric powers.",
      _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      {+1, -1, -1, +1, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  23,  31,   1,  14,   2, 70, 25},
      {  11,   9,  10,   0,   0,   0, 29, 11},
      { { TV_SWORD, SV_BASTARD_SWORD } ,
        { TV_SWORD, SV_BROKEN_SWORD } ,
        { TV_SWORD, SV_BLADE_OF_CHAOS } ,
        { TV_SWORD, SV_BROAD_SWORD } ,
        { TV_SWORD, SV_CLAYMORE } ,
        { TV_SWORD, SV_CUTLASS } ,
        { TV_SWORD, SV_DIAMOND_EDGE } ,
        { TV_SWORD, SV_ESPADON } ,
        { TV_SWORD, SV_EXECUTIONERS_SWORD } ,
        { TV_SWORD, SV_FLAMBERGE } ,
        { TV_SWORD, SV_GREAT_SCIMITAR } , /* Falchion */
        { TV_SWORD, SV_KATANA } ,
        { TV_SWORD, SV_LONG_SWORD } ,
        { TV_SWORD, SV_KHOPESH } ,
        { TV_SWORD, SV_NO_DACHI },
        { TV_SWORD, SV_SCIMITAR } ,
        { TV_SWORD, SV_SHORT_SWORD } ,
        { TV_SWORD, SV_SMALL_SWORD } ,
        { TV_SWORD, SV_TULWAR } ,
        { TV_SWORD, SV_TWO_HANDED_SWORD } ,
        { TV_SWORD, SV_WAKIZASHI } ,
        { TV_SWORD, SV_ZWEIHANDER } ,
        { TV_SWORD, SV_RUNESWORD } ,
        { 0, 0 },
      },
      {
        {  5,   0,  0, _burning_blade_spell },
        { 10,   0,  0, _ice_blade_spell },
        { 15,   0,  0, _thunder_blade_spell },
        { 25,  20, 50, _judge_spell },
        { 25,   0,  0, _blood_blade_spell },
        { 30,   0,  0, _holy_blade_spell },
        { 35,   0,  0, _order_blade_spell },
        { 40,   0,  0, _wild_blade_spell },
        { -1,   0,  0, NULL },
      },
      { TV_SWORD, SV_LONG_SWORD } ,
    },
    { "Diggers",
      "A master of digging. You prefer rocky enclosures and don't mind "
      "lugging around a corpse or two, which you can bury in a pinch for "
      "a temporary bonus. You can choose one of several postures which allow "
      "you to use the digging bonus of your current weapon as an additional "
      "bonus, such as increased strength, constitution, or even speed! So keep "
      "your eye open for those +8 diggers!",
      _WEAPONMASTER_MELEE,
    /*  S   I   W   D   C   C */
      {+2, -1, -1,  0, +1,  0},
    /* Dsrm Dvce Save Stlh Srch Prcp Thn Thb*/
      {  25,  25,  33,   3,  14,   2, 60, 25},
      {   9,   9,  11,   0,   0,   0, 26, 11},
      {
        { TV_DIGGING, SV_SHOVEL},
        { TV_DIGGING, SV_GNOMISH_SHOVEL},
        { TV_DIGGING, SV_DWARVEN_SHOVEL},
        { TV_DIGGING, SV_PICK},
        { TV_DIGGING, SV_ORCISH_PICK},
        { TV_DIGGING, SV_DWARVEN_PICK},
        { TV_DIGGING, SV_MATTOCK},
        { 0, 0 },
      },
      {
        {  5, 10, 30, _bury_dead_spell },
        { 10,  0,  0, _strength_of_the_undertaker_spell },
        { 20, 20, 40, _tunnel_spell },
        { 25,  20, 50, _judge_spell },
        { 25,  0,  0, _stoicism_spell },
        { 30, 25, 40, _barricade_spell },
        { 35, 30,  0, _calamity_of_the_living_spell },
        { 40,  0,  0, _industrious_mortician_spell },
        { -1,  0,  0, NULL },
      },
      { TV_DIGGING, SV_PICK },
    },
};

static bool _check_speciality_aux(object_type *o_ptr)
{
    int i;
    _speciality *ptr = &_specialities[p_ptr->psubclass];

    for (i = 0; i < _MAX_OBJECTS_PER_SPECIALITY; i++)
    {
        if (ptr->objects[i].tval == 0) break;
        if (ptr->objects[i].tval == o_ptr->tval && ptr->objects[i].sval == o_ptr->sval) return TRUE;
    }

    return FALSE;
}

static bool _check_speciality_equip(void)
{
    bool result = equip_find_first(_check_speciality_aux);

    /* For melee specialities, all melee weapons must be favored. Shieldmasters
       and shooters just need a single matching object to be OK. */
    if (_specialities[p_ptr->psubclass].kind == _WEAPONMASTER_MELEE)
    {
        int slot;
        for (slot = equip_find_first(object_is_melee_weapon);
                slot;
                slot = equip_find_next(object_is_melee_weapon, slot))
        {
            object_type *o_ptr = equip_obj(slot);
            if (!_check_speciality_aux(o_ptr))
                return FALSE;
        }
    }
    return result;
}

bool weaponmaster_is_favorite(object_type *o_ptr)
{
    return _check_speciality_aux(o_ptr);
}

int weaponmaster_get_max_blows(object_type *o_ptr, int hand)
{
    int num = 100;
    switch (_specialities[p_ptr->psubclass].kind)
    {
    case _WEAPONMASTER_BOWS:
        num = 300;
        break;

    case _WEAPONMASTER_SHIELDS: /* Shieldmaster can bash or melee with aplomb */
        num = 500;
        break;

    case _WEAPONMASTER_MELEE:
        if (_check_speciality_aux(o_ptr))
        {
            switch (p_ptr->psubclass)
            {
            case WEAPONMASTER_AXES:
                num = 500;
                if (p_ptr->weapon_info[hand].wield_how == WIELD_TWO_HANDS)
                    num = 600;
                break;
            case WEAPONMASTER_DAGGERS:
                num = 500;
                if (_get_toggle() == TOGGLE_FRENZY_STANCE)
                    num = 600;
                break;
            case WEAPONMASTER_CLUBS:
                num = 525;
                break;
            case WEAPONMASTER_POLEARMS:
                num = 525;
                break;
            case WEAPONMASTER_STAVES:
                num = 500;
                break;
            case WEAPONMASTER_SWORDS:
                num = 525;
                break;
            case WEAPONMASTER_DIGGERS:
                num = 550;
                break;
            }
        }
        break;
    }
    return num;
}

static int _get_spells_aux(spell_info* spells, int max)
{
    int i;
    int ct = 0;

    for (i = 0; ; i++)
    {
        spell_info *base = &_specialities[p_ptr->psubclass].spells[i];
        if (base->level <= 0) break;
        if (ct >= max) break;
        if (base->level <= p_ptr->lev)
        {
            spell_info* current = &spells[ct++];
            current->fn = base->fn;
            current->level = base->level;
            current->cost = base->cost;
            current->fail = calculate_fail_rate(base->level, base->fail, p_ptr->stat_ind[A_STR]);
        }
    }

    return ct;
}

static int _get_spells(spell_info* spells, int max)
{
    int ct = _get_spells_aux(spells, max);

    if (ct == 0)
        msg_print("You need more experience. Why not kill something?");

    return ct;
}

static caster_info * _caster_info(void)
{
    static caster_info me = {0};
    static bool init = FALSE;
    if (!init)
    {
        me.which_stat = A_STR;
        me.magic_desc = "skill";
        me.options = CASTER_USE_HP;
        me.weight = 3000;
        init = TRUE;
    }
    return &me;
}

void _on_birth(void)
{
    object_type forge;
    _object_kind kind;
    int i;

    /* Give the player a starting weapon from this group */
    kind = _specialities[p_ptr->psubclass].birth_obj;
    object_prep(&forge, lookup_kind(kind.tval, kind.sval));
    add_outfit(&forge);

    if (kind.tval == TV_BOW)
    {
        switch (kind.sval)
        {
        case SV_SLING:
            object_prep(&forge, lookup_kind(TV_SHOT, SV_AMMO_NORMAL));
            forge.number = (byte)rand_range(15, 20);
            add_outfit(&forge);
            break;
        case SV_SHORT_BOW:
        case SV_LONG_BOW:
            object_prep(&forge, lookup_kind(TV_ARROW, SV_AMMO_NORMAL));
            forge.number = (byte)rand_range(15, 20);
            add_outfit(&forge);
            break;
        case SV_LIGHT_XBOW:
        case SV_HEAVY_XBOW:
            object_prep(&forge, lookup_kind(TV_BOLT, SV_AMMO_NORMAL));
            forge.number = (byte)rand_range(15, 20);
            add_outfit(&forge);
            break;
        }
    }

    for (i = 0; i < _MAX_OBJECTS_PER_SPECIALITY; i++)
    {
        kind = _specialities[p_ptr->psubclass].objects[i];
        if (kind.tval == 0) break;

        if (kind.tval != TV_SHIELD)
            p_ptr->weapon_exp[kind.tval-TV_WEAPON_BEGIN][kind.sval] = WEAPON_EXP_BEGINNER;
    }

    weaponmaster_adjust_skills();
}

static void _set_max_skill(int tval, int skill)
{
    int j;
    for (j = 0; j < 64; j++)
        s_info[p_ptr->pclass].w_max[tval - TV_WEAPON_BEGIN][j] = skill;
}

void weaponmaster_adjust_skills(void)
{
    int i, j;
    _object_kind kind;

    /* Fix up skills for Speciality. This needs to be called every time the game is loaded! */
    /* Bang everything in class (melee, bows, shields) down to unskilled max */
    switch (_specialities[p_ptr->psubclass].kind)
    {
    case _WEAPONMASTER_MELEE:
        _set_max_skill(TV_DIGGING, WEAPON_EXP_UNSKILLED);
        _set_max_skill(TV_HAFTED, WEAPON_EXP_UNSKILLED);
        _set_max_skill(TV_POLEARM, WEAPON_EXP_UNSKILLED);
        _set_max_skill(TV_SWORD, WEAPON_EXP_UNSKILLED);
        break;

    case _WEAPONMASTER_BOWS:
        _set_max_skill(TV_BOW, WEAPON_EXP_UNSKILLED);
        break;

    case _WEAPONMASTER_SHIELDS:
        /* TODO: We do not keep skills for shields. Probably never will, either ... */
        break;
    }

    /* Now make favored weapons "masterable" */
    for (i = 0; i < _MAX_OBJECTS_PER_SPECIALITY; i++)
    {
        kind = _specialities[p_ptr->psubclass].objects[i];
        if (kind.tval == 0) break;

        s_info[p_ptr->pclass].w_max[kind.tval-TV_WEAPON_BEGIN][kind.sval] = WEAPON_EXP_MASTER;
    }

    /* Patch up current skills since we keep changing the allowed maximums */
    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 64; j++)
        {
            if (p_ptr->weapon_exp[i][j] > s_info[p_ptr->pclass].w_max[i][j])
                p_ptr->weapon_exp[i][j] = s_info[p_ptr->pclass].w_max[i][j];
        }
    }
}

static int _max_pval(void)
{
    int slot;
    int result = 0;

    for (slot = equip_find_first(object_is_melee_weapon);
            slot;
            slot = equip_find_next(object_is_melee_weapon, slot))
    {
        object_type *o_ptr = equip_obj(slot);
        result = MAX(result, o_ptr->pval);
    }
    return result;
}

static void _calc_bonuses(void)
{
    static bool last_spec = FALSE;
    static bool init = FALSE;
    bool spec = _check_speciality_equip();

    p_ptr->speciality_equip = spec;

    /* Handle cases where user swaps in unfavorable gear */
    if (!spec && _get_toggle() != TOGGLE_NONE)
    {
        /* Triggering a recursive call to calc_bonuses would be bad ... */
        /*    _set_toggle(TOGGLE_NONE); */
        /* This assumes all bonus calcs are handled here and in _calc_weapon_bonuses() */
        p_ptr->magic_num1[0] = TOGGLE_NONE;
        p_ptr->redraw |= (PR_STATUS);
    }

    if (p_ptr->psubclass == WEAPONMASTER_SLINGS)
    {
        if (spec)
        {
            p_ptr->return_ammo = TRUE;

            if (p_ptr->lev >= 10)
                p_ptr->painted_target = TRUE;

            if (p_ptr->lev >= 20)
                p_ptr->big_shot = TRUE;

            if (p_ptr->lev >= 45)
                p_ptr->unlimited_quiver = TRUE;
        }
    } 
    else if (p_ptr->psubclass == WEAPONMASTER_BOWS)
    {
        if (spec)
        {
            if (p_ptr->lev >= 45)
                p_ptr->unlimited_quiver = TRUE;
        }
    } 
    else if (p_ptr->psubclass == WEAPONMASTER_CROSSBOWS)
    {
        if (spec)
        {
            switch (_get_toggle())
            {
            case TOGGLE_RAPID_RELOAD:
                p_ptr->to_a -= 30;
                p_ptr->dis_to_a -= 30;
                break;
            }
        }
    } 
    else if (p_ptr->psubclass == WEAPONMASTER_DAGGERS)
    {
        if (spec) 
        {
            p_ptr->easy_2weapon = TRUE;
            if (p_ptr->lev >= 25 && p_ptr->weapon_ct > 1)
            {
                p_ptr->to_a += 10 + p_ptr->lev/2;
                p_ptr->dis_to_a += 10 + p_ptr->lev/2;
            }

            p_ptr->skills.stl += p_ptr->lev/12;

            if (p_ptr->lev >= 30)
                p_ptr->sneak_attack = TRUE;

            switch (_get_toggle())
            {
            case TOGGLE_SHADOW_STANCE:
                p_ptr->shooter_info.to_d -= 10;
                p_ptr->shooter_info.dis_to_d -= 10;
                p_ptr->to_d_m -= 10;
                p_ptr->skills.stl += p_ptr->lev/12;
                break;

            case TOGGLE_FRENZY_STANCE:
                p_ptr->skills.stl -= 8;
                p_ptr->to_a -= 25;
                p_ptr->dis_to_a -= 25;
                break;
            }
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_CLUBS)
    {
        if (spec)
        {
            if (p_ptr->lev >= 40)
                p_ptr->enhanced_crit = TRUE;

            switch (_get_toggle())
            {
            case TOGGLE_TRADE_BLOWS:
                p_ptr->to_a -= 30;
                p_ptr->dis_to_a -= 30;
                break;
            case TOGGLE_COMBAT_EXPERTISE:
                p_ptr->to_a += 5 + p_ptr->lev;
                p_ptr->dis_to_a += 5 + p_ptr->lev;
                break;
            }
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_AXES)
    {
        if (p_ptr->tim_vicious_strike)
        {
            p_ptr->to_a -= 120;
            p_ptr->dis_to_a -= 120;
            /* AC should not go below 0 ... See xtra1.c calc_bonuses() */
        }

        if (spec)
        {
            if (p_ptr->lev >= 5)
                p_ptr->skill_dig += (5 * 20); /* As if you had a +5 digger ... */

            if (p_ptr->lev >= 30)
                p_ptr->cleave = TRUE;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SWORDS)
    {
        if (spec)
        {
            if (p_ptr->lev >= 20)
            {
                /* Hackery to keep the status bar up to date ... sigh */
                if (!IS_HERO())
                {
                    p_ptr->constant_hero = TRUE;
                    p_ptr->redraw |= (PR_STATUS);
                }
                else
                    p_ptr->constant_hero = TRUE;                
            }

            if (p_ptr->lev >= 45)
                p_ptr->vorpal = TRUE;
        }
        else if (last_spec && p_ptr->lev >= 20)
        {
            p_ptr->redraw |= (PR_STATUS);
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_POLEARMS)
    {
        if (spec)
        {
            if (p_ptr->lev >= 45)
                p_ptr->whirlwind = TRUE;

            if (p_ptr->lev >= 30)
            {
                if (p_ptr->entrench_ct >= 3) 
                {
                    p_ptr->entrenched = TRUE;
                    p_ptr->redraw |= PR_STATUS;

                    p_ptr->to_a += 20 + p_ptr->entrench_ct;
                    p_ptr->dis_to_a += 20 + p_ptr->entrench_ct;
                }
            }
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SHIELDS)
    {
        if (spec)
        {
            if (p_ptr->lev >= 20)
                p_ptr->inven_prot = TRUE;

            if (p_ptr->lev >= 45)
            {
                res_add(RES_ACID);
                res_add(RES_COLD);
                res_add(RES_FIRE);
                res_add(RES_ELEC);
                p_ptr->reflect = TRUE;
            }

            /* Block: Shield AC doubled. */
            if (p_ptr->lev >= 5)
            {
                int slot;
                for (slot = equip_find_first(object_is_shield);
                        slot;
                        slot = equip_find_next(object_is_shield, slot))
                {
                    object_type *o_ptr = equip_obj(slot);
                    p_ptr->to_a += k_info[o_ptr->k_idx].ac;
                    p_ptr->dis_to_a += k_info[o_ptr->k_idx].ac;
                    p_ptr->to_a += o_ptr->to_a;
                    if (object_is_known(o_ptr))
                        p_ptr->dis_to_a += o_ptr->to_a;
                }
            }

            /* Stalwart: +20 saving throws */
            if (p_ptr->lev >= 25)
                p_ptr->skills.sav += 20;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_STAVES)
    {
        if (spec)
        {
            if (p_ptr->elaborate_defense)
            {
                p_ptr->to_a += 10 + p_ptr->lev*2/3;
                p_ptr->dis_to_a += 10 + p_ptr->lev*2/3;
            }

            if (p_ptr->lev >= 10)
                p_ptr->sh_retaliation = TRUE;

            if (p_ptr->lev >= 20)
                p_ptr->pspeed += 2;

            if (p_ptr->cloak_of_shadows && !p_ptr->elaborate_defense)
            {
                p_ptr->to_a += 10 + p_ptr->lev*2/3;
                p_ptr->dis_to_a += 10 + p_ptr->lev*2/3;
            }

            if (p_ptr->lev >= 35)
                p_ptr->lightning_reflexes = TRUE;
        }

        if (equip_find_first(object_is_shield))
            p_ptr->pspeed -= 5;
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DIGGERS)
    {
        if (spec)
        {
            int pval = _max_pval();

            p_ptr->skill_dig += (5 + p_ptr->lev/5) * 20;

            if (p_ptr->lev >= 45)
                p_ptr->kill_wall = TRUE;

            if (p_ptr->lev >= 15) /* Earthen Shield */
            {
                int dir, x, y;
                int count = 0;
                int mult = 2 + p_ptr->lev/10;

                for (dir = 0; dir < 8; dir++)
                {
                    y = py + ddy_ddd[dir];
                    x = px + ddx_ddd[dir];

                    if (!in_bounds(y, x)) continue;

                    if ( cave_have_flag_bold(y, x, FF_WALL)
                      || cave[y][x].feat == feat_rubble )
                    {
                        count++;
                    }
                }
                p_ptr->to_a += mult*count;
                p_ptr->dis_to_a += mult*count;
            }
            switch (_get_toggle())
            {
            case TOGGLE_STOICISM:
                p_ptr->skills.stl += pval;
                break;
            case TOGGLE_INDUSTRIOUS_MORTICIAN:
                p_ptr->pspeed += pval;
                break;
            }
        }
    }

    if (!p_ptr->painted_target)
    {
        p_ptr->painted_target_idx = 0;
        p_ptr->painted_target_ct = 0;
    }

    /* Hack -- handle "xtra" mode 
       This works around a startup glitch where the screen is only half painted.
       If we do a msg_print at this point, the user gets a face full of yuk!
    */
    if (character_xtra) return;

    /* Message about favored gear */
    if (!init || spec != last_spec)
    {
        int kind = _specialities[p_ptr->psubclass].kind;
        if (!spec)
        {
            switch (kind)
            {
            case _WEAPONMASTER_BOWS:
                msg_print("You do not feel comfortable with your shooter.");
                break;
            case _WEAPONMASTER_SHIELDS:
                msg_print("You do not feel comfortable with your shield.");
                break;
            default:
                msg_print("You do not feel comfortable with your weapon.");
                break;
            }
        }
        else if (init)
        {
            switch (kind)
            {
            case _WEAPONMASTER_BOWS:
                msg_print("You love your shooter.");
                break;
            case _WEAPONMASTER_SHIELDS:
                msg_print("You love your shield.");
                break;
            default:
                msg_print("You love your weapon.");
                break;
            } 
        }

        init = TRUE;
        last_spec = spec;
    }
}

static void _calc_stats(s16b stats[MAX_STATS])
{
    /* Note: _calc_stats() gets called before _calc_bonuses, so p_ptr->speciality_equip
       won't be set yet. I suppose we could take over setting this field, but I don't like
       relying on the non-obvious ordering of callbacks */
    if (p_ptr->psubclass == WEAPONMASTER_DAGGERS)
    {
        if (_check_speciality_equip())
        {
            switch (_get_toggle())
            {
            case TOGGLE_FLYING_DAGGER_STANCE:
                stats[A_CON] -= 4;
                break;
            }
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DIGGERS)
    {
        if (_check_speciality_equip())
        {
            int pval = _max_pval();

            switch (_get_toggle())
            {
            case TOGGLE_STRENGTH_OF_THE_UNDERTAKER:
                stats[A_STR] += pval;
                break;
            case TOGGLE_STOICISM:
                stats[A_CON] += pval;
                break;
            }
        }
    }
}

static void _get_flags(u32b flgs[TR_FLAG_SIZE])
{
    if (p_ptr->psubclass == WEAPONMASTER_DAGGERS)
    {
        if (p_ptr->speciality_equip)
        {
            if (p_ptr->lev >= 10) add_flag(flgs, TR_STEALTH);
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SWORDS)
    {
        if (p_ptr->speciality_equip)
        {
            if (p_ptr->lev >= 45) add_flag(flgs, TR_VORPAL);
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SHIELDS)
    {
        if (p_ptr->speciality_equip)
        {
            if (p_ptr->lev >= 45)
            {
                add_flag(flgs, TR_RES_ACID);
                add_flag(flgs, TR_RES_COLD);
                add_flag(flgs, TR_RES_FIRE);
                add_flag(flgs, TR_RES_ELEC);
                add_flag(flgs, TR_REFLECT);
            }
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_STAVES)
    {
        if (p_ptr->speciality_equip)
        {
            if (p_ptr->lev >= 10) add_flag(flgs, TR_SH_REVENGE);
            if (p_ptr->lev >= 20) add_flag(flgs, TR_SPEED);
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DIGGERS)
    {
        if (p_ptr->speciality_equip)
        {
            switch (_get_toggle())
            {
            case TOGGLE_STOICISM:
                add_flag(flgs, TR_STEALTH);
                break;
            case TOGGLE_INDUSTRIOUS_MORTICIAN:
                add_flag(flgs, TR_SPEED);
                break;
            }
        }
    }
}

static void _calc_shooter_bonuses(object_type *o_ptr, shooter_info_t *info_ptr)
{
    int spec = _check_speciality_aux(o_ptr);

    if (spec && !p_ptr->shooter_info.heavy_shoot)
    {
        p_ptr->shooter_info.num_fire += p_ptr->lev * 150 / 50;
        if (p_ptr->psubclass == WEAPONMASTER_SLINGS && p_ptr->lev >= 40)
            p_ptr->shooter_info.num_fire += 100;

        p_ptr->shooter_info.to_d += p_ptr->lev/5;
        p_ptr->shooter_info.dis_to_d += p_ptr->lev/5;
        p_ptr->shooter_info.to_h += p_ptr->lev/5;
        p_ptr->shooter_info.dis_to_h += p_ptr->lev/5;

        if (p_ptr->psubclass == WEAPONMASTER_CROSSBOWS)
        {
            switch (_get_toggle())
            {
            case TOGGLE_RAPID_RELOAD:
                p_ptr->shooter_info.to_h -= 10;
                p_ptr->shooter_info.dis_to_h -= 10;
                p_ptr->shooter_info.num_fire += p_ptr->shooter_info.num_fire * (10 + p_ptr->lev/3) / 100;
                break;
            case TOGGLE_EXPLODING_BOLT:
                p_ptr->shooter_info.num_fire /= 2;
                break;
            case TOGGLE_OVERDRAW:
                p_ptr->shooter_info.to_mult += 1;
                p_ptr->shooter_info.to_h -= 20;
                p_ptr->shooter_info.dis_to_h -= 20;
                break;
            }
        }
    }
}

static void _calc_weapon_bonuses(object_type *o_ptr, weapon_info_t *info_ptr)
{
    int spec1 = _check_speciality_aux(o_ptr);

    /* (+L/5, +L/5) for all weaponmasters replacing 'Signature Weapon' */
    if (spec1 && p_ptr->speciality_equip)
    {
        info_ptr->to_d += p_ptr->lev/5;
        info_ptr->dis_to_d += p_ptr->lev/5;
        info_ptr->to_h += p_ptr->lev/5;
        info_ptr->dis_to_h += p_ptr->lev/5;
    }

    if (p_ptr->psubclass == WEAPONMASTER_AXES)
    {
        if (spec1 && p_ptr->speciality_equip)
        {
            info_ptr->to_d += 5;
            info_ptr->dis_to_d += 5;

            if (p_ptr->lev >= 20)
            {
                info_ptr->to_d += 5;
                info_ptr->dis_to_d += 5;
            }

            if (p_ptr->lev >= 45)
            {
                info_ptr->to_d += 10;
                info_ptr->dis_to_d += 10;
            }

            switch (_get_toggle())
            {
            case TOGGLE_POWER_ATTACK:
                info_ptr->dis_to_h -= 2*p_ptr->lev/3;
                info_ptr->to_h -= 2*p_ptr->lev/3;
                if (info_ptr->wield_how == WIELD_TWO_HANDS && !info_ptr->omoi)
                {
                    info_ptr->dis_to_d += p_ptr->lev/2;
                    info_ptr->to_d += p_ptr->lev/2;
                }
                else
                {
                    info_ptr->dis_to_d += p_ptr->lev/4;
                    info_ptr->to_d += p_ptr->lev/4;
                }
                break;
            }
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DAGGERS)
    {
        switch (_get_toggle())
        {
        case TOGGLE_SHADOW_STANCE:
            info_ptr->to_d -= 10;
            info_ptr->dis_to_d -= 10;
            break;
        case TOGGLE_FRENZY_STANCE:
            info_ptr->to_h += p_ptr->lev/5;
            info_ptr->dis_to_h += p_ptr->lev/5;
            break;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_CLUBS)
    {
        if (p_ptr->speciality_equip)
        {
            if (p_ptr->lev >= 25)
            {
                info_ptr->to_h += 20;
                info_ptr->dis_to_h += 20;
            }
        }

        switch (_get_toggle())
        {
        case TOGGLE_COMBAT_EXPERTISE:
            info_ptr->to_h -= 5 + p_ptr->lev/2;
            info_ptr->dis_to_h -= 5 + p_ptr->lev/2;
            break;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SHIELDS)
    {
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SWORDS)
    {
        if (spec1 && p_ptr->speciality_equip)
        {
            info_ptr->to_h += 10;
            info_ptr->dis_to_h += 10;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_POLEARMS)
    {
        if (p_ptr->entrenched)
        {
            info_ptr->to_h += 20 + p_ptr->entrench_ct;
            info_ptr->dis_to_h += 20 + p_ptr->entrench_ct;
        }

        switch (_get_toggle())
        {
        case TOGGLE_MANY_STRIKE:
            info_ptr->to_h -= 5;
            info_ptr->dis_to_h -= 5;
            break;
        case TOGGLE_TRIP:
            info_ptr->to_h -= 30;
            info_ptr->dis_to_h -= 30;
            break;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_STAVES)
    {
        if (spec1 && p_ptr->speciality_equip && p_ptr->lev >= 30 && p_ptr->chp == p_ptr->mhp)
            info_ptr->xtra_blow += 100;
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DIGGERS)
    {
        if (spec1 && p_ptr->speciality_equip)
        {
            switch (_get_toggle())
            {
            case TOGGLE_INDUSTRIOUS_MORTICIAN:
                info_ptr->xtra_blow += 100;
                /*info_ptr->xtra_blow += MIN(o_ptr->pval*50, 250);*/
                break;
            }
        }
    }
}

static void _move_monster(int m_idx)
{
    if (p_ptr->psubclass == WEAPONMASTER_POLEARMS)
    {
        if (p_ptr->lev >= 20 && p_ptr->speciality_equip)
        {
            monster_type *m_ptr = &m_list[m_idx];
            if (m_ptr->cdis == 1)
            {
                char m_name[80];
                monster_desc(m_name, m_ptr, 0);
                msg_format("%^s gets too close!", m_name);
                py_attack(m_ptr->fy, m_ptr->fx, WEAPONMASTER_PROXIMITY_ALERT);
            }
        }
    }
}

static void _process_player(void)
{
    if (p_ptr->psubclass == WEAPONMASTER_POLEARMS)
    {
        /* process_player() fires before move_player() */
        if (px == p_ptr->entrench_x && py == p_ptr->entrench_y)
        {
            if (p_ptr->entrench_ct < 30)
                p_ptr->entrench_ct++;
            p_ptr->update |= PU_BONUS;
            p_ptr->redraw |= PR_STATUS;
        }
        else
        {
            p_ptr->entrench_x = px;
            p_ptr->entrench_y = py;
            p_ptr->entrench_ct = 0;
            p_ptr->update |= PU_BONUS;
            p_ptr->redraw |= PR_STATUS;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_STAVES)
    {
        if (p_ptr->elaborate_defense)
        {
            p_ptr->elaborate_defense--;
            if (p_ptr->elaborate_defense <= 0)
            {
                p_ptr->update |= PU_BONUS;
                p_ptr->redraw |= PR_ARMOR;
            }
        }
        if (p_ptr->cloak_of_shadows)
        {
            p_ptr->cloak_of_shadows--;
            if (p_ptr->cloak_of_shadows <= 0)
            {
                p_ptr->update |= PU_BONUS;
                p_ptr->redraw |= PR_ARMOR;
            }
        }
    }
}

static void _move_player(void)
{
    if (_get_toggle() == TOGGLE_SHOT_ON_THE_RUN)
    {
        int idx = -1;
        int num_shots = 1 + p_ptr->shooter_info.num_fire / 400;
        int i;

        /* Paranoia:  Did the player remove their sling? */
        if (!_check_speciality_equip())
        {
            _set_toggle(TOGGLE_NONE);
            return;
        }

        for (i = 0; i < num_shots; i++)
        {
            /* End the technique when the ammo runs out.  Note that "return ammo"
               might not consume the current shot. Note that we will intentionally spill
               over into the next stack of shots, provided they are legal for this shooter. */
            if (shoot_item != INVEN_UNLIMITED_QUIVER)
            {
                if (inventory[shoot_item].tval != p_ptr->shooter_info.tval_ammo)
                {
                    /* Ugh, with this technique, the ammo slot is constantly moving thanks
                       to pseudo-id/autodestroyer or unstacking and stacking of staves.
                       Just try to find usable ammo should this occur */
                    shoot_item = _find_ammo_slot();
                    if (shoot_item < 0)
                    {
                        msg_print("Your ammo has run out. Time to reload!");
                        _set_toggle(TOGGLE_NONE);
                        return;
                    }
                }
            }

            /* Pick a target to blast */
            idx = _get_nearest_target_los();
            if (idx > 0)
            {
                int tx, ty;
                object_type *bow = equip_obj(p_ptr->shooter_info.slot);

                if (bow)
                {
                    tx = m_list[idx].fx;
                    ty = m_list[idx].fy;
                    shoot_hack = SHOOT_RUN;
                    do_cmd_fire_aux2(shoot_item, bow, px, py, tx, ty);
                    shoot_hack = SHOOT_NONE;
                }
            }
        }
    }
    if (p_ptr->psubclass == WEAPONMASTER_POLEARMS)
    {
        int y, x;
        if (one_in_(5) && random_opponent(&y, &x))
        {
            py_attack(y, x, WEAPONMASTER_AUTO_BLOW);
            energy_use = 0;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_STAVES)
    {
        if (p_ptr->speciality_equip && p_ptr->lev >= 5)
        {
            if (!p_ptr->cloak_of_shadows)
            {
                p_ptr->update |= PU_BONUS;
                p_ptr->redraw |= PR_ARMOR;
            }
            p_ptr->cloak_of_shadows = 1;
        }
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DIGGERS)
    {
        if (p_ptr->speciality_equip && p_ptr->lev >= 15)
            p_ptr->update |= PU_BONUS;
    }
}

static void _character_dump(doc_ptr doc)
{
    doc_printf(doc, "<topic:Abilities>================================== <color:keypress>A</color>bilities ==================================\n\n");
    
    if (p_ptr->psubclass == WEAPONMASTER_AXES)
    {
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * You gain a huge bonus to damage when wielding an axe.\n");
        else if (p_ptr->lev >= 20)
            doc_printf(doc, "  * You gain a large bonus to damage when wielding an axe.\n");
        else
            doc_printf(doc, "  * You gain a small bonus to damage when wielding an axe.\n");
        
        doc_printf(doc, "  * You gain +1 max attack when wielding an axe with two hands.\n");

        if (p_ptr->lev >= 5)
            doc_printf(doc, "  * You gain a bonus to tunneling when wielding an axe.\n");

        if (p_ptr->lev >= 30)
            doc_printf(doc, "  * <indent>You occasionally attack an adjacent opponent after killing a foe when wielding an axe.</indent>\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_CLUBS)
    {
        doc_printf(doc, "  * Your attacks have a chance to confuse when wielding a club.\n");
        
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * Your attacks have a chance to knock out when wielding a club.\n");
        
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * Your attacks have a chance to stun when wielding a club.\n");

        if (p_ptr->lev >= 25)
            doc_printf(doc, "  * You gain a bonus to hit when wielding a club.\n");

        if (p_ptr->lev >= 40)
            doc_printf(doc, "  * You gain crushing blows when wielding a club.\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DAGGERS)
    {
        doc_printf(doc, "  * You pay reduced energy costs when equipping a dagger.\n");
        doc_printf(doc, "  * You dual wield very effectively with daggers.\n");
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * You gain a bonus to AC when wielding a dagger.\n");
        if (p_ptr->lev >= 12)
            doc_printf(doc, "  * You gain a bonus to stealth when wielding a dagger.\n");
        if (p_ptr->lev >= 30)
            doc_printf(doc, "  * You gain sneak attack and backstab when wielding a dagger.\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_DIGGERS)
    {
        doc_printf(doc, "  * You gain a bonus to tunneling when wielding a digger.\n");
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * Your steps break walls when wielding a digger.\n");

        if (p_ptr->lev >= 15)
            doc_printf(doc, "  * <indent>You gain an AC bonus depending on the number of adjacent walls when wielding a digger.</indent>\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_POLEARMS)
    {
        doc_printf(doc, "  * You occasionally get a free round of attacks after moving when wielding a polearm.\n");
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * You automatically attack any enemy that steps next to you when wielding a polearm.\n");
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * You occasionally strike all adjacent foes when wielding a polearm.\n");

        if (p_ptr->lev >= 30)
            doc_printf(doc, "  * <indent>You gain a bonus to hit and to AC when you don't move for 3 rounds when wielding a polearm.</indent>\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SLINGS)
    {
        doc_printf(doc, "  * Your ammo often returns to you when wielding a sling.\n");
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * Your ammo gains extra damage dice when wielding a sling.\n");
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * You have access to an unlimited quiver when wielding a sling.\n");

        if (p_ptr->lev >= 10)
            doc_printf(doc, "  * <indent>Your shots never miss your target once you score 3 consecutive hits when wielding a sling.</indent>\n");
        if (p_ptr->lev >= 40)
            doc_printf(doc, "  * You gain extra shots when wielding a sling.\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SHIELDS)
    {
        doc_printf(doc, "  * You gain two handed wielding bonuses even when wielding a shield.\n");
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * <indent>Your inventory items are somewhat protected from destruction when wielding a shield.</indent>\n");
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * You gain basic resistance and reflection when wielding a shield.\n");

        if (p_ptr->lev >= 5)
            doc_printf(doc, "  * You gain double the AC benefit when wielding a shield.\n");

        if (p_ptr->lev >= 25)
            doc_printf(doc, "  * You gain a bonus to saving throws when wielding a shield.\n");
    
    }
    else if (p_ptr->psubclass == WEAPONMASTER_STAVES)
    {
        doc_printf(doc, "  * <indent>You gain a bonus to AC until your next turn after any successful hit when wielding a staff.</indent>\n");
        doc_printf(doc, "  * You suffer a penalty to speed when wielding a shield.\n");
        if (p_ptr->lev >= 5)
            doc_printf(doc, "  * You gain a bonus AC after moving until your next turn when wielding a staff.\n");
        if (p_ptr->lev >= 10)
            doc_printf(doc, "  * You retaliate when struck when wielding a staff.\n");
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * You gain a bonus to speed when wielding a staff.\n");
        if (p_ptr->lev >= 30)
            doc_printf(doc, "  * You gain an extra attack when you are at full health and wielding a staff.\n");
        if (p_ptr->lev >= 35)
            doc_printf(doc, "  * You are unaffected by monster auras when wielding a staff.\n");
    }
    else if (p_ptr->psubclass == WEAPONMASTER_SWORDS)
    {
        doc_printf(doc, "  * You gain a bonus to hit when wielding a sword.\n");
        if (p_ptr->lev >= 20)
            doc_printf(doc, "  * You gain constant heroism when wielding a sword.\n");
        if (p_ptr->lev >= 45)
            doc_printf(doc, "  * You gain vorpal attacks when wielding a sword.\n");
    }

    doc_newline(doc);

    {
        spell_info spells[MAX_SPELLS];
        int        ct = _get_spells_aux(spells, MAX_SPELLS);

        if (ct)
            py_display_spells(doc, spells, ct);
    }
}

class_t *weaponmaster_get_class(int subclass)
{
    static class_t me = {0};
    static bool    init = FALSE;

    /* static info never changes */
    if (!init)
    {
        me.name = "Weaponmaster";
        me.desc = "The weaponmaster is great with a single class of weapons. "
                  "The character gets combat bonuses and special powers "
                  "depending on the type of specialization. Alas, the "
                  "weaponmaster is truly lousy when using any weapon "
                  "outside their chosen specialty so focus is key.";

        me.life = 109;
        me.base_hp = 12;
        me.exp = 135;
        me.pets = 40;
        
        me.caster_info = _caster_info;
        me.get_spells = _get_spells;
        me.birth = _on_birth;
        me.calc_bonuses = _calc_bonuses;
        me.calc_stats = _calc_stats;
        me.get_flags = _get_flags;
        me.calc_weapon_bonuses = _calc_weapon_bonuses;
        me.calc_shooter_bonuses = _calc_shooter_bonuses;
        me.move_player = _move_player;
        me.move_monster = _move_monster;
        me.process_player = _process_player;
        me.character_dump = _character_dump;
        init = TRUE;
    }
    {
        me.stats[A_STR] =  1;
        me.stats[A_INT] = -1;
        me.stats[A_WIS] = -1;
        me.stats[A_DEX] =  1;
        me.stats[A_CON] =  1;
        me.stats[A_CHR] =  1;
    }
    if (0 <= subclass && subclass < WEAPONMASTER_MAX)
    {
        _speciality *ptr = &_specialities[subclass];
        int          i;

        for (i = 0; i < MAX_STATS; i++)
            me.stats[i] += ptr->stats[i];

        me.base_skills = ptr->base_skills;
        me.extra_skills = ptr->extra_skills;

        me.subname = ptr->name;
        me.subdesc = ptr->help;
    }
    else
    {
        me.subname = "";
        me.subdesc = "";
    }

    return &me;
}

int weaponmaster_wield_hack(object_type *o_ptr)
{
    int result = 100;

    if (p_ptr->pclass == CLASS_WEAPONMASTER)
    {
        if (p_ptr->psubclass == WEAPONMASTER_DAGGERS)
        {
            if (_check_speciality_aux(o_ptr)) 
                result = 50 - p_ptr->lev/2;
        }
    }
    return result;
}

cptr weaponmaster_speciality_name(int psubclass)
{
    /* assert(p_ptr->pclass == CLASS_WEAPONMASTER); */
    return _specialities[psubclass].name;
}

void weaponmaster_do_readied_shot(monster_type *m_ptr)
{
    if (weaponmaster_get_toggle() == TOGGLE_READIED_SHOT)
    {
        int tx = m_ptr->fx;
        int ty = m_ptr->fy;
        object_type *bow = equip_obj(p_ptr->shooter_info.slot);

        if (bow)
        {
            shoot_hack = SHOOT_RETALIATE;
            do_cmd_fire_aux2(shoot_item, bow, px, py, tx, ty);
            shoot_hack = SHOOT_NONE;
        }
        /* Force the player to ready another arrow ... */
        _set_toggle(TOGGLE_NONE);
    }
}
