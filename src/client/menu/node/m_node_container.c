/**
 * @file m_node_container.c
 * @todo move container list code out
 * @todo improve the code genericity
 */

/*
Copyright (C) 1997-2008 UFO:AI Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../../client.h"
#include "../../renderer/r_draw.h"
#include "../../renderer/r_mesh.h"
#include "../../cl_actor.h"
#include "../../cl_team.h"
#include "../m_parse.h"
#include "../m_font.h"
#include "../m_dragndrop.h"
#include "../m_tooltip.h"
#include "../m_nodes.h"
#include "m_node_model.h"
#include "m_node_container.h"
#include "m_node_abstractnode.h"

inventory_t *menuInventory = NULL;

#define EXTRADATA(node) (node->u.container)

/** self cache for drag item
 * @note we can use a global var because we only can have 1 source node at a time
 */
static int dragInfoFromX = -1;
static int dragInfoFromY = -1;

/** self cache for the preview and dropped item
 * @note we can use a global var because we only can have 1 target node at a time
 */
static int dragInfoToX = -1;
static int dragInfoToY = -1;

/** The current invList pointer (only used for ignoring the dragged item
 * for finding free space right now)
 */
static const invList_t *dragInfoIC;

/**
 * @brief Set a filter
 */
void MN_ContainerNodeSetFilter (menuNode_t* node, int num)
{
	if (EXTRADATA(node).filterEquipType != num) {
		EXTRADATA(node).filterEquipType = num;
		EXTRADATA(node).scrollCur = 0;
		EXTRADATA(node).scrollNum = 0;
		EXTRADATA(node).scrollTotalNum = 0;
	}
}

/**
 * @brief Update display of scroll buttons.
 * @note The cvars "mn_cont_scroll_prev_hover" and "mn_cont_scroll_next_hover" are
 * set by the "in" and "out" functions of the scroll buttons.
 */
static void MN_ScrollContainerUpdateScroll (menuNode_t* node)
{
	Cbuf_AddText(va("mn_setnodeproperty equip_scroll current %i\n", EXTRADATA(node).scrollCur));
	Cbuf_AddText(va("mn_setnodeproperty equip_scroll viewsize %i\n", EXTRADATA(node).scrollNum));
	Cbuf_AddText(va("mn_setnodeproperty equip_scroll fullsize %i\n", EXTRADATA(node).scrollTotalNum));
}

/**
 * @brief Scrolls one item forward in a scrollable container.
 * @sa Console command "scrollcont_next"
 * @todo Add check so we do only increase if there are still hidden items after the last displayed one.
 */
static void MN_ScrollContainerNext_f (void)
{
	menuNode_t* node = MN_GetNodeFromCurrentMenu("equip");

	/* Can be called from everywhere. */
	if (!node || !menuInventory)
		return;

	/* Check if the end of the currently visible items still is not the last of the displayable items. */
	if (EXTRADATA(node).scrollCur < EXTRADATA(node).scrollTotalNum
	 && EXTRADATA(node).scrollCur + EXTRADATA(node).scrollNum < EXTRADATA(node).scrollTotalNum) {
		EXTRADATA(node).scrollCur++;
		Com_DPrintf(DEBUG_CLIENT, "MN_ScrollContainerNext_f: Increased current scroll index: %i (num: %i, total: %i).\n",
			EXTRADATA(node).scrollCur,
			EXTRADATA(node).scrollNum,
			EXTRADATA(node).scrollTotalNum);
	} else
		Com_DPrintf(DEBUG_CLIENT, "MN_ScrollContainerNext_f: Current scroll index already %i (num: %i, total: %i)).\n",
			EXTRADATA(node).scrollCur,
			EXTRADATA(node).scrollNum,
			EXTRADATA(node).scrollTotalNum);

	/* Update display of scroll buttons. */
	MN_ScrollContainerUpdateScroll(node);
}

/**
 * @brief Scrolls one item backwards in a scrollable container.
 * @sa Console command "scrollcont_prev"
 */
static void MN_ScrollContainerPrev_f (void)
{
	menuNode_t* node = MN_GetNodeFromCurrentMenu("equip");

	/* Can be called from everywhere. */
	if (!node || !menuInventory)
		return;

	if (EXTRADATA(node).scrollCur > 0) {
		EXTRADATA(node).scrollCur--;
		Com_DPrintf(DEBUG_CLIENT, "MN_ScrollContainerNext_f: Decreased current scroll index: %i (num: %i, total: %i).\n",
			EXTRADATA(node).scrollCur,
			EXTRADATA(node).scrollNum,
			EXTRADATA(node).scrollTotalNum);
	} else
		Com_DPrintf(DEBUG_CLIENT, "MN_ScrollContainerNext_f: Current scroll index already %i (num: %i, total: %i).\n",
			EXTRADATA(node).scrollCur,
			EXTRADATA(node).scrollNum,
			EXTRADATA(node).scrollTotalNum);

	/* Update display of scroll buttons. */
	MN_ScrollContainerUpdateScroll(node);
}

/**
 * @brief Scrolls items in a scrollable container.
 */
static void MN_ScrollContainerScroll_f (void)
{
	int offset;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <+/-offset>\n", Cmd_Argv(0));
		return;
	}

	/* Can be called from everywhere. */
	if (!menuInventory)
		return;

	offset = atoi(Cmd_Argv(1));
	while (offset > 0) {
		MN_ScrollContainerNext_f();
		offset--;
	}
	while (offset < 0) {
		MN_ScrollContainerPrev_f();
		offset++;
	}
}

static inline qboolean MN_IsScrollContainerNode(menuNode_t *node)
{
	return EXTRADATA(node).container && EXTRADATA(node).container->scroll;
}

/**
 * @brief Draws an item to the screen
 *
 * @param[in] org Node position on the screen (pixel). Single nodes: Use the center of the node.
 * @param[in] item The item to draw.
 * @param[in] x Position in container. Set this to -1 if it's drawn in a single container.
 * @param[in] y Position in container. Set this to -1 if it's drawn in a single container.
 * @param[in] scale
 * @param[in] color
 * @sa SCR_DrawCursor
 * Used to draw an item to the equipment containers. First look whether the objDef_t
 * includes an image - if there is none then draw the model
 * @todo check each call of this function nd see if org is every time a vec3_t
 */
