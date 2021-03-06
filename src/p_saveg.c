/*
========================================================================

                           D O O M  R e t r o
         The classic, refined DOOM source port. For Windows PC.

========================================================================

  Copyright © 1993-2012 id Software LLC, a ZeniMax Media company.
  Copyright © 2013-2017 Brad Harding.

  DOOM Retro is a fork of Chocolate DOOM.
  For a list of credits, see <http://wiki.doomretro.com/credits>.

  This file is part of DOOM Retro.

  DOOM Retro is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  DOOM Retro is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with DOOM Retro. If not, see <https://www.gnu.org/licenses/>.

  DOOM is a registered trademark of id Software LLC, a ZeniMax Media
  company, in the US and/or other countries and is used without
  permission. All other trademarks are the property of their respective
  holders. DOOM Retro is in no way affiliated with nor endorsed by
  id Software.

========================================================================
*/

#include "am_map.h"
#include "c_console.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_misc.h"
#include "p_local.h"
#include "p_saveg.h"
#include "p_tick.h"
#include "version.h"
#include "z_zone.h"

#define SAVEGAME_EOF    0x1D

FILE    *save_stream;
int     savegamelength;

extern dboolean r_textures;
extern dboolean r_translucency;

// Get the filename of a temporary file to write the savegame to. After
// the file has been successfully saved, it will be renamed to the
// real file.
char *P_TempSaveGameFile(void)
{
    static char *filename;

    if (!filename)
        filename = M_StringJoin(savegamefolder, "temp.save", NULL);

    return filename;
}

// Get the filename of the save game file to use for the specified slot.
char *P_SaveGameFile(int slot)
{
    static char         *filename;
    static size_t       filename_size;
    char                basename[32];

    if (!filename)
    {
        filename_size = strlen(savegamefolder) + 32;
        filename = malloc(filename_size);
    }

    M_snprintf(basename, 32, PACKAGE_SAVE, slot);
    M_snprintf(filename, filename_size, "%s%s", savegamefolder, basename);

    return filename;
}

// Endian-safe integer read/write functions
static byte saveg_read8(void)
{
    byte        result = -1;

    fread(&result, 1, 1, save_stream);

    return result;
}

static void saveg_write8(byte value)
{
    fwrite(&value, 1, 1, save_stream);
}

static short saveg_read16(void)
{
    int result;

    result = saveg_read8();
    result |= saveg_read8() << 8;

    return result;
}

static void saveg_write16(short value)
{
    saveg_write8(value & 0xFF);
    saveg_write8((value >> 8) & 0xFF);
}

static int saveg_read32(void)
{
    int result;

    result = saveg_read8();
    result |= saveg_read8() << 8;
    result |= saveg_read8() << 16;
    result |= saveg_read8() << 24;

    return result;
}

static void saveg_write32(int value)
{
    saveg_write8(value & 0xFF);
    saveg_write8((value >> 8) & 0xFF);
    saveg_write8((value >> 16) & 0xFF);
    saveg_write8((value >> 24) & 0xFF);
}

// Pad to 4-byte boundaries
static void saveg_read_pad(void)
{
    unsigned long       pos = ftell(save_stream);
    int                 padding = (4 - (pos & 3)) & 3;
    int                 i;

    for (i = 0; i < padding; i++)
        saveg_read8();
}

static void saveg_write_pad(void)
{
    unsigned long       pos = ftell(save_stream);
    int                 padding = (4 - (pos & 3)) & 3;
    int                 i;

    for (i = 0; i < padding; i++)
        saveg_write8(0);
}

// Pointers
static void *saveg_readp(void)
{
    return (void *)(intptr_t)saveg_read32();
}

static void saveg_writep(void *p)
{
    saveg_write32((intptr_t)p);
}

// Enum values are 32-bit integers.
#define saveg_read_enum         saveg_read32
#define saveg_write_enum        saveg_write32

//
// Structure read/write functions
//

//
// mapthing_t
//
static void saveg_read_mapthing_t(mapthing_t *str)
{
    str->x = saveg_read16();
    str->y = saveg_read16();
    str->angle = saveg_read16();
    str->type = saveg_read16();
    str->options = saveg_read16();
}

static void saveg_write_mapthing_t(mapthing_t *str)
{
    saveg_write16(str->x);
    saveg_write16(str->y);
    saveg_write16(str->angle);
    saveg_write16(str->type);
    saveg_write16(str->options);
}

//
// mobj_t
//
static void saveg_read_mobj_t(mobj_t *str)
{
    int pl;

    str->x = saveg_read32();
    str->y = saveg_read32();
    str->z = saveg_read32();
    str->snext = (mobj_t *)saveg_readp();
    str->sprev = (mobj_t **)saveg_readp();
    str->angle = saveg_read32();
    str->sprite = (spritenum_t)saveg_read_enum();
    str->frame = saveg_read32();
    str->bnext = (mobj_t *)saveg_readp();
    str->bprev = (mobj_t **)saveg_readp();
    str->subsector = (subsector_t *)saveg_readp();
    str->floorz = saveg_read32();
    str->ceilingz = saveg_read32();
    str->dropoffz = saveg_read32();
    str->radius = saveg_read32();
    str->height = saveg_read32();
    str->projectilepassheight = saveg_read32();
    str->momx = saveg_read32();
    str->momy = saveg_read32();
    str->momz = saveg_read32();
    str->type = (mobjtype_t)saveg_read_enum();
    str->info = (mobjinfo_t *)saveg_readp();
    str->tics = saveg_read32();
    str->state = &states[saveg_read32()];
    str->flags = saveg_read32();
    str->flags2 = saveg_read32();
    str->health = saveg_read32();
    str->movedir = saveg_read32();
    str->movecount = saveg_read32();
    str->target = (mobj_t *)saveg_readp();
    str->reactiontime = saveg_read32();
    str->threshold = saveg_read32();

    if ((pl = saveg_read32()) > 0)
    {
        str->player = &players[pl - 1];
        str->player->mo = str;
    }
    else
        str->player = NULL;

    saveg_read_mapthing_t(&str->spawnpoint);
    str->tracer = (mobj_t *)saveg_readp();
    str->lastenemy = (mobj_t *)saveg_readp();
    str->floatbob = saveg_read32();
    str->touching_sectorlist = NULL;
    str->gear = saveg_read16();
    str->bloodsplats = saveg_read32();
    str->blood = saveg_read32();
    str->interp = saveg_read32();
    str->oldx = saveg_read32();
    str->oldy = saveg_read32();
    str->oldz = saveg_read32();
    str->oldangle = saveg_read32();
    str->pitch = saveg_read32();
    str->id = saveg_read32();
}

