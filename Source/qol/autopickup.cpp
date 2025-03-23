/**
 * @file autopickup.cpp
 *
 * QoL feature for automatically picking up gold
 */
#include "qol/autopickup.h"

#include <algorithm>

#include "inv_iterators.hpp"
#include "options.h"
#include "player.h"
#include "utils/algorithm/container.hpp"
#include "levels/tile_properties.hpp"

namespace devilution {
namespace {

bool HasRoomForGold()
{
	for (int idx : MyPlayer->InvGrid) {
		// Secondary item cell. No need to check those as we'll go through the main item cells anyway.
		if (idx < 0)
			continue;

		// Empty cell. 1x1 space available.
		if (idx == 0)
			return true;

		// Main item cell. Potentially a gold pile so check it.
		auto item = MyPlayer->InvList[idx - 1];
		if (item._itype == ItemType::Gold && item._ivalue < MaxGold)
			return true;
	}

	return false;
}

int NumMiscItemsInInv(int iMiscId)
{
	return c_count_if(InventoryAndBeltPlayerItemsRange { *MyPlayer },
	    [iMiscId](const Item &item) { return item._iMiscId == iMiscId; });
}

bool DoPickup(Item item)
{
	if (item._itype == ItemType::Gold && *GetOptions().Gameplay.autoGoldPickup && HasRoomForGold())
		return true;

	if (item._itype == ItemType::Misc
	    && (CanFitItemInInventory(*MyPlayer, item) || AutoPlaceItemInBelt(*MyPlayer, item))) {
		switch (item._iMiscId) {
		case IMISC_HEAL:
			return *GetOptions().Gameplay.numHealPotionPickup > NumMiscItemsInInv(item._iMiscId);
		case IMISC_FULLHEAL:
			return *GetOptions().Gameplay.numFullHealPotionPickup > NumMiscItemsInInv(item._iMiscId);
		case IMISC_MANA:
			return *GetOptions().Gameplay.numManaPotionPickup > NumMiscItemsInInv(item._iMiscId);
		case IMISC_FULLMANA:
			return *GetOptions().Gameplay.numFullManaPotionPickup > NumMiscItemsInInv(item._iMiscId);
		case IMISC_REJUV:
			return *GetOptions().Gameplay.numRejuPotionPickup > NumMiscItemsInInv(item._iMiscId);
		case IMISC_FULLREJUV:
			return *GetOptions().Gameplay.numFullRejuPotionPickup > NumMiscItemsInInv(item._iMiscId);
		case IMISC_ELIXSTR:
		case IMISC_ELIXMAG:
		case IMISC_ELIXDEX:
		case IMISC_ELIXVIT:
			return *GetOptions().Gameplay.autoElixirPickup;
		case IMISC_OILFIRST:
		case IMISC_OILOF:
		case IMISC_OILACC:
		case IMISC_OILMAST:
		case IMISC_OILSHARP:
		case IMISC_OILDEATH:
		case IMISC_OILSKILL:
		case IMISC_OILBSMTH:
		case IMISC_OILFORT:
		case IMISC_OILPERM:
		case IMISC_OILHARD:
		case IMISC_OILIMP:
		case IMISC_OILLAST:
			return *GetOptions().Gameplay.autoOilPickup;
		default:
			return false;
		}
	}

	return false;
}

} // namespace

void TryPickupAtLocation(const Player& player, Point loc)
{
	if (&player != MyPlayer)
		return;

	if (dItem[loc.x][loc.y] != 0) {
		int itemIndex = dItem[loc.x][loc.y] - 1;
		auto &item = Items[itemIndex];
		if (DoPickup(item)) {
			NetSendCmdGItem(true, CMD_REQUESTAGITEM, player, itemIndex);
			item._iRequest = true;
		}
	}
}

bool IsLineClear(tl::function_ref<bool(Point)> clear, Point startPoint, Point endPoint)
{
	Point position = startPoint;

	int dx = endPoint.x - position.x;
	int dy = endPoint.y - position.y;
	if (std::abs(dx) > std::abs(dy)) {
		if (dx < 0) {
			std::swap(position, endPoint);
			dx = -dx;
			dy = -dy;
		}
		int d;
		int yincD;
		int dincD;
		int dincH;
		if (dy > 0) {
			d = 2 * dy - dx;
			dincD = 2 * dy;
			dincH = 2 * (dy - dx);
			yincD = 1;
		} else {
			d = 2 * dy + dx;
			dincD = 2 * dy;
			dincH = 2 * (dx + dy);
			yincD = -1;
		}
		bool done = false;
		while (!done && position != endPoint) {
			if ((d <= 0) ^ (yincD < 0)) {
				d += dincD;
			} else {
				d += dincH;
				position.y += yincD;
			}
			position.x++;
			done = position != startPoint && !clear(position);
		}
	} else {
		if (dy < 0) {
			std::swap(position, endPoint);
			dy = -dy;
			dx = -dx;
		}
		int d;
		int xincD;
		int dincD;
		int dincH;
		if (dx > 0) {
			d = 2 * dx - dy;
			dincD = 2 * dx;
			dincH = 2 * (dx - dy);
			xincD = 1;
		} else {
			d = 2 * dx + dy;
			dincD = 2 * dx;
			dincH = 2 * (dy + dx);
			xincD = -1;
		}
		bool done = false;
		while (!done && position != endPoint) {
			if ((d <= 0) ^ (xincD < 0)) {
				d += dincD;
			} else {
				d += dincH;
				position.x += xincD;
			}
			position.y++;
			done = position != startPoint && !clear(position);
		}
	}
	return position == endPoint;
}

bool IsLineBlocked(Point startPoint, Point endPoint)
{
	return !IsLineClear(IsTileNotSolid, startPoint, endPoint);
}

void AutoPickup(const Player &player)
{
	if (&player != MyPlayer)
		return;
	if (leveltype == DTYPE_TOWN && !*GetOptions().Gameplay.autoPickupInTown)
		return;

	const int MIN_RADIUS = 0;
	const int MAX_RADIUS = 3;

	int radius = *GetOptions().Gameplay.numAutoPickupRadius;
	radius = std::clamp(radius, MIN_RADIUS, MAX_RADIUS); // subtract 1 since the path already includes a radius of 1.

	if (radius == 0) {
		TryPickupAtLocation(player, player.position.tile);
	} else {
		Point referenceTile = player.position.tile;
		for (int xRad = -radius; xRad <= radius; xRad++) {
			for (int yRad = -radius; yRad <= radius; yRad++) {
				Point itemSpace = { referenceTile.x + xRad, referenceTile.y + yRad };
				if (!InDungeonBounds(itemSpace)) {
					continue;
				}
				if (IsLineBlocked(player.position.tile, itemSpace)) {
					continue;
				}

				TryPickupAtLocation(player, itemSpace);
			}
		}
	}
}
} // namespace devilution