void MN_DrawItem (menuNode_t *node, const vec3_t org, const item_t *item, int x, int y, const vec3_t scale, const vec4_t color)
{
	objDef_t *od;
	vec4_t col;
	vec3_t origin;

	assert(item);
	assert(item->t);
	assert(org[2] > -1000 && org[2] < 1000); 	/*< see the todo "check each call" */
	od = item->t;

	Vector4Copy(color, col);
	/* no ammo in this weapon - highlight this item */
	if (od->weapon && od->reload && !item->a) {
		col[1] *= 0.5;
		col[2] *= 0.5;
	}

	VectorCopy(org, origin);

	/* Calculate correct location of the image or the model (depends on rotation) */
	/** @todo Change the rotation of the image as well, right now only the location is changed.
	 * How is image-rotation handled right now? */
	if (x >= 0 || y >= 0) {
		/* Add offset of location in container. */
		origin[0] += x * C_UNIT;
		origin[1] += y * C_UNIT;

		/* Add offset for item-center (depends on rotation). */
		if (item->rotated) {
			origin[0] += od->sy * C_UNIT / 2.0;
			origin[1] += od->sx * C_UNIT / 2.0;
			/** @todo Image size calculation depends on handling of image-rotation.
			imgWidth = od->sy * C_UNIT;
			imgHeight = od->sx * C_UNIT;
			*/
		} else {
			origin[0] += od->sx * C_UNIT / 2.0;
			origin[1] += od->sy * C_UNIT / 2.0;
		}
	}

	/* don't handle the od->tech->image here - it's very ufopedia specific in most cases */
	if (od->image[0] != '\0') {
		const int imgWidth = od->sx * C_UNIT;
		const int imgHeight = od->sy * C_UNIT;

		/* Draw the image. */
		R_DrawNormPic(origin[0], origin[1], imgWidth, imgHeight, 0, 0, 0, 0, ALIGN_CC, qtrue, od->image);
	} else {
		menuModel_t *menuModel = NULL;
		const char *modelName = od->model;
		if (od->tech && od->tech->mdl) {
			menuModel = MN_GetMenuModel(od->tech->mdl);
			/* the model from the tech structure has higher priority, because the item model itself
			 * is mainly for the battlescape or the geoscape - only use that as a fallback */
			modelName = od->tech->mdl;
		}

		/* no model definition in the tech struct, not in the fallback object definition */
		if (modelName == NULL || modelName[0] == '\0') {
			Com_Printf("MN_DrawItem: No model given for item: '%s'\n", od->id);
			return;
		}

		if (menuModel && node) {
			const char* ref = MN_GetReferenceString(node->menu, node->dataImageOrModel);
			MN_DrawModelNode(node, ref, modelName);
		} else {
			modelInfo_t mi;
			vec3_t angles = {-10, 160, 70};
			vec3_t size = {scale[0], scale[1], scale[2]};

			if (item->rotated)
				angles[0] -= 90;

			memset(&mi, 0, sizeof(mi));
			mi.origin = origin;
			mi.angles = angles;
			mi.center = od->center;
			mi.scale = size;
			mi.color = col;
			mi.name = modelName;
			if (od->scale)
				VectorScale(size, od->scale, size);

			/* draw the model */
			R_DrawModelDirect(&mi, NULL, NULL);
		}
	}
}

/**
 * @brief Generate tooltip text for an item.
 * @param[in] item The item we want to generate the tooltip text for.
 * @param[in,out] tooltiptext Pointer to a string the information should be written into.
 * @param[in] string_maxlength Max. string size of tooltiptext.
 * @return Number of lines
 */
static void MN_GetItemTooltip (item_t item, char *tooltiptext, size_t string_maxlength)
{
	int i;
	objDef_t *weapon;

	assert(item.t);

	if (item.amount > 1)
		Com_sprintf(tooltiptext, string_maxlength, "%i x %s\n", item.amount, item.t->name);
	else
		Com_sprintf(tooltiptext, string_maxlength, "%s\n", item.t->name);

	/* Only display further info if item.t is researched */
	if (RS_IsResearched_ptr(item.t->tech)) {
		if (item.t->weapon) {
			/* Get info about used ammo (if there is any) */
			if (item.t == item.m) {
				/* Item has no ammo but might have shot-count */
				if (item.a) {
					Q_strcat(tooltiptext, va(_("Ammo: %i\n"), item.a), string_maxlength);
				}
			} else if (item.m) {
				/* Search for used ammo and display name + ammo count */
				Q_strcat(tooltiptext, va(_("%s loaded\n"), item.m->name), string_maxlength);
				Q_strcat(tooltiptext, va(_("Ammo: %i\n"),  item.a), string_maxlength);
			}
		} else if (item.t->numWeapons) {
			/* Check if this is a non-weapon and non-ammo item */
			if (!(item.t->numWeapons == 1 && item.t->weapons[0] == item.t)) {
				/* If it's ammo get the weapon names it can be used in */
				Q_strcat(tooltiptext, _("Usable in:\n"), string_maxlength);
				for (i = 0; i < item.t->numWeapons; i++) {
					weapon = item.t->weapons[i];
					if (RS_IsResearched_ptr(weapon->tech)) {
						Q_strcat(tooltiptext, va("* %s\n", weapon->name), string_maxlength);
					}
				}
			}
		}
	}
}

/**
 * @brief Draws the rectangle in a 'free' style on position posx/posy (pixel) in the size sizex/sizey (pixel)
 */
static void MN_DrawDisabled (const menuNode_t* node)
{
	const vec4_t color = { 0.3f, 0.3f, 0.3f, 0.7f };
	vec2_t nodepos;

	MN_GetNodeAbsPos(node, nodepos);
	R_DrawFill(nodepos[0], nodepos[1], node->size[0], node->size[1], ALIGN_UL, color);
}

/**
 * @brief Draws the rectangle in a 'free' style on position posx/posy (pixel) in the size sizex/sizey (pixel)
 */
static void MN_DrawFree (int container, const menuNode_t *node, int posx, int posy, int sizex, int sizey, qboolean showTUs)
{
	const vec4_t color = { 0.0f, 1.0f, 0.0f, 0.7f };
	invDef_t* inv = &csi.ids[container];
	vec2_t nodepos;

	MN_GetNodeAbsPos(node, nodepos);
	R_DrawFill(posx, posy, sizex, sizey, ALIGN_UL, color);

	/* if showTUs is true (only the first time in none single containers)
	 * and we are connected to a game */
	if (showTUs && cls.state == ca_active){
		R_FontDrawString("f_verysmall", 0, nodepos[0] + 3, nodepos[1] + 3,
			nodepos[0] + 3, nodepos[1] + 3, node->size[0] - 6, 0, 0,
			va(_("In: %i Out: %i"), inv->in, inv->out), 0, 0, NULL, qfalse, 0);
	}
}

/**
 * @brief Draws the free and usable inventory positions when dragging an item.
 * @note Only call this function in dragging mode
 */
