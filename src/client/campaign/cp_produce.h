/**
 * @file cp_produce.h
 * @brief Header for production related stuff.
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#ifndef CLIENT_CP_PRODUCE
#define CLIENT_CP_PRODUCE

/** @brief Maximum number of productions queued in any one base. */
#define MAX_PRODUCTIONS		256
#define MAX_PRODUCTIONS_PER_WORKSHOP 5
/** Maximum number of produced items. */
#define MAX_PRODUCTION_AMOUNT 500

/** Size of a UGV in hangar capacity */
#define UGV_SIZE 300

extern const int PRODUCE_FACTOR;
extern const int PRODUCE_DIVISOR;

typedef enum {
	PRODUCTION_TYPE_ITEM,
	PRODUCTION_TYPE_AIRCRAFT,
	PRODUCTION_TYPE_DISASSEMBLY,

	PRODUCTION_TYPE_MAX
} productionType_t;

/**
 * @note Use @c PR_SetData to set this.
 */
typedef struct {
	union productionItem_t {
		const objDef_t *item;				/**< Item to be produced. */
		const struct aircraft_s *aircraft;	/**< Aircraft (sample) to be produced. */
		struct storedUFO_s *ufo;
		const void *pointer;				/**< if you just wanna check whether a valid pointer was set */
	} data;
	productionType_t type;
} productionData_t;

/**
 * @brief Holds all information for the production of one item-type.
 */
typedef struct production_s
{
	int idx; /**< Self reference in the production list. Mainly used for moving/deleting them. */
	productionData_t data; /**< The data behind this production (type and item pointer) */

	int frame;			/**< the frame counter */
	signed int amount;	/**< How much are we producing. */
	double percentDone;		/**< Fraction of the item which is already produced.
							 * 0 if production is not started, 1 if production is over */
	qboolean spaceMessage;	/**< Used in No Free Space message adding. */
	qboolean creditMessage;	/**< Used in No Credits message adding. */
} production_t;

#define PR_IsDisassemblyData(data)	((data)->type == PRODUCTION_TYPE_DISASSEMBLY)
#define PR_IsAircraftData(data)		((data)->type == PRODUCTION_TYPE_AIRCRAFT)
#define PR_IsItemData(data)			((data)->type == PRODUCTION_TYPE_ITEM)
#define PR_IsDisassembly(prod)		(PR_IsDisassemblyData(&(prod)->data))
#define PR_IsAircraft(prod)			(PR_IsAircraftData(&(prod)->data))
#define PR_IsItem(prod)				(PR_IsItemData(&(prod)->data))
#define PR_IsProduction(prod)		(!PR_IsDisassembly(prod))

#define PR_SetData(dataPtr, typeVal, ptr)  do { assert(ptr); (dataPtr)->data.pointer = (ptr); (dataPtr)->type = (typeVal); } while (0);
#define PR_IsDataValid(dataPtr)	((dataPtr)->data.pointer != NULL)

#define PR_GetProgress(prod)	((prod)->percentDone)

/**
 * @brief A production queue. Lists all items to be produced.
 * @sa production_t
 */
typedef struct production_queue_s
{
	int				numItems;		/**< The number of items in the queue. */
	struct production_s	items[MAX_PRODUCTIONS];	/**< Actual production items (in order). */
} production_queue_t;

#define PR_GetProductionForBase(base) (&((base)->productions))

void PR_ProductionInit(void);
void PR_ProductionRun(void);

qboolean PR_ItemIsProduceable(const objDef_t const *item);

struct base_s *PR_ProductionBase(const production_t *production);

int PR_IncreaseProduction(production_t *prod, int amount);
int PR_DecreaseProduction(production_t *prod, int amount);

const char* PR_GetName(const productionData_t *data);
technology_t* PR_GetTech(const productionData_t *data);

void PR_UpdateProductionCap(struct base_s *base);

void PR_UpdateRequiredItemsInBasestorage(struct base_s *base, int amount, const requirements_t const *reqs);
int PR_RequirementsMet(int amount, const requirements_t const *reqs, struct base_s *base);

int PR_GetRemainingMinutes(const struct base_s *base, const production_t* prod, double percentDone);
int PR_GetRemainingHours(const struct base_s *base, const production_t* prod, double percentDone);

production_t *PR_QueueNew(struct base_s *base, const productionData_t *data, signed int amount);
void PR_QueueMove(production_queue_t *queue, int index, int dir);
void PR_QueueDelete(struct base_s *base, production_queue_t *queue, int index);
void PR_QueueNext(struct base_s *base);
int PR_QueueFreeSpace(const struct base_s* base);

#endif /* CLIENT_CP_PRODUCE */