static void saveg_write_mobj_t(mobj_t *str)
{
    saveg_write32(str->x);
    saveg_write32(str->y);
    saveg_write32(str->z);
    saveg_writep(str->snext);
    saveg_writep(str->sprev);
    saveg_write32(str->angle);
    saveg_write_enum(str->sprite);
    saveg_write32(str->frame);
    saveg_writep(str->bnext);
    saveg_writep(str->bprev);
    saveg_writep(str->subsector);
    saveg_write32(str->floorz);
    saveg_write32(str->ceilingz);
    saveg_write32(str->dropoffz);
    saveg_write32(str->radius);
    saveg_write32(str->height);
    saveg_write32(str->projectilepassheight);
    saveg_write32(str->momx);
    saveg_write32(str->momy);
    saveg_write32(str->momz);
    saveg_write_enum(str->type);
    saveg_writep(str->info);
    saveg_write32(str->tics);
    saveg_write32(str->state - states);
    saveg_write32(str->flags);
    saveg_write32(str->flags2);
    saveg_write32(str->health);
    saveg_write32(str->movedir);
    saveg_write32(str->movecount);
    saveg_writep((void *)(uintptr_t)P_ThinkerToIndex((thinker_t *)str->target));
    saveg_write32(str->reactiontime);
    saveg_write32(str->threshold);
    saveg_write32(str->player ? str->player - players + 1 : 0);
    saveg_write_mapthing_t(&str->spawnpoint);
    saveg_writep((void *)(uintptr_t)P_ThinkerToIndex((thinker_t *)str->tracer));
    saveg_writep((void *)(uintptr_t)P_ThinkerToIndex((thinker_t *)str->lastenemy));
    saveg_write32(str->floatbob);
    saveg_write16(str->gear);
    saveg_write32(str->bloodsplats);
    saveg_write32(str->blood);
    saveg_write32(str->interp);
    saveg_write32(str->oldx);
    saveg_write32(str->oldy);
    saveg_write32(str->oldz);
    saveg_write32(str->oldangle);
    saveg_write32(str->pitch);
    saveg_write32(str->id);
}

//
// bloodsplat_t
//
static void saveg_read_bloodsplat_t(bloodsplat_t *str)
{
    str->x = saveg_read32();
    str->y = saveg_read32();
    str->frame = saveg_read32();
    str->flags = saveg_read32();
    str->blood = saveg_read32();
}

static void saveg_write_bloodsplat_t(bloodsplat_t *str)
{
    saveg_write32(str->x);
    saveg_write32(str->y);
    saveg_write32(str->frame);
    saveg_write32(str->flags);
    saveg_write32(str->blood);
}

//
// ticcmd_t
//
static void saveg_read_ticcmd_t(ticcmd_t *str)
{
    str->forwardmove = saveg_read8();
    str->sidemove = saveg_read8();
    str->angleturn = saveg_read16();
    str->buttons = saveg_read8();
}

static void saveg_write_ticcmd_t(ticcmd_t *str)
{
    saveg_write8(str->forwardmove);
    saveg_write8(str->sidemove);
    saveg_write16(str->angleturn);
    saveg_write8(str->buttons);
}

//
// pspdef_t
//
static void saveg_read_pspdef_t(pspdef_t *str)
{
    int state = saveg_read32();

    str->state = (state > 0 ? &states[state] : NULL);
    str->tics = saveg_read32();
    str->sx = saveg_read32();
    str->sy = saveg_read32();
}

static void saveg_write_pspdef_t(pspdef_t *str)
{
    saveg_write32(str->state ? str->state - states : 0);
    saveg_write32(str->tics);
    saveg_write32(str->sx);
    saveg_write32(str->sy);
}

extern int oldhealth;
extern int cardsfound;