static void MN_ContainerNodeDrawFreeSpace (menuNode_t *node, inventory_t *inv)
{
	const objDef_t *od = MN_DNDGetItem()->t;	/**< Get the 'type' of the dragged item. */
	vec2_t nodepos;

	/* Draw only in dragging-mode and not for the equip-floor */
	assert(MN_DNDIsDragging());
	assert(inv);

	MN_GetNodeAbsPos(node, nodepos);
	/* if single container (hands, extension, headgear) */
	if (EXTRADATA(node).container->single) {
		/* if container is free or the dragged-item is in it */
		if (MN_DNDIsSourceNode(node) || Com_CheckToInventory(inv, od, EXTRADATA(node).container, 0, 0, dragInfoIC))
			MN_DrawFree(EXTRADATA(node).container->id, node, nodepos[0], nodepos[1], node->size[0], node->size[1], qtrue);
	} else {
		/* The shape of the free positions. */
		uint32_t free[SHAPE_BIG_MAX_HEIGHT];
		qboolean showTUs = qtrue;
		int x, y;

		memset(free, 0, sizeof(free));

		for (y = 0; y < SHAPE_BIG_MAX_HEIGHT; y++) {
			for (x = 0; x < SHAPE_BIG_MAX_WIDTH; x++) {
				/* Check if the current position is usable (topleft of the item). */

				/* Add '1's to each position the item is 'blocking'. */
				const int checkedTo = Com_CheckToInventory(inv, od, EXTRADATA(node).container, x, y, dragInfoIC);
				if (checkedTo & INV_FITS)				/* Item can be placed normally. */
					Com_MergeShapes(free, (uint32_t)od->shape, x, y);
				if (checkedTo & INV_FITS_ONLY_ROTATED)	/* Item can be placed rotated. */
					Com_MergeShapes(free, Com_ShapeRotate((uint32_t)od->shape), x, y);

				/* Only draw on existing positions. */
				if (Com_CheckShape(EXTRADATA(node).container->shape, x, y)) {
					if (Com_CheckShape(free, x, y)) {
						MN_DrawFree(EXTRADATA(node).container->id, node, nodepos[0] + x * C_UNIT, nodepos[1] + y * C_UNIT, C_UNIT, C_UNIT, showTUs);
						showTUs = qfalse;
					}
				}
			}	/* for x */
		}	/* for y */
	}
}

/**
 * @brief Calculates the size of a container node and links the container
 * into the node (uses the @c invDef_t shape bitmask to determine the size)
 * @param[in,out] node The node to get the size for
 */
void MN_FindContainer (menuNode_t* const node)
{
	invDef_t *id;
	int i, j;

	/* already a container assigned - no need to recalculate the size */
	if (EXTRADATA(node).container)
		return;

	for (i = 0, id = csi.ids; i < csi.numIDs; id++, i++)
		if (!Q_strncmp(node->name, id->name, sizeof(node->name)))
			break;

	if (i == csi.numIDs)
		EXTRADATA(node).container = NULL;
	else
		EXTRADATA(node).container = &csi.ids[i];

	if (MN_IsScrollContainerNode(node)) {
		/* No need to calculate the size - we directly define it in
		 * the "inventory" entry in the .ufo file anyway. */
		node->size[0] = EXTRADATA(node).container->scroll;
		node->size[1] = EXTRADATA(node).container->scrollHeight;
	} else {
		/* Start on the last bit of the shape mask. */
		for (i = 31; i >= 0; i--) {
			for (j = 0; j < SHAPE_BIG_MAX_HEIGHT; j++)
				if (id->shape[j] & (1 << i))
					break;
			if (j < SHAPE_BIG_MAX_HEIGHT)
				break;
		}
		node->size[0] = C_UNIT * (i + 1) + 0.01;

		/* start on the lower row of the shape mask */
		for (i = SHAPE_BIG_MAX_HEIGHT - 1; i >= 0; i--)
			if (id->shape[i] & ~0x0)
				break;
		node->size[1] = C_UNIT * (i + 1) + 0.01;
	}
}

static const vec3_t scale = {3.5, 3.5, 3.5};
/** @todo it may nice to vectorise that */
static const vec4_t colorDefault = {1, 1, 1, 1};
static const vec4_t colorLoadable = {0.5, 1, 0.5, 1};
static const vec4_t colorDisabled = {0.5, 0.5, 0.5, 1};
static const vec4_t colorDisabledLoadable = {0.5, 0.25, 0.25, 1.0};

/**
 * @brief Draw a container can containe only one iten
 */
static void MN_ContainerNodeDrawSingle (menuNode_t *node, objDef_t *highlightType)
{
	vec4_t color;
	vec3_t pos;
	qboolean disabled = qfalse;

	MN_GetNodeAbsPos(node, pos);
	pos[0] += node->size[0] / 2.0;
	pos[1] += node->size[1] / 2.0;
	pos[2] = 0;

	/* Single item container (special case for left hand). */
	if (EXTRADATA(node).container->id == csi.idLeft && !menuInventory->c[csi.idLeft]) {
		if (menuInventory->c[csi.idRight]) {
			const item_t *item = &menuInventory->c[csi.idRight]->item;
			assert(item);
			assert(item->t);

			if (item->t->holdTwoHanded) {
				if (highlightType && INVSH_LoadableInWeapon(highlightType, item->t))
					memcpy(color, colorLoadable, sizeof(vec4_t));
				else
					memcpy(color, colorDefault, sizeof(vec4_t));
				color[3] = 0.5;
				MN_DrawItem(node, pos, item, -1, -1, scale, color);
			}
		}
	} else if (menuInventory->c[EXTRADATA(node).container->id]) {
		const item_t *item;

		if (menuInventory->c[csi.idRight]) {
			item = &menuInventory->c[csi.idRight]->item;
			/* If there is a weapon in the right hand that needs two hands to shoot it
			 * and there is a weapon in the left, then draw a disabled marker for the
			 * fireTwoHanded weapon. */
			assert(item);
			assert(item->t);
			if (EXTRADATA(node).container->id == csi.idRight && item->t->fireTwoHanded && menuInventory->c[csi.idLeft]) {
				disabled = qtrue;
				MN_DrawDisabled(node);
			}
		}

		item = &menuInventory->c[EXTRADATA(node).container->id]->item;
		assert(item);
		assert(item->t);
		if (highlightType && INVSH_LoadableInWeapon(highlightType, item->t)) {
			if (disabled)
					memcpy(color, colorDisabledLoadable, sizeof(vec4_t));
			else
					memcpy(color, colorLoadable, sizeof(vec4_t));
		} else {
			if (disabled)
					memcpy(color, colorDisabled, sizeof(vec4_t));
			else
					memcpy(color, colorDefault, sizeof(vec4_t));
		}
		if (disabled)
			color[3] = 0.5;
		MN_DrawItem(node, pos, item, -1, -1, scale, color);
	}
}

