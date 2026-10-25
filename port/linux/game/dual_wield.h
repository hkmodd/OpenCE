#ifndef DUAL_WIELD_H
#define DUAL_WIELD_H
#include "cseries.h"
#include "math/real_math.h"
/* a weapon in the other hand (dual_wield.c): a headset's player, by a VR
server's rules, holds a second weapon of their unit's in their other hand,
readied, which fires with that hand's trigger */

/* the weapon of the unit's inventory slot taken into its other hand (not
its weapon in hand; FALSE: none taken), and put back */
boolean dual_wield_begin(long unit_index, short slot);
void dual_wield_end(long unit_index);
/* the unit's weapon in its other hand, and its slot (NONE: none) */
long dual_wield_weapon(long unit_index);
short dual_wield_slot(long unit_index);
/* the other hand's trigger (0 to 1), as the unit's weapon takes its own */
void dual_wield_set_trigger(long unit_index, real trigger);

/* units.c's: once a tick, after the unit's weapon in hand is told its
controls; as a weapon is readied in its hand (the other hand's put back
first, if it is that one); as its inventory is dropped or deleted (put
back first) */
void dual_wield_unit_update(long unit_index);
void dual_wield_unit_readying(long unit_index);
void dual_wield_unit_dropping(long unit_index);
/* ... as a weapon of its inventory is dropped (put back first, if it is
that one) */
void dual_wield_unit_releasing(long unit_index, short slot);
/* weapons.c's: a shot of a weapon in an other hand, from that hand's
weapon along where it points (FALSE: not such a weapon's) */
boolean dual_wield_projectile_ray(long weapon_index, real_point3d *origin, real_vector3d *forward);

/* the drawing (render_interpolation.c, render_objects.c): any weapon is in
an other hand; the one the first-person unit holds there, drawn though the
unit is not (NONE: none); its node matrices as drawn, at the hand */
boolean dual_wield_any(void);
long dual_wield_first_person_weapon(long unit_index);
real_matrix4x3 *dual_wield_node_matrices(long object_index, real_matrix4x3 *nodes, long frame);
#endif