//
// player_t
//
static void saveg_read_player_t(player_t *str)
{
    int i;

    str->mo = (mobj_t *)saveg_readp();
    str->playerstate = (playerstate_t)saveg_read_enum();
    saveg_read_ticcmd_t(&str->cmd);
    str->viewz = saveg_read32();
    str->viewheight = saveg_read32();
    str->deltaviewheight = saveg_read32();
    str->bob = saveg_read32();
    str->momx = saveg_read32();
    str->momy = saveg_read32();
    str->health = saveg_read32();
    oldhealth = saveg_read32();
    str->armorpoints = saveg_read32();
    str->armortype = (armortype_t)saveg_read_enum();

    for (i = 0; i < NUMPOWERS; i++)
        str->powers[i] = saveg_read32();

    for (i = 0; i < NUMCARDS; i++)
    {
        str->cards[i] = saveg_read32();
        cardsfound = MAX(cardsfound, str->cards[i]);
    }

    str->neededcard = saveg_read32();
    str->neededcardflash = saveg_read32();
    str->backpack = saveg_read32();
    str->readyweapon = (weapontype_t)saveg_read_enum();
    str->pendingweapon = (weapontype_t)saveg_read_enum();

    for (i = 0; i < NUMWEAPONS; i++)
        str->weaponowned[i] = saveg_read32();

    str->shotguns = (str->weaponowned[wp_shotgun] || str->weaponowned[wp_supershotgun]);

    for (i = 0; i < NUMAMMO; i++)
        str->ammo[i] = saveg_read32();

    for (i = 0; i < NUMAMMO; i++)
        str->maxammo[i] = saveg_read32();

    str->attackdown = saveg_read32();
    str->usedown = saveg_read32();
    str->cheats = saveg_read32();
    str->refire = saveg_read32();
    str->killcount = saveg_read32();
    str->itemcount = saveg_read32();
    str->secretcount = saveg_read32();
    str->message = (char *)saveg_readp();
    str->damagecount = saveg_read32();
    str->bonuscount = saveg_read32();
    str->attacker = (mobj_t *)saveg_readp();
    str->extralight = saveg_read32();
    str->fixedcolormap = saveg_read32();

    for (i = 0; i < NUMPSPRITES; i++)
        saveg_read_pspdef_t(&str->psprites[i]);

    str->didsecret = saveg_read32();
    str->preferredshotgun = (weapontype_t)saveg_read_enum();
    str->shotguns = saveg_read32();
    str->fistorchainsaw = (weapontype_t)saveg_read_enum();
    str->invulnbeforechoppers = saveg_read32();
    str->chainsawbeforechoppers = saveg_read32();
    str->weaponbeforechoppers = (weapontype_t)saveg_read_enum();
    str->oldviewz = saveg_read32();
    str->damageinflicted = saveg_read32();
    str->damagereceived = saveg_read32();
    str->cheated = saveg_read32();
    str->shotshit = saveg_read32();
    str->shotsfired = saveg_read32();
    str->deaths = saveg_read32();

    for (i = 0; i < NUMMOBJTYPES; i++)
        str->mobjcount[i] = saveg_read32();

    str->distancetraveled = saveg_read32();
    str->itemspickedup_ammo_bullets = saveg_read32();
    str->itemspickedup_ammo_cells = saveg_read32();
    str->itemspickedup_ammo_rockets = saveg_read32();
    str->itemspickedup_ammo_shells = saveg_read32();
    str->itemspickedup_armor = saveg_read32();
    str->itemspickedup_health = saveg_read32();
}

static void saveg_write_player_t(player_t *str)
{
    int i;

    saveg_writep(str->mo);
    saveg_write_enum(str->playerstate);
    saveg_write_ticcmd_t(&str->cmd);
    saveg_write32(str->viewz);
    saveg_write32(str->viewheight);
    saveg_write32(str->deltaviewheight);
    saveg_write32(str->bob);
    saveg_write32(str->momx);
    saveg_write32(str->momy);
    saveg_write32(str->health);
    saveg_write32(oldhealth);
    saveg_write32(str->armorpoints);
    saveg_write_enum(str->armortype);

    for (i = 0; i < NUMPOWERS; i++)
        saveg_write32(str->powers[i]);

    for (i = 0; i < NUMCARDS; i++)
        saveg_write32(str->cards[i]);

    saveg_write32(str->neededcard);
    saveg_write32(str->neededcardflash);
    saveg_write32(str->backpack);
    saveg_write_enum(str->readyweapon);
    saveg_write_enum(str->pendingweapon);

    for (i = 0; i < NUMWEAPONS; i++)
        saveg_write32(str->weaponowned[i]);

    for (i = 0; i < NUMAMMO; i++)
        saveg_write32(str->ammo[i]);

    for (i = 0; i < NUMAMMO; i++)
        saveg_write32(str->maxammo[i]);

    saveg_write32(str->attackdown);
    saveg_write32(str->usedown);
    saveg_write32(str->cheats);
    saveg_write32(str->refire);
    saveg_write32(str->killcount);
    saveg_write32(str->itemcount);
    saveg_write32(str->secretcount);
    saveg_writep(str->message);
    saveg_write32(str->damagecount);
    saveg_write32(str->bonuscount);
    saveg_writep(str->attacker);
    saveg_write32(str->extralight);
    saveg_write32(str->fixedcolormap);

    for (i = 0; i < NUMPSPRITES; i++)
        saveg_write_pspdef_t(&str->psprites[i]);

    saveg_write32(str->didsecret);
    saveg_write_enum(str->preferredshotgun);
    saveg_write32(str->shotguns);
    saveg_write32(str->fistorchainsaw);
    saveg_write32(str->invulnbeforechoppers);
    saveg_write32(str->chainsawbeforechoppers);
    saveg_write_enum(str->weaponbeforechoppers);
    saveg_write32(str->oldviewz);
    saveg_write32(str->damageinflicted);
    saveg_write32(str->damagereceived);
    saveg_write32(str->cheated);
    saveg_write32(str->shotshit);
    saveg_write32(str->shotsfired);
    saveg_write32(str->deaths);

    for (i = 0; i < NUMMOBJTYPES; i++)
        saveg_write32(str->mobjcount[i]);

    saveg_write32(str->distancetraveled);
    saveg_write32(str->itemspickedup_ammo_bullets);
    saveg_write32(str->itemspickedup_ammo_cells);
    saveg_write32(str->itemspickedup_ammo_rockets);
    saveg_write32(str->itemspickedup_ammo_shells);
    saveg_write32(str->itemspickedup_armor);
    saveg_write32(str->itemspickedup_health);
}

//
// ceiling_t
//
static void saveg_read_ceiling_t(ceiling_t *str)
{
    str->type = (ceiling_e)saveg_read_enum();
    str->sector = &sectors[saveg_read32()];
    str->bottomheight = saveg_read32();
    str->topheight = saveg_read32();
    str->speed = saveg_read32();
    str->oldspeed = saveg_read32();
    str->crush = saveg_read32();
    str->newspecial = saveg_read32();
    str->texture = saveg_read16();
    str->direction = saveg_read32();
    str->tag = saveg_read32();
    str->olddirection = saveg_read32();
}