/**
 * @brief Searches if there is an item at location (x/y) in a scrollable container. You can also provide an item to search for directly (x/y is ignored in that case).
 * @note x = x-th item in a row, y = row. i.e. x/y does not equal the "grid" coordinates as used in those containers.
 * @param[in] i Pointer to the inventory where we will search.
 * @param[in] container Container in the inventory.
 * @param[in] x/y Position in the scrollable container that you want to check. Ignored if "item" is set.
 * @param[in] item The item to search. Will ignore "x" and "y" if set, it'll also search invisible items.
 * @return invList_t Pointer to the invList_t/item that is located at x/y or equals "item".
 * @sa Com_SearchInInventory
 */
static invList_t *MN_ContainerNodeGetItem (const menuNode_t *node, objDef_t *item, const itemFilterTypes_t filterType)
{
	return Com_SearchInInventoryWithFilter(menuInventory, EXTRADATA(node).container, NONE, NONE, item, filterType);
}

/**
 * @brief Draw the inventory of the base
 * @todo We really should group similar weapons (using same ammo) and their ammo together.
 * @todo Need to split and cleanup the loop
 * @todo More local var than it should
 */
static void MN_ContainerNodeDrawBaseInventory (menuNode_t *node, objDef_t *highlightType)
{
	invList_t *ic;
	byte itemType;
	int curWidth = 0;	/**< Combined width of all drawn item so far. */
	int curHeight = 0;	/**< Combined Height of all drawn item so far. */
	int maxHeight = 0;	/**< Max. height of a row. */
	int curCol = 0;		/**< Index of item in current row. */
	int curRow = 0;		/**< Current row. */
	int curItem = 0;	/**< Item counter for all items that get displayed. */
	qboolean lastItem = qfalse;	/**< Did we reach the last displayed/visible item? We only count the items after that.*/
	int rowOffset;
	int ammoIdx;
	vec2_t nodepos;

	/* Remember the last used scroll settings, so we know if we
	 * need to update the button-display later on. */
	const int cache_scrollCur = EXTRADATA(node).scrollCur;
	const int cache_scrollNum = EXTRADATA(node).scrollNum;
	const int cache_scrollTotalNum = EXTRADATA(node).scrollTotalNum;
	const int equipType = EXTRADATA(node).filterEquipType;

	MN_GetNodeAbsPos(node, nodepos);

	EXTRADATA(node).scrollNum = 0;
	EXTRADATA(node).scrollTotalNum = 0;

	/* Change row spacing according to vertical/horizontal setting. */
	rowOffset = EXTRADATA(node).container->scrollVertical ? C_ROW_OFFSET : 0;

	for (itemType = 0; itemType <= 1; itemType++) {	/**< 0==weapons, 1==ammo */
		for (ic = menuInventory->c[EXTRADATA(node).container->id]; ic; ic = ic->next) {
			if (ic->item.t
			 && ((!itemType && Q_strncmp(ic->item.t->type, "ammo", 4) != 0) || (itemType && !Q_strncmp(ic->item.t->type, "ammo", 4)))
			 && INV_ItemMatchesFilter(ic->item.t, equipType)) {
				if (!lastItem && curItem >= EXTRADATA(node).scrollCur) {
					item_t tempItem = {1, NULL, NULL, 0, 0};
					qboolean newRow = qfalse;
					vec3_t pos;
					Vector2Copy(nodepos, pos);
					pos[0] += curWidth;
					pos[1] += curHeight;
					pos[2] = 0;

					if (!EXTRADATA(node).container->scrollVertical && curWidth + ic->item.t->sx * C_UNIT <= EXTRADATA(node).container->scroll) {
						curWidth += ic->item.t->sx * C_UNIT;
					} else {
						/* New row */
						if (curHeight + maxHeight + rowOffset > EXTRADATA(node).container->scrollHeight) {
							/* Last item - We do not draw anything else. */
							lastItem = qtrue;
						} else {
							curHeight += maxHeight + rowOffset;
							curWidth = maxHeight = curCol = 0;
							curRow++;

							/* Re-calculate the position of this item in the new row. */
							Vector2Copy(nodepos, pos);
							pos[1] += curHeight;
							newRow = qtrue;
						}
					}

					maxHeight = max(ic->item.t->sy * C_UNIT, maxHeight);

					if (lastItem || curHeight + ic->item.t->sy + rowOffset > EXTRADATA(node).container->scrollHeight) {
						/* Last item - We do not draw anything else. */
						lastItem = qtrue;
					} else {
						vec2_t posName;
						Vector2Copy(pos, posName);	/* Save original coordiantes for drawing of the item name. */
						posName[1] -= rowOffset;

						/* Calculate the center of the item model/image. */
						pos[0] += ic->item.t->sx * C_UNIT / 2.0;
						pos[1] += ic->item.t->sy * C_UNIT / 2.0;

						assert(ic->item.t);

						/* Update location info for this entry. */
						ic->x = curCol;
						ic->y = curRow;

						/* Actually draw the item. */
						tempItem.t = ic->item.t;

						assert(pos[2] == 0);
						if (highlightType && INVSH_LoadableInWeapon(highlightType, ic->item.t))
							MN_DrawItem(node, pos, &tempItem, -1, -1, scale, colorLoadable);
						else
							MN_DrawItem(node, pos, &tempItem, -1, -1, scale, colorDefault);

						if (EXTRADATA(node).container->scrollVertical) {
							/* Draw the item name. */
							R_FontDrawString("f_verysmall", ALIGN_UL,
								posName[0], posName[1],
								node->pos[0], posName[1],
								node->size[0], 100,	/* max width/height */
								0, va("%s (%i)", ic->item.t->name,  ic->item.amount), 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
						} /* scrollVertical */

						if (newRow)
							curWidth += ic->item.t->sx * C_UNIT;

						/* marge */
						pos[0] += ic->item.t->sx * C_UNIT / 2.0;

						/* Loop through all ammo-types for this item. */
						for (ammoIdx = 0; ammoIdx < ic->item.t->numAmmos; ammoIdx++) {
							invList_t *icAmmo;
							tempItem.t = ic->item.t->ammos[ammoIdx];

							/* skip unusable ammo */
							if (!tempItem.t->tech || !RS_IsResearched_ptr(tempItem.t->tech))
								continue;

							/* find and skip unexisting ammo */
							icAmmo = MN_ContainerNodeGetItem(node, tempItem.t, equipType);
							if (!icAmmo)
								continue;

							/* Calculate the center of the item model/image. */
							pos[0] += tempItem.t->sx * C_UNIT / 2;
							pos[1] = nodepos[1] + curHeight + icAmmo->item.t->sy * C_UNIT / 2.0;

							curWidth += tempItem.t->sx * C_UNIT;

							MN_DrawItem(node, pos, &tempItem, -1, -1, scale, colorDefault);
							R_FontDrawString("f_verysmall", ALIGN_LC,
								pos[0] + icAmmo->item.t->sx * C_UNIT / 2.0, pos[1] + icAmmo->item.t->sy * C_UNIT / 2.0,
								pos[0] + icAmmo->item.t->sx * C_UNIT / 2.0, pos[1] + icAmmo->item.t->sy * C_UNIT / 2.0,
								C_UNIT,	/* maxWidth */
								0,	/* maxHeight */
								0, va("%i", icAmmo->item.amount), 0, 0, NULL, qfalse, 0);

							/* Add width of text and the rest of the item. */
							curWidth += C_UNIT / 2.0;
							pos[0] += tempItem.t->sx * C_UNIT / 2 + C_UNIT / 2.0;
						}
					}

					/* Count displayed items */
					if (!lastItem) {
						EXTRADATA(node).scrollNum++;
						curCol++;
					}
				}

				/* Count items that are possible to be displayed. */
				EXTRADATA(node).scrollTotalNum++;
				curItem++;
			}
		}
	}

	/* Update display of scroll buttons if something changed. */
	if (cache_scrollCur != EXTRADATA(node).scrollCur
	 || cache_scrollNum != EXTRADATA(node).scrollNum
	 ||	cache_scrollTotalNum != EXTRADATA(node).scrollTotalNum)
		MN_ScrollContainerUpdateScroll(node);
}

/**
 * @brief Draw a grip container
 * @todo We really should group similar weapons (using same ammo) and their ammo together.
 */
static void MN_ContainerNodeDrawGrid (menuNode_t *node, objDef_t *highlightType)
{
	const invList_t *ic;
	vec3_t pos;

	MN_GetNodeAbsPos(node, pos);
	pos[2] = 0;

	for (ic = menuInventory->c[EXTRADATA(node).container->id]; ic; ic = ic->next) {
		assert(ic->item.t);
		if (highlightType && INVSH_LoadableInWeapon(highlightType, ic->item.t))
			MN_DrawItem(node, pos, &ic->item, ic->x, ic->y, scale, colorLoadable);
		else
			MN_DrawItem(node, pos, &ic->item, ic->x, ic->y, scale, colorDefault);
	}
}

/**
 * @brief Draw a preview of the DND item dropped into the node
 */
static void MN_ContainerNodeDrawDropPreview (menuNode_t *target)
{
	vec2_t nodepos;
	int checkedTo = INV_DOES_NOT_FIT;
	vec3_t origine;
	vec4_t color = { 1, 1, 1, 1 };
	const vec3_t scale = { 3.5, 3.5, 3.5 };
	item_t previewItem;

	MN_GetNodeAbsPos(target, nodepos);

	/* draw the drop preview item */

	/** Revert the rotation info for the cursor-item in case it
	 * has been changed (can happen in rare conditions).
	 * @todo Maybe we can later change this to reflect "manual" rotation?
	 * @todo Check if this causes problems when letting the item snap back to its original location. */
	previewItem = *MN_DNDGetItem();

	previewItem.rotated = qfalse;

	/* Draw "preview" of placed (&rotated) item. */
	if (!MN_IsScrollContainerNode(target)) {
		checkedTo = Com_CheckToInventory(menuInventory, previewItem.t, EXTRADATA(target).container, dragInfoToX, dragInfoToY, dragInfoIC);

		if (checkedTo == INV_FITS_ONLY_ROTATED)
			previewItem.rotated = qtrue;

		if (checkedTo && Q_strncmp(previewItem.t->type, "armour", MAX_VAR)) {	/* If the item fits somehow and it's not armour */
			vec2_t nodepos;

			MN_GetNodeAbsPos(target, nodepos);
			if (EXTRADATA(target).container->single) { /* Get center of single container for placement of preview item */
				VectorSet(origine,
					nodepos[0] + target->size[0] / 2.0,
					nodepos[1] + target->size[1] / 2.0,
					-40);
			} else {
				/* This is a "grid" container - we need to calculate the item-position
				 * (on the screen) from stored placement in the container and the
				 * calculated rotation info. */
				if (previewItem.rotated)
					VectorSet(origine,
						nodepos[0] + (dragInfoToX + previewItem.t->sy / 2.0) * C_UNIT,
						nodepos[1] + (dragInfoToY + previewItem.t->sx / 2.0) * C_UNIT,
						-40);
				else
					VectorSet(origine,
						nodepos[0] + (dragInfoToX + previewItem.t->sx / 2.0) * C_UNIT,
						nodepos[1] + (dragInfoToY + previewItem.t->sy / 2.0) * C_UNIT,
						-40);
			}
			Vector4Set(color, 0.5, 0.5, 1, 1);	/**< Make the preview item look blueish */
			MN_DrawItem(NULL, origine, &previewItem, -1, -1, scale, color);	/**< Draw preview item. */
		}
	}
}

/**
 * @brief Main function to draw a container node
 */
static void MN_ContainerNodeDraw (menuNode_t *node)
{
	objDef_t *highlightType = NULL;

	if (!EXTRADATA(node).container)
		return;
	if (!menuInventory)
		return;
	/* is container invisible */
	if (node->color[3] < 0.001)
		return;

	/* Highlight weapons that the dragged ammo (if it is one) can be loaded into. */
	if (MN_DNDIsDragging() && MN_DNDGetType() == DND_ITEM) {
		highlightType = MN_DNDGetItem()->t;
	}

	if (EXTRADATA(node).container->single) {
		MN_ContainerNodeDrawSingle(node, highlightType);
	} else {
		if (MN_IsScrollContainerNode(node)) {
			MN_ContainerNodeDrawBaseInventory(node, highlightType);
		} else {
			MN_ContainerNodeDrawGrid(node, highlightType);
		}
	}

	/* Draw free space if dragging - but not for idEquip */
	if (MN_DNDIsDragging() && EXTRADATA(node).container->id != csi.idEquip)
		MN_ContainerNodeDrawFreeSpace(node, menuInventory);

	if (MN_DNDIsTargetNode(node))
		MN_ContainerNodeDrawDropPreview(node);
}

/**
 * @brief Custom tooltip for container node
 * @param[in] node Node we request to draw tooltip
 * @param[in] x Position x of the mouse
 * @param[in] y Position y of the mouse
 */
static void MN_ContainerNodeDrawTooltip (menuNode_t *node, int x, int y)
{
	static char tooltiptext[MAX_VAR * 2];
	const invList_t *itemHover;
	vec2_t nodepos;

	MN_GetNodeAbsPos(node, nodepos);

	/** Find out where the mouse is. */
	if (MN_IsScrollContainerNode(node)) {
		itemHover = MN_GetItemFromScrollableContainer(node, x, y, NULL, NULL);
	} else {
		itemHover = Com_SearchInInventory(menuInventory,
			EXTRADATA(node).container,
			(x - nodepos[0]) / C_UNIT,
			(y - nodepos[1]) / C_UNIT);
	}

	if (itemHover) {
		const int itemToolTipWidth = 250;

		/* Get name and info about item */
		MN_GetItemTooltip(itemHover->item, tooltiptext, sizeof(tooltiptext));
#ifdef DEBUG
		/* Display stored container-coordinates of the item. */
		Q_strcat(tooltiptext, va("\n%i/%i", itemHover->x, itemHover->y), sizeof(tooltiptext));
#endif
		MN_DrawTooltip("f_small", tooltiptext, x, y, itemToolTipWidth, 0);
	}
}

/**
 * @brief Gets location of the item the mouse is over (in a scrollable container)
 * @param[in] node	The container-node.
 * @param[in] mouseX	X location of the mouse.
 * @param[in] mouseY	Y location of the mouse.
 * @param[out] contX	X location in the container (index of item in row).
 * @param[out] contY	Y location in the container (row).
 * @sa MN_ContainerNodeSearchInScrollableContainer
 */
invList_t *MN_GetItemFromScrollableContainer (const menuNode_t* const node, int mouseX, int mouseY, int* contX, int* contY)
{
	invList_t *ic;
	byte itemType;		/**< Variable to seperate weapons&other items (0) and ammo (1). */
	int curWidth = 0;	/**< Combined width of all drawn item so far. */
	int curHeight = 0;	/**< Combined height of all drawn item so far. */
	int maxHeight = 0;	/**< Max. height of a row. */
	int curItem = 0;	/**< Item counter for all items that _could_ get displayed in the current view (equipType). */
	int curDispItem = 0;	/**< Item counter for all items that actually get displayed. */
	int rowOffset;
	const int equipType = EXTRADATA(node).filterEquipType;

	int tempX, tempY;

	if (!contX)
		contX = &tempX;
	if (!contY)
		contY = &tempY;

	/* Change row spacing according to vertical/horizontal setting. */
	rowOffset = EXTRADATA(node).container->scrollVertical ? C_ROW_OFFSET : 0;

	for (itemType = 0; itemType <= 1; itemType++) {	/**< 0==weapons, 1==ammo */
		for (ic = menuInventory->c[EXTRADATA(node).container->id]; ic; ic = ic->next) {
			if (ic->item.t
			 && ((!itemType && !(!Q_strncmp(ic->item.t->type, "ammo", 4))) || (itemType && !Q_strncmp(ic->item.t->type, "ammo", 4)))
			 && INV_ItemMatchesFilter(ic->item.t, equipType)) {
				if (curItem >= EXTRADATA(node).scrollCur && curDispItem < EXTRADATA(node).scrollNum) {
					qboolean newRow = qfalse;
					vec2_t posMin;
					vec2_t posMax;
					MN_GetNodeAbsPos(node, posMin);
					posMin[0] += curWidth;
					posMin[1] += curHeight;
					Vector2Copy(posMin, posMax);

					assert(ic->item.t);

					if (!EXTRADATA(node).container->scrollVertical && curWidth + ic->item.t->sx * C_UNIT <= EXTRADATA(node).container->scroll) {
						curWidth += ic->item.t->sx * C_UNIT;
					} else {
						/* New row */
						if (curHeight + maxHeight + rowOffset > EXTRADATA(node).container->scrollHeight)
								return NULL;

						curHeight += maxHeight + rowOffset;
						curWidth = maxHeight = 0;

						/* Re-calculate the min & max values for this item in the new row. */
						MN_GetNodeAbsPos(node, posMin);
						posMin[1] += curHeight;
						Vector2Copy(posMin, posMax);
						newRow = qtrue;
					}

					maxHeight = max(ic->item.t->sy * C_UNIT, maxHeight);
					if (curHeight + ic->item.t->sy + rowOffset > EXTRADATA(node).container->scrollHeight)
						return NULL;

					posMax[0] += ic->item.t->sx * C_UNIT;
					posMax[1] += ic->item.t->sy * C_UNIT;

					/* If the mouse coordinate is within the area of this item/invList we return its pointer. */
					if (mouseX >= posMin[0]	&& mouseX <= posMax[0]
					 && mouseY >= posMin[1] && mouseY <= posMax[1]) {
						*contX = ic->x;
						*contY = ic->y;
						return ic;
					}

					if (newRow)
						curWidth += ic->item.t->sx * C_UNIT;

					/* This item uses ammo - lets check that as well. */
					if (EXTRADATA(node).container->scrollVertical && ic->item.t->numAmmos && ic->item.t != ic->item.t->ammos[0]) {
						int ammoIdx;
						objDef_t *ammoItem;
						posMin[0] += ic->item.t->sx * C_UNIT;
						Vector2Copy(posMin, posMax);

						/* Loop through all ammo-types for this item. */
						for (ammoIdx = 0; ammoIdx < ic->item.t->numAmmos; ammoIdx++) {
							ammoItem = ic->item.t->ammos[ammoIdx];
							/* Only check for researched ammo (although this is implyed in most cases). */
							if (ammoItem->tech && RS_IsResearched_ptr(ammoItem->tech)) {
								invList_t *icAmmo = MN_ContainerNodeGetItem(node, ammoItem, equipType);

								/* Only check the item (and calculate its size) if it's in the container. */
								if (icAmmo) {
									posMax[0] += ammoItem->sx * C_UNIT;
									posMax[1] += ammoItem->sy * C_UNIT;

									/* If the mouse coordinate is within the area of this ammo item/invList we return its pointer. */
									if (mouseX >= posMin[0]	&& mouseX <= posMax[0]
									 && mouseY >= posMin[1] && mouseY <= posMax[1]) {
										*contX = icAmmo->x;
										*contY = icAmmo->y;
										return icAmmo;
									}

									curWidth += ammoItem->sx * C_UNIT + C_UNIT / 2.0;
									posMin[0] += ammoItem->sx * C_UNIT + C_UNIT / 2.0;
									Vector2Copy(posMin, posMax);
								}
							}
						}
					}

					/* Count displayed/visible items */
					curDispItem++;
				}

				/* Count all items that could get displayed. */
				curItem++;
			}
		}
	}

	*contX = *contY = NONE;
	return NULL;
}

/**
 * @brief Tries to switch to drag mode or auto-assign items
 * when right-click was used in the inventory.
 * @param[in] node The node in the menu that the mouse is over (i.e. a container node).
 * @param[in] base The base we are in.
 * @param[in] mouseX/mouseY Mouse coordinates.
 * @param[in] rightClick If we want to auto-assign items instead of dragging them this has to be qtrue.
 * @todo split this function in two start drag, and autoplace
 */
static void MN_Drag (menuNode_t* node, int mouseX, int mouseY, qboolean rightClick)
{
	int sel;
	invList_t *ic;
	int fromX, fromY;

	if (!menuInventory)
		return;

	/* don't allow this in tactical missions */
	if (selActor && rightClick)
		return;

	sel = cl_selected->integer;
	if (sel < 0)
		return;

	assert(EXTRADATA(node).container);
	/* Get coordinates inside a scrollable container (if it is one). */
	if (MN_IsScrollContainerNode(node)) {
		ic = MN_GetItemFromScrollableContainer(node, mouseX, mouseY, &fromX, &fromY);
		Com_DPrintf(DEBUG_CLIENT, "MN_Drag: item %i/%i selected from scrollable container.\n", fromX, fromY);
	} else {
		vec2_t nodepos;

		MN_GetNodeAbsPos(node, nodepos);
		/* Normalize screen coordinates to container coordinates. */
		fromX = (int) (mouseX - nodepos[0]) / C_UNIT;
		fromY = (int) (mouseY - nodepos[1]) / C_UNIT;

		ic = Com_SearchInInventory(menuInventory, EXTRADATA(node).container, fromX, fromY);
	}

	/* Start drag  */
	if (ic) {
		if (!rightClick) {
			/* Found item to drag. Prepare for drag-mode. */
			MN_DNDDragItem(node, &(ic->item));
			dragInfoIC = ic;
			dragInfoFromX = fromX;
			dragInfoFromY = fromY;
		} else {
			/* Right click: automatic item assignment/removal. */
			if (EXTRADATA(node).container->id != csi.idEquip) {
				/* Move back to idEquip (ground, floor) container. */
				INV_MoveItem(menuInventory, &csi.ids[csi.idEquip], NONE, NONE, EXTRADATA(node).container, ic);
			} else {
				qboolean packed = qfalse;
				int px, py;
				assert(ic->item.t);
				/* armour can only have one target */
				if (!Q_strncmp(ic->item.t->type, "armour", MAX_VAR)) {
					packed = INV_MoveItem(menuInventory, &csi.ids[csi.idArmour], 0, 0, EXTRADATA(node).container, ic);
				/* ammo or item */
				} else if (!Q_strncmp(ic->item.t->type, "ammo", MAX_VAR)) {
					Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idBelt], &px, &py, NULL);
					packed = INV_MoveItem(menuInventory, &csi.ids[csi.idBelt], px, py, EXTRADATA(node).container, ic);
					if (!packed) {
						Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idHolster], &px, &py, NULL);
						packed = INV_MoveItem(menuInventory, &csi.ids[csi.idHolster], px, py, EXTRADATA(node).container, ic);
					}
					if (!packed) {
						Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idBackpack], &px, &py, NULL);
						packed = INV_MoveItem( menuInventory, &csi.ids[csi.idBackpack], px, py, EXTRADATA(node).container, ic);
					}
					/* Finally try left and right hand. There is no other place to put it now. */
					if (!packed) {
						const invList_t *rightHand = Com_SearchInInventory(menuInventory, &csi.ids[csi.idRight], 0, 0);

						/* Only try left hand if right hand is empty or no twohanded weapon/item is in it. */
						if (!rightHand || (rightHand && !rightHand->item.t->fireTwoHanded)) {
							Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idLeft], &px, &py, NULL);
							packed = INV_MoveItem(menuInventory, &csi.ids[csi.idLeft], px, py, EXTRADATA(node).container, ic);
						}
					}
					if (!packed) {
						Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idRight], &px, &py, NULL);
						packed = INV_MoveItem(menuInventory, &csi.ids[csi.idRight], px, py, EXTRADATA(node).container, ic);
					}
				} else {
					if (ic->item.t->headgear) {
						Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idHeadgear], &px, &py, NULL);
						packed = INV_MoveItem(menuInventory, &csi.ids[csi.idHeadgear], px, py, EXTRADATA(node).container, ic);
					} else {
						/* left and right are single containers, but this might change - it's cleaner to check
						 * for available space here, too */
						Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idRight], &px, &py, NULL);
						packed = INV_MoveItem(menuInventory, &csi.ids[csi.idRight], px, py, EXTRADATA(node).container, ic);
						if (!packed) {
							const invList_t *rightHand = Com_SearchInInventory(menuInventory, &csi.ids[csi.idRight], 0, 0);

							/* Only try left hand if right hand is empty or no twohanded weapon/item is in it. */
							if (!rightHand || (rightHand && !rightHand->item.t->fireTwoHanded)) {
								Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idLeft], &px, &py, NULL);
								packed = INV_MoveItem(menuInventory, &csi.ids[csi.idLeft], px, py, EXTRADATA(node).container, ic);
							}
						}
						if (!packed) {
							Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idBelt], &px, &py, NULL);
							packed = INV_MoveItem(menuInventory, &csi.ids[csi.idBelt], px, py, EXTRADATA(node).container, ic);
						}
						if (!packed) {
							Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idHolster], &px, &py, NULL);
							packed = INV_MoveItem(menuInventory, &csi.ids[csi.idHolster], px, py, EXTRADATA(node).container, ic);
						}
						if (!packed) {
							Com_FindSpace(menuInventory, &ic->item, &csi.ids[csi.idBackpack], &px, &py, NULL);
							packed = INV_MoveItem(menuInventory, &csi.ids[csi.idBackpack], px, py, EXTRADATA(node).container, ic);
						}
					}
				}
				/* no need to continue here - placement wasn't successful at all */
				if (!packed)
					return;
			}
		}
		UP_ItemDescription(ic->item.t);
