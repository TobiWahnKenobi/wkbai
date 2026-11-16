VERSION  3.1
NAME "wkbai - Winter crafts: the farmers indoors add to the wood and gold delivered"
FILE_TYPE COMPONENT

// A wkbai patch to the game set (loaded after it, see wkbai/gameset/gameset.cpp: each definition
// here replaces the game set's of that name). A mod can replace this file with its own copy at
// "Warrior Kings Game Set\wkbai patches\Winter Crafts.cpp".
//
// wkbai's peasant lodging (wkbai/PeasantLodging.h, the lobby's "Farmers winter indoors"): the
// farmers spend the winter in their houses, villages, manors and castles. Their fields lie fallow
// then anyway (no harvest; their food support is the winter's). Indoors they make a little of
// everything: the engine sets the player's "Winter Crafts Bonus" to 0.002 a farmer indoors, at
// most 0.05, and the wood and gold the peasants deliver are worth that much more. Food is not:
// the game set's food comes from the farms only.
//
// These are Mechanics\Resource Delivery.cpp's two reactions with the wood and gold multiplied by
// 1 + the bonus; the food as before. With the bonus 0 (the rest of the year, and without the
// switch) every delivery is exactly what it was. The carts' deliveries are not multiplied: what
// they carry was delivered to the village by the peasants, with the bonus then.

DECLARE_ITEM "Winter Crafts Bonus" CRITICAL_FOR_CLIENT


			REACTION "Receive Resources Locally"

				TRIGGERED_BY "I wish to Deliver"

				ACTION INCREASE_ITEM "Gold" FINDER_SELF EQUATION_RESULT "Gold Delivered with Winter Crafts" FINDER_PACKAGE_SENDER
				ACTION INCREASE_ITEM "Food" FINDER_SELF EQUATION_RESULT "Food, Less Penalty" FINDER_SELF

				ACTION INCREASE_ITEM "Wood" FINDER_SELF EQUATION_RESULT "Wood Delivered with Winter Crafts" FINDER_PACKAGE_SENDER
				ACTION INCREASE_ITEM "Wood" FINDER_SELF EQUATION_RESULT "Stone into Wood with Winter Crafts" FINDER_PACKAGE_SENDER

			END_REACTION


			REACTION "Receive Resources Globally"

				TRIGGERED_BY "I wish to Deliver"

				ACTION INCREASE_ITEM "Wood" FINDER_PLAYER EQUATION_RESULT "Wood Delivered with Winter Crafts" FINDER_PACKAGE_SENDER

				ACTION INCREASE_ITEM "Wood" FINDER_PLAYER EQUATION_RESULT "Stone into Wood with Winter Crafts" FINDER_PACKAGE_SENDER

				ACTION INCREASE_ITEM "Gold" FINDER_PLAYER EQUATION_RESULT "Gold Delivered with Winter Crafts" FINDER_PACKAGE_SENDER

				ACTION INCREASE_ITEM "Food" FINDER_PLAYER EQUATION_RESULT "Food, Less Penalty" FINDER_PACKAGE_SENDER

			END_REACTION


	// (self: the peasant delivering; its player's bonus)
	EQUATION "Winter Crafts Factor"

		ENODE ADDITION
			ENODE CONSTANT 1
			ENODE ITEM_VALUE "Winter Crafts Bonus" FINDER_PLAYER

	END_EQUATION


	EQUATION "Wood Delivered with Winter Crafts"

		ENODE MULTIPLICATION
			ENODE ITEM_VALUE "Wood" FINDER_SELF
			ENODE EQUATION_RESULT "Winter Crafts Factor" FINDER_SELF

	END_EQUATION


	EQUATION "Gold Delivered with Winter Crafts"

		ENODE MULTIPLICATION
			ENODE ITEM_VALUE "Gold" FINDER_SELF
			ENODE EQUATION_RESULT "Winter Crafts Factor" FINDER_SELF

	END_EQUATION


	EQUATION "Stone into Wood with Winter Crafts"

		ENODE MULTIPLICATION
			ENODE EQUATION_RESULT "Stone converted into Wood" FINDER_SELF
			ENODE EQUATION_RESULT "Winter Crafts Factor" FINDER_SELF

	END_EQUATION