static void saveg_write_ceiling_t(ceiling_t *str)
{
    saveg_write_enum(str->type);
    saveg_write32(str->sector - sectors);
    saveg_write32(str->bottomheight);
    saveg_write32(str->topheight);
    saveg_write32(str->speed);
    saveg_write32(str->oldspeed);
    saveg_write32(str->crush);
    saveg_write32(str->newspecial);
    saveg_write16(str->texture);
    saveg_write32(str->direction);
    saveg_write32(str->tag);
    saveg_write32(str->olddirection);
}

//
// vldoor_t
//
static void saveg_read_vldoor_t(vldoor_t *str)
{
    str->type = (vldoor_e)saveg_read_enum();
    str->sector = &sectors[saveg_read32()];
    str->topheight = saveg_read32();
    str->speed = saveg_read32();
    str->direction = saveg_read32();
    str->topwait = saveg_read32();
    str->topcountdown = saveg_read32();
    str->line = &lines[saveg_read32()];
    str->lighttag = saveg_read32();
}

static void saveg_write_vldoor_t(vldoor_t *str)
{
    saveg_write_enum(str->type);
    saveg_write32(str->sector - sectors);
    saveg_write32(str->topheight);
    saveg_write32(str->speed);
    saveg_write32(str->direction);
    saveg_write32(str->topwait);
    saveg_write32(str->topcountdown);
    saveg_write32(str->line - lines);
    saveg_write32(str->lighttag);
}

//
// floormove_t
//
static void saveg_read_floormove_t(floormove_t *str)
{
    str->type = (floor_e)saveg_read_enum();
    str->crush = saveg_read32();
    str->sector = &sectors[saveg_read32()];
    str->direction = saveg_read32();
    str->newspecial = saveg_read32();
    str->texture = saveg_read16();
    str->floordestheight = saveg_read32();
    str->speed = saveg_read32();
    str->stopsound = saveg_read32();
}

static void saveg_write_floormove_t(floormove_t *str)
{
    saveg_write_enum(str->type);
    saveg_write32(str->crush);
    saveg_write32(str->sector - sectors);
    saveg_write32(str->direction);
    saveg_write32(str->newspecial);
    saveg_write16(str->texture);
    saveg_write32(str->floordestheight);
    saveg_write32(str->speed);
    saveg_write32(str->stopsound);
}

//
// plat_t
//
static void saveg_read_plat_t(plat_t *str)
{
    str->thinker.function = (saveg_read32() ? T_PlatRaise : NULL);
    str->sector = &sectors[saveg_read32()];
    str->speed = saveg_read32();
    str->low = saveg_read32();
    str->high = saveg_read32();
    str->wait = saveg_read32();
    str->count = saveg_read32();
    str->status = (plat_e)saveg_read_enum();
    str->oldstatus = (plat_e)saveg_read_enum();
    str->crush = saveg_read32();
    str->tag = saveg_read32();
    str->type = (plattype_e)saveg_read_enum();
}

static void saveg_write_plat_t(plat_t *str)
{
    saveg_write32(!!str->thinker.function);
    saveg_write32(str->sector - sectors);
    saveg_write32(str->speed);
    saveg_write32(str->low);
    saveg_write32(str->high);
    saveg_write32(str->wait);
    saveg_write32(str->count);
    saveg_write_enum(str->status);
    saveg_write_enum(str->oldstatus);
    saveg_write32(str->crush);
    saveg_write32(str->tag);
    saveg_write_enum(str->type);
}

//
// lightflash_t
//
static void saveg_read_lightflash_t(lightflash_t *str)
{
    str->sector = &sectors[saveg_read32()];
    str->count = saveg_read32();
    str->maxlight = saveg_read32();
    str->minlight = saveg_read32();
    str->maxtime = saveg_read32();
    str->mintime = saveg_read32();
}

static void saveg_write_lightflash_t(lightflash_t *str)
{
    saveg_write32(str->sector - sectors);
    saveg_write32(str->count);
    saveg_write32(str->maxlight);
    saveg_write32(str->minlight);
    saveg_write32(str->maxtime);
    saveg_write32(str->mintime);
}

//
// strobe_t
//
static void saveg_read_strobe_t(strobe_t *str)
{
    str->sector = &sectors[saveg_read32()];
    str->count = saveg_read32();
    str->minlight = saveg_read32();
    str->maxlight = saveg_read32();
    str->darktime = saveg_read32();
    str->brighttime = saveg_read32();
}

static void saveg_write_strobe_t(strobe_t *str)
{
    saveg_write32(str->sector - sectors);
    saveg_write32(str->count);
    saveg_write32(str->minlight);
    saveg_write32(str->maxlight);
    saveg_write32(str->darktime);
    saveg_write32(str->brighttime);
}

//
// glow_t
//
static void saveg_read_glow_t(glow_t *str)
{
    str->sector = &sectors[saveg_read32()];
    str->minlight = saveg_read32();
    str->maxlight = saveg_read32();
    str->direction = saveg_read32();
}

static void saveg_write_glow_t(glow_t *str)
{
    saveg_write32(str->sector - sectors);
    saveg_write32(str->minlight);
    saveg_write32(str->maxlight);
    saveg_write32(str->direction);
}

static void saveg_read_fireflicker_t(fireflicker_t *str)
{
    str->sector = &sectors[saveg_read32()];
    str->count = saveg_read32();
    str->minlight = saveg_read32();
    str->maxlight = saveg_read32();
}

static void saveg_write_fireflicker_t(fireflicker_t *str)
{
    saveg_write32(str->sector - sectors);
    saveg_write32(str->count);
    saveg_write32(str->minlight);
    saveg_write32(str->maxlight);
}

static void saveg_read_elevator_t(elevator_t *str)
{
    str->type = (elevator_e)saveg_read_enum();
    str->sector = &sectors[saveg_read32()];
    str->direction = saveg_read32();
    str->floordestheight = saveg_read32();
    str->ceilingdestheight = saveg_read32();
    str->speed = saveg_read32();
}