/*		MN_DrawTooltip("f_verysmall", csi.ods[MN_DNDGetItem()->t].name, fromX, fromY, 0);*/
	}

	/* Update display of scroll buttons. */
	if (MN_IsScrollContainerNode(node))
		MN_ScrollContainerUpdateScroll(node);
}

static void MN_ContainerNodeMouseDown (menuNode_t *node, int x, int y, int button)
{
	switch (button) {
	case K_MOUSE1:
		/* start drag and drop */
		MN_Drag(node, x, y, qfalse);
		break;
	case K_MOUSE2:
		if (MN_DNDIsDragging()) {
			MN_DNDAbort();
		} else {
			/* auto place */
			MN_Drag(node, x, y, qtrue);
		}
		break;
	default:
		break;
	}
}

static void MN_ContainerNodeMouseUp (menuNode_t *node, int x, int y, int button)
{
	if (button != K_MOUSE1)
		return;
	if (MN_DNDIsDragging())
		MN_DNDDrop();
}

static void MN_ContainerNodeLoading (menuNode_t *node)
{
	EXTRADATA(node).container = NULL;
	node->color[3] = 1.0;
}

/**
 * @brief Call when a DND enter into the node
 */
static qboolean MN_ContainerNodeDNDEnter (menuNode_t *target)
{
	/* accept items only, if we have a container */
	return MN_DNDGetType() == DND_ITEM && EXTRADATA(target).container;
}

