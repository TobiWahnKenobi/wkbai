VERSION  3.1
NAME "wkbai - Snow on a roof keeps the fire off"
FILE_TYPE COMPONENT

// A wkbai patch to the game set (loaded after it, see wkbai/gameset/gameset.cpp: each definition
// here replaces the game set's of that name). A mod can replace this file with its own copy at
// "Warrior Kings Game Set\wkbai patches\Snow and Fire.cpp".
//
// wkbai's seasons and weather (wkbai/ServerEnvironment.h, the burning buildings): the snow lying
// on a building's roof makes it harder to set alight and slows a fire on it. The engine sets the
// building's "Snow Fire Proofing" to 30 at a full snow cover (less where a fire melted the roof's
// snow), and it counts as Fire Proofing in Mechanics\Fire.cpp's roll: a fire on a house (Fire
// Proofing 60) grows, and its neighbours catch it, 1 time in 20 instead of 1 in 5.
// (The rain putting fires out, and the snow holding a fire's strength down, are the engine's.)
// Without snow, and without the seasons, the item is 0 and the roll is exactly the game set's.

DECLARE_ITEM "Snow Fire Proofing" SERVER_ONLY


		EQUATION "Fire Proofing fails"

			ENODE GREATER_THAN
				ENODE RANDOM_RANGE
					ENODE CONSTANT -100
					ENODE CONSTANT 100.0
				ENODE ADDITION
					ENODE ITEM_VALUE "Fire Proofing" FINDER_SELF
					ENODE ITEM_VALUE "Snow Fire Proofing" FINDER_SELF

		END_EQUATION