static void saveg_write_elevator_t(elevator_t *str)
{
    saveg_write_enum(str->type);
    saveg_write32(str->sector - sectors);
    saveg_write32(str->direction);
    saveg_write32(str->floordestheight);
    saveg_write32(str->ceilingdestheight);
    saveg_write32(str->speed);
}

static void saveg_read_scroll_t(scroll_t *str)
{
    str->dx = saveg_read32();
    str->dy = saveg_read32();
    str->affectee = saveg_read32();
    str->control = saveg_read32();
    str->last_height = saveg_read32();
    str->vdx = saveg_read32();
    str->vdy = saveg_read32();
    str->accel = saveg_read32();
    str->type = saveg_read_enum();
}

static void saveg_write_scroll_t(scroll_t *str)
{
    saveg_write32(str->dx);
    saveg_write32(str->dy);
    saveg_write32(str->affectee);
    saveg_write32(str->control);
    saveg_write32(str->last_height);
    saveg_write32(str->vdx);
    saveg_write32(str->vdy);
    saveg_write32(str->accel);
    saveg_write_enum(str->type);
}

static void saveg_read_pusher_t(pusher_t *str)
{
    str->type = saveg_read_enum();
    str->x_mag = saveg_read32();
    str->y_mag = saveg_read32();
    str->magnitude = saveg_read32();
    str->radius = saveg_read32();
    str->x = saveg_read32();
    str->y = saveg_read32();
    str->affectee = saveg_read32();
}

static void saveg_write_pusher_t(pusher_t *str)
{
    saveg_write_enum(str->type);
    saveg_write32(str->x_mag);
    saveg_write32(str->y_mag);
    saveg_write32(str->magnitude);
    saveg_write32(str->radius);
    saveg_write32(str->x);
    saveg_write32(str->y);
    saveg_write32(str->affectee);
}

static void saveg_read_button_t(button_t *str)
{
    str->line = &lines[saveg_read32()];
    str->where = (bwhere_e)saveg_read32();
    str->btexture = saveg_read32();
    str->btimer = saveg_read32();
}

static void saveg_write_button_t(button_t *str)
{
    saveg_write32(str->line - lines);
    saveg_write32((int)str->where);
    saveg_write32(str->btexture);
    saveg_write32(str->btimer);
}

//
// Write the header for a savegame
//
void P_WriteSaveGameHeader(char *description)
{
    char        name[VERSIONSIZE];
    int         i;

    for (i = 0; description[i] != '\0'; i++)
        saveg_write8(description[i]);

    for (; i < SAVESTRINGSIZE; i++)
        saveg_write8(0);

    memset(name, 0, sizeof(name));
    strcpy(name, PACKAGE_SAVEGAMEVERSIONSTRING);

    for (i = 0; i < VERSIONSIZE; i++)
        saveg_write8(name[i]);

    saveg_write8(gameskill);
    saveg_write8(gameepisode);
    saveg_write8(gamemap);
    saveg_write8(gamemission);
    saveg_write8((leveltime >> 16) & 0xFF);
    saveg_write8((leveltime >> 8) & 0xFF);
    saveg_write8(leveltime & 0xFF);
}

//
// Read the header for a savegame
//
dboolean P_ReadSaveGameHeader(char *description)
{
    int         i;
    byte        a, b, c;
    char        vcheck[VERSIONSIZE];
    char        read_vcheck[VERSIONSIZE];

    for (i = 0; i < SAVESTRINGSIZE; i++)
        description[i] = saveg_read8();

    for (i = 0; i < VERSIONSIZE; i++)
        read_vcheck[i] = saveg_read8();

    memset(vcheck, 0, sizeof(vcheck));
    strcpy(vcheck, PACKAGE_SAVEGAMEVERSIONSTRING);

    if (strcmp(read_vcheck, vcheck))
    {
        menuactive = false;
        C_ShowConsole();
        C_Warning("This savegame requires <i>%s</i>.", read_vcheck);
        return false;   // bad version
    }

    gameskill = (skill_t)saveg_read8();
    gameepisode = saveg_read8();
    gamemap = saveg_read8();
    saveg_read8();

    // get the times
    a = saveg_read8();
    b = saveg_read8();
    c = saveg_read8();
    leveltime = (a << 16) + (b << 8) + c;

    return true;
}

//
// Read the end of file marker. Returns true if read successfully.
//
dboolean P_ReadSaveGameEOF(void)
{
    return (saveg_read8() == SAVEGAME_EOF);
}

//
// Write the end of file marker
//
void P_WriteSaveGameEOF(void)
{
    saveg_write8(SAVEGAME_EOF);
}

//
// P_ArchivePlayers
//
void P_ArchivePlayers(void)
{
    saveg_write_pad();
    saveg_write_player_t(&players[0]);
}

//
// P_UnArchivePlayers
//
void P_UnArchivePlayers(void)
{
    saveg_read_pad();

    P_InitCards(&players[0]);

    saveg_read_player_t(&players[0]);

    // will be set when unarchiving thinker
    players[0].mo = NULL;
    players[0].message = NULL;
    players[0].attacker = NULL;
}

//
// P_ArchiveWorld
//
void P_ArchiveWorld(void)
{
    int         i;
    int         j;
    sector_t    *sec;
    line_t      *li;
    side_t      *si;

    // do sectors
    for (i = 0, sec = sectors; i < numsectors; i++, sec++)
    {
        saveg_write16(sec->floorheight >> FRACBITS);
        saveg_write16(sec->ceilingheight >> FRACBITS);
        saveg_write16(sec->floorpic);
        saveg_write16(sec->ceilingpic);
        saveg_write16(sec->lightlevel);
        saveg_write16(sec->special);
        saveg_write16(sec->tag);
    }

    // do lines
    for (i = 0, li = lines; i < numlines; i++, li++)
    {
        saveg_write16(li->flags);
        saveg_write16(li->special);
        saveg_write16(li->tag);
        for (j = 0; j < 2; j++)
        {
            if (li->sidenum[j] == NO_INDEX)
                continue;

            si = &sides[li->sidenum[j]];

            saveg_write16(si->textureoffset >> FRACBITS);
            saveg_write16(si->rowoffset >> FRACBITS);
            saveg_write16(si->toptexture);
            saveg_write16(si->bottomtexture);
            saveg_write16(si->midtexture);
        }
    }
}