/**
 * @brief Call into the target when the DND hover it
 * @return True if the DND is accepted
 */
static qboolean MN_ContainerNodeDNDMove (menuNode_t *target, int x, int y)
{
	vec2_t nodepos;
	qboolean exists;
	int itemX = 0;
	int itemY = 0;
	int checkedTo = INV_DOES_NOT_FIT;
	item_t *dragItem = MN_DNDGetItem();

	/* we already check it when the node accepte the DND */
	assert(EXTRADATA(target).container);

	MN_GetNodeAbsPos(target, nodepos);

	/** We calculate the position of the top-left corner of the dragged
	 * item in oder to compensate for the centered-drawn cursor-item.
	 * Or to be more exact, we calculate the relative offset from the cursor
	 * location to the middle of the top-left square of the item.
	 * @sa MN_LeftClick */
	if (dragItem->t) {
		itemX = C_UNIT * dragItem->t->sx / 2;	/* Half item-width. */
		itemY = C_UNIT * dragItem->t->sy / 2;	/* Half item-height. */

		/* Place relative center in the middle of the square. */
		itemX -= C_UNIT / 2;
		itemY -= C_UNIT / 2;
	}

	dragInfoToX = (mousePosX - nodepos[0] - itemX) / C_UNIT;
	dragInfoToY = (mousePosY - nodepos[1] - itemY) / C_UNIT;

	/** Check if the items already exists in the container. i.e. there is already at least one item.
	 * @sa Com_AddToInventory */
	exists = qfalse;
	if ((EXTRADATA(target).container->id == csi.idFloor || EXTRADATA(target).container->id == csi.idEquip)
		&& (dragInfoToX  < 0 || dragInfoToY < 0 || dragInfoToX >= SHAPE_BIG_MAX_WIDTH || dragInfoToY >= SHAPE_BIG_MAX_HEIGHT)
		&& Com_ExistsInInventory(menuInventory, EXTRADATA(target).container, *dragItem)) {
		exists = qtrue;
	}

	/** Search for a suitable position to render the item at if
	 * the container is "single", the cursor is out of bound of the container.
	 */
	if (!exists && dragItem->t && (EXTRADATA(target).container->single
		|| dragInfoToX  < 0 || dragInfoToY < 0
		|| dragInfoToX >= SHAPE_BIG_MAX_WIDTH || dragInfoToY >= SHAPE_BIG_MAX_HEIGHT)) {
#if 0
/* ... or there is something in the way. */
/* We would need to check for weapon/ammo as well here, otherwise a preview would be drawn as well when hovering over the correct weapon to reload. */
		|| (Com_CheckToInventory(menuInventory, dragItem->t, EXTRADATA(target).container, dragInfoToX, dragInfoToY) == INV_DOES_NOT_FIT)) {
#endif
		Com_FindSpace(menuInventory, dragItem, EXTRADATA(target).container, &dragInfoToX, &dragInfoToY, dragInfoIC);
	}

	if (!MN_IsScrollContainerNode(target)) {
		checkedTo = Com_CheckToInventory(menuInventory, dragItem->t, EXTRADATA(target).container, dragInfoToX, dragInfoToY, dragInfoIC);
		return checkedTo != 0;
	}

	return qtrue;
}