//
// P_UnArchiveWorld
//
void P_UnArchiveWorld(void)
{
    int         i;
    int         j;
    sector_t    *sec;
    line_t      *li;
    side_t      *si;

    // do sectors
    for (i = 0, sec = sectors; i < numsectors; i++, sec++)
    {
        sec->floorheight = saveg_read16() << FRACBITS;
        sec->ceilingheight = saveg_read16() << FRACBITS;
        sec->floorpic = saveg_read16();
        sec->ceilingpic = saveg_read16();
        sec->lightlevel = saveg_read16();
        sec->special = saveg_read16();
        sec->tag = saveg_read16();
        sec->ceilingdata = NULL;
        sec->floordata = NULL;
        sec->lightingdata = NULL;
        sec->soundtarget = NULL;
        sec->isliquid = isliquid[sec->floorpic];
    }

    // do lines
    for (i = 0, li = lines; i < numlines; i++, li++)
    {
        li->flags = saveg_read16();
        li->special = saveg_read16();
        li->tag = saveg_read16();
        for (j = 0; j < 2; j++)
        {
            if (li->sidenum[j] == NO_INDEX)
                continue;

            si = &sides[li->sidenum[j]];

            si->textureoffset = saveg_read16() << FRACBITS;
            si->rowoffset = saveg_read16() << FRACBITS;
            si->toptexture = saveg_read16();
            si->bottomtexture = saveg_read16();
            si->midtexture = saveg_read16();
        }
    }
}

//
// Thinkers
//

//
// P_ArchiveThinkers
//
void P_ArchiveThinkers(void)
{
    thinker_t   *th;
    int         i;

    // save off the current thinkers
    for (th = thinkerclasscap[th_mobj].cnext; th != &thinkerclasscap[th_mobj]; th = th->cnext)
    {
        saveg_write8(tc_mobj);
        saveg_write_pad();
        saveg_write_mobj_t((mobj_t *)th);
    }

    // save off the bloodsplats
    for (i = 0; i < numsectors; i++)
    {
        bloodsplat_t    *splat;

        for (splat = sectors[i].splatlist; splat; splat = splat->snext)
        {
            saveg_write8(tc_bloodsplat);
            saveg_write_pad();
            saveg_write_bloodsplat_t(splat);
        }
    }

    // add a terminating marker
    saveg_write8(tc_end);
}

//
// killough 11/98
//
// Same as P_SetTarget() in p_tick.c, except that the target is nullified
// first, so that no old target's reference count is decreased (when loading
// savegames, old targets are indices, not really pointers to targets).
//
static void P_SetNewTarget(mobj_t **mop, mobj_t *targ)
{
    *mop = NULL;
    P_SetTarget(mop, targ);
}

//
// P_UnArchiveThinkers
//
void P_UnArchiveThinkers(void)
{
    thinker_t   *currentthinker = thinkercap.next;
    thinker_t   *next;
    int i;

    // remove all the current thinkers
    while (currentthinker != &thinkercap)
    {
        next = currentthinker->next;

        if (currentthinker->function == P_MobjThinker)
        {
            P_RemoveMobj((mobj_t *)currentthinker);
            P_RemoveThinkerDelayed(currentthinker);     // fix mobj leak
        }
        else
            Z_Free(currentthinker);

        currentthinker = next;
    }

    P_InitThinkers();

    // remove the remaining bloodsplats
    for (i = 0; i < numsectors; i++)
    {
        bloodsplat_t    *splat = sectors[i].splatlist;

        while (splat)
        {
            bloodsplat_t    *next = splat->snext;

            P_UnsetBloodSplatPosition(splat);
            splat = next;
        }
    }

    // read in saved thinkers
    while (1)
    {
        byte    tclass = saveg_read8();

        switch (tclass)
        {
            case tc_end:
                return;         // end of list

            case tc_mobj:
            {
                mobj_t  *mobj = Z_Malloc(sizeof(*mobj), PU_LEVEL, NULL);

                saveg_read_pad();
                saveg_read_mobj_t(mobj);

                P_SetThingPosition(mobj);
                mobj->info = &mobjinfo[mobj->type];

                mobj->thinker.function = P_MobjThinker;
                mobj->colfunc = mobj->info->colfunc;
                if (r_textures)
                    mobj->shadowcolfunc = (r_translucency ? ((mobj->flags & MF_FUZZ) ?
                        R_DrawFuzzyShadowColumn : R_DrawShadowColumn) : R_DrawSolidShadowColumn);
                else
                    mobj->shadowcolfunc = R_DrawColorColumn;
                mobj->projectfunc = R_ProjectSprite;

                P_AddThinker(&mobj->thinker);
                break;
            }

            case tc_bloodsplat:
            {
                bloodsplat_t    *splat = Z_Malloc(sizeof(*splat), PU_LEVEL, NULL);

                saveg_read_pad();
                saveg_read_bloodsplat_t(splat);

                if (r_bloodsplats_total < r_bloodsplats_max)
                {
                    splat->sector = R_PointInSubsector(splat->x, splat->y)->sector;
                    P_SetBloodSplatPosition(splat);
                    splat->colfunc = (splat->blood == FUZZYBLOOD ? fuzzcolfunc : bloodsplatcolfunc);
                    r_bloodsplats_total++;
                }
                break;
            }

            default:
                I_Error("P_UnArchiveThinkers: Unknown tclass %i in savegame", tclass);
        }
    }
}