/**
 * @brief Call when a DND enter into the node
 */
static void MN_ContainerNodeDNDLeave (menuNode_t *node)
{
	dragInfoToX = -1;
	dragInfoToY = -1;
}

/**
 * @brief Call into the source when the DND end
 */
static qboolean MN_ContainerNodeDNDFinished (menuNode_t *source, qboolean isDroped)
{
	item_t *dragItem = MN_DNDGetItem();

	/* if the target can't finalyse the DND we stop */
	if (!isDroped) {
		return qfalse;
	}

	/* tactical mission */
	if (selActor) {
		const menuNode_t *target = MN_DNDGetTargetNode();
		assert(EXTRADATA(source).container);
		assert(target);
		assert(EXTRADATA(target).container);
		MSG_Write_PA(PA_INVMOVE, selActor->entnum,
			EXTRADATA(source).container->id, dragInfoFromX, dragInfoFromY,
			EXTRADATA(target).container->id, dragInfoToX, dragInfoToY);
	} else {
		invList_t *fItem;
		menuNode_t *target;

		/* menu */
		if (MN_IsScrollContainerNode(source)) {
			const int equipType = EXTRADATA(source).filterEquipType;
			fItem = MN_ContainerNodeGetItem(source, dragItem->t, equipType);
		} else
			fItem = Com_SearchInInventory(menuInventory, EXTRADATA(source).container, dragInfoFromX, dragInfoFromY);

		/** @todo need to understand what its done here */
		if (EXTRADATA(source).container->id == csi.idArmour) {
			/** hackhack @todo This is only because armour containers (and their nodes) are
			 * handled differently than normal containers somehow.
			 * dragInfo is not updated in MN_DrawMenus for them, this needs to be fixed.
			 * In a perfect world EXTRADATA(node).container would always be the same as dragInfo.to here. */
			if (MN_DNDGetTargetNode() == source) {
				/* dragInfo.to = EXTRADATA(source).container; */
				dragInfoToX = 0;
				dragInfoToY = 0;
			}
		}

		target = MN_DNDGetTargetNode();
		if (target) {
			/** @todo We must split the move in two. Here, we sould not know how to add the item to the target (see dndDrop) */
			assert(EXTRADATA(target).container);
			INV_MoveItem(menuInventory,
				EXTRADATA(target).container, dragInfoToX, dragInfoToY,
				EXTRADATA(source).container, fItem);
		}
	}

	dragInfoFromX = -1;
	dragInfoFromY = -1;
	return qtrue;
}

void MN_RegisterContainerNode (nodeBehaviour_t* behaviour)
{
	behaviour->name = "container";
	behaviour->draw = MN_ContainerNodeDraw;
	behaviour->drawTooltip = MN_ContainerNodeDrawTooltip;
	behaviour->mouseDown = MN_ContainerNodeMouseDown;
	behaviour->mouseUp = MN_ContainerNodeMouseUp;
	behaviour->loading = MN_ContainerNodeLoading;
	behaviour->loaded = MN_FindContainer;
	behaviour->dndEnter = MN_ContainerNodeDNDEnter;
	behaviour->dndFinished = MN_ContainerNodeDNDFinished;
	behaviour->dndMove = MN_ContainerNodeDNDMove;
	behaviour->dndLeave = MN_ContainerNodeDNDLeave;

	Cmd_AddCommand("scrollcont_next", MN_ScrollContainerNext_f, "Scrolls the current container (forward).");
	Cmd_AddCommand("scrollcont_prev", MN_ScrollContainerPrev_f, "Scrolls the current container (backward).");
	Cmd_AddCommand("scrollcont_scroll", MN_ScrollContainerScroll_f, "Scrolls the current container.");
}