// By Fabian Greffrath. See http://www.doomworld.com/vb/post/1294860.
uint32_t P_ThinkerToIndex(thinker_t *thinker)
{
    thinker_t   *th;
    uint32_t    i = 0;

    if (!thinker)
        return 0;

    for (th = thinkerclasscap[th_mobj].cnext; th != &thinkerclasscap[th_mobj]; th = th->cnext)
    {
        i++;
        if (th == thinker)
            return i;
    }

    return 0;
}

thinker_t *P_IndexToThinker(uint32_t index)
{
    thinker_t   *th;
    uint32_t    i = 0;

    if (!index)
        return NULL;

    for (th = thinkerclasscap[th_mobj].cnext; th != &thinkerclasscap[th_mobj]; th = th->cnext)
        if (++i == index)
            return th;

    return NULL;
}

void P_RestoreTargets(void)
{
    thinker_t   *th;

    for (th = thinkerclasscap[th_mobj].cnext; th != &thinkerclasscap[th_mobj]; th = th->cnext)
    {
        mobj_t      *mo = (mobj_t *)th;

        P_SetNewTarget(&mo->target, (mobj_t *)P_IndexToThinker((uintptr_t)mo->target));
        P_SetNewTarget(&mo->tracer, (mobj_t *)P_IndexToThinker((uintptr_t)mo->tracer));
        P_SetNewTarget(&mo->lastenemy, (mobj_t *)P_IndexToThinker((uintptr_t)mo->lastenemy));
    }
}

//
// P_ArchiveSpecials
//
void P_ArchiveSpecials(void)
{
    thinker_t   *th;
    int         i;
    button_t    *button_ptr;

    // save off the current thinkers
    for (th = thinkerclasscap[th_misc].cnext; th != &thinkerclasscap[th_misc]; th = th->cnext)
    {
        if (!th->function)
        {
            dboolean            done_one = false;

            ceilinglist_t       *ceilinglist;
            platlist_t          *platlist;

            for (ceilinglist = activeceilings; ceilinglist; ceilinglist = ceilinglist->next)
                if (ceilinglist->ceiling == (ceiling_t *)th)
                {
                    saveg_write8(tc_ceiling);
                    saveg_write_pad();
                    saveg_write_ceiling_t((ceiling_t *)th);
                    done_one = true;
                    break;
                }

            // [jeff-d] save height of moving platforms
            for (platlist = activeplats; platlist; platlist = platlist->next)
                if (platlist->plat == (plat_t *)th)
                {
                    saveg_write8(tc_plat);
                    saveg_write_pad();
                    saveg_write_plat_t((plat_t *)th);
                    done_one = true;
                    break;
                }

            if (done_one)
                continue;
        }

        if (th->function == T_MoveCeiling)
        {
            saveg_write8(tc_ceiling);
            saveg_write_pad();
            saveg_write_ceiling_t((ceiling_t *)th);
            continue;
        }

        if (th->function == T_VerticalDoor)
        {
            saveg_write8(tc_door);
            saveg_write_pad();
            saveg_write_vldoor_t((vldoor_t *)th);
            continue;
        }

        if (th->function == T_MoveFloor)
        {
            saveg_write8(tc_floor);
            saveg_write_pad();
            saveg_write_floormove_t((floormove_t *)th);
            continue;
        }

        if (th->function == T_PlatRaise)
        {
            saveg_write8(tc_plat);
            saveg_write_pad();
            saveg_write_plat_t((plat_t *)th);
            continue;
        }

        if (th->function == T_LightFlash)
        {
            saveg_write8(tc_flash);
            saveg_write_pad();
            saveg_write_lightflash_t((lightflash_t *)th);
            continue;
        }

        if (th->function == T_StrobeFlash)
        {
            saveg_write8(tc_strobe);
            saveg_write_pad();
            saveg_write_strobe_t((strobe_t *)th);
            continue;
        }

        if (th->function == T_Glow)
        {
            saveg_write8(tc_glow);
            saveg_write_pad();
            saveg_write_glow_t((glow_t *)th);
            continue;
        }

        if (th->function == T_FireFlicker)
        {
            saveg_write8(tc_fireflicker);
            saveg_write_pad();
            saveg_write_fireflicker_t((fireflicker_t *)th);
            continue;
        }

        if (th->function == T_MoveElevator)
        {
            saveg_write8(tc_elevator);
            saveg_write_pad();
            saveg_write_elevator_t((elevator_t *)th);
            continue;
        }

        if (th->function == T_Scroll)
        {
            saveg_write8(tc_scroll);
            saveg_write_pad();
            saveg_write_scroll_t((scroll_t *)th);
            continue;
        }

        if (th->function == T_Pusher)
        {
            saveg_write8(tc_pusher);
            saveg_write_pad();
            saveg_write_pusher_t((pusher_t *)th);
            continue;
        }
    }

    button_ptr = buttonlist;
    i = MAXBUTTONS;
    do
    {
        if (button_ptr->btimer != 0)
        {
            saveg_write8(tc_button);
            saveg_write_pad();
            saveg_write_button_t(button_ptr);
        }
        button_ptr++;
    } while (--i);

    // add a terminating marker
    saveg_write8(tc_endspecials);
}

void P_StartButton(line_t *line, bwhere_e w, int texture, int time);

//
// P_UnArchiveSpecials
//
void P_UnArchiveSpecials(void)
{
    ceiling_t           *ceiling;
    vldoor_t            *door;
    floormove_t         *floor;
    plat_t              *plat;
    lightflash_t        *flash;
    strobe_t            *strobe;
    glow_t              *glow;
    fireflicker_t       *fireflicker;
    elevator_t          *elevator;
    scroll_t            *scroll;
    pusher_t            *pusher;
    button_t            *button;

    // read in saved thinkers
    while (1)
    {
        byte            tclass = saveg_read8();

        switch (tclass)
        {
            case tc_endspecials:
                return;          // end of list

            case tc_ceiling:
                saveg_read_pad();
                ceiling = Z_Malloc(sizeof(*ceiling), PU_LEVEL, NULL);
                saveg_read_ceiling_t(ceiling);
                ceiling->sector->ceilingdata = ceiling;
                ceiling->thinker.function = T_MoveCeiling;
                P_AddThinker(&ceiling->thinker);
                P_AddActiveCeiling(ceiling);
                break;

            case tc_door:
                saveg_read_pad();
                door = Z_Malloc(sizeof(*door), PU_LEVEL, NULL);
                saveg_read_vldoor_t(door);
                door->sector->ceilingdata = door;
                door->thinker.function = T_VerticalDoor;
                P_AddThinker(&door->thinker);
                break;

            case tc_floor:
                saveg_read_pad();
                floor = Z_Malloc(sizeof(*floor), PU_LEVEL, NULL);
                saveg_read_floormove_t(floor);
                floor->sector->floordata = floor;
                floor->thinker.function = T_MoveFloor;
                P_AddThinker(&floor->thinker);
                break;

            case tc_plat:
                saveg_read_pad();
                plat = Z_Malloc(sizeof(*plat), PU_LEVEL, NULL);
                saveg_read_plat_t(plat);
                plat->sector->floordata = plat;
                P_AddThinker(&plat->thinker);
                P_AddActivePlat(plat);
                break;

            case tc_flash:
                saveg_read_pad();
                flash = Z_Malloc(sizeof(*flash), PU_LEVEL, NULL);
                saveg_read_lightflash_t(flash);
                flash->thinker.function = T_LightFlash;
                P_AddThinker(&flash->thinker);
                break;

            case tc_strobe:
                saveg_read_pad();
                strobe = Z_Malloc(sizeof(*strobe), PU_LEVEL, NULL);
                saveg_read_strobe_t(strobe);
                strobe->thinker.function = T_StrobeFlash;
                P_AddThinker(&strobe->thinker);
                break;

            case tc_glow:
                saveg_read_pad();
                glow = Z_Malloc(sizeof(*glow), PU_LEVEL, NULL);
                saveg_read_glow_t(glow);
                glow->thinker.function = T_Glow;
                P_AddThinker(&glow->thinker);
                break;

            case tc_fireflicker:
                saveg_read_pad();
                fireflicker = Z_Malloc(sizeof(*fireflicker), PU_LEVEL, NULL);
                saveg_read_fireflicker_t(fireflicker);
                fireflicker->thinker.function = T_FireFlicker;
                P_AddThinker(&fireflicker->thinker);
                break;

            case tc_elevator:
                saveg_read_pad();
                elevator = Z_Malloc(sizeof(*elevator), PU_LEVEL, NULL);
                saveg_read_elevator_t(elevator);
                elevator->sector->ceilingdata = elevator;
                elevator->thinker.function = T_MoveElevator;
                P_AddThinker(&elevator->thinker);
                break;

            case tc_scroll:
                saveg_read_pad();
                scroll = Z_Malloc(sizeof(*scroll), PU_LEVEL, NULL);
                saveg_read_scroll_t(scroll);
                scroll->thinker.function = T_Scroll;
                P_AddThinker(&scroll->thinker);
                break;

            case tc_pusher:
                saveg_read_pad();
                pusher = Z_Malloc(sizeof(*pusher), PU_LEVEL, NULL);
                saveg_read_pusher_t(pusher);
                pusher->thinker.function = T_Pusher;
                pusher->source = P_GetPushThing(pusher->affectee);
                P_AddThinker(&pusher->thinker);
                break;

            case tc_button:
                saveg_read_pad();
                button = Z_Malloc(sizeof(*button), PU_LEVEL, NULL);
                saveg_read_button_t(button);
                P_StartButton(button->line, button->where, button->btexture, button->btimer);
                break;

            default:
                I_Error("P_UnarchiveSpecials: unknown tclass %i in savegame", tclass);
        }
    }
}

//
// P_ArchiveMap
//
void P_ArchiveMap(void)
{
    saveg_write32(automapactive);
    saveg_write32(markpointnum);
    saveg_write32(pathpointnum);

    if (markpointnum)
    {
        int     i;

        for (i = 0; i < markpointnum; i++)
        {
            saveg_write32(markpoints[i].x);
            saveg_write32(markpoints[i].y);
        }
    }

    if (pathpointnum)
    {
        int     i;

        for (i = 0; i < pathpointnum; i++)
        {
            saveg_write32(pathpoints[i].x);
            saveg_write32(pathpoints[i].y);
        }
    }
}

//
// P_UnArchiveMap
//
void P_UnArchiveMap(void)
{
    automapactive = saveg_read32();
    markpointnum = saveg_read32();
    pathpointnum = saveg_read32();

    if (automapactive || mapwindow)
        AM_Start(automapactive);

    if (markpointnum)
    {
        int     i;

        while (markpointnum >= markpointnum_max)
        {
            markpointnum_max = (markpointnum_max ? markpointnum_max << 1 : 16);
            markpoints = Z_Realloc(markpoints, markpointnum_max * sizeof(*markpoints));
        }

        for (i = 0; i < markpointnum; i++)
        {
            markpoints[i].x = saveg_read32();
            markpoints[i].y = saveg_read32();
        }
    }

    if (pathpointnum)
    {
        int     i;

        while (pathpointnum >= pathpointnum_max)
        {
            pathpointnum_max = (pathpointnum_max ? pathpointnum_max << 1 : 16);
            pathpoints = Z_Realloc(pathpoints, pathpointnum_max * sizeof(*pathpoints));
        }

        for (i = 0; i < pathpointnum; i++)
        {
            pathpoints[i].x = saveg_read32();
            pathpoints[i].y = saveg_read32();
        }
    }
}
