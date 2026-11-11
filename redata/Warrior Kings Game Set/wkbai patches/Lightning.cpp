VERSION  3.1
NAME "wkbai - Lightning only scratches buildings"
FILE_TYPE COMPONENT

// A wkbai patch to the game set (loaded after it, see wkbai/gameset/gameset.cpp: each definition
// here replaces the game set's of that name). A mod can replace this file with its own copy at
// "Warrior Kings Game Set\wkbai patches\Lightning.cpp".
//
// Mechanics\AOG Lightning.cpp took a flat 350 Structural Integrity off a struck building: more
// than a whole bank or house (300), most of a barracks (600). The Act of God's bolt and a
// thunderstorm's (wkbai/ServerEnvironment.cpp strikeLightning, which runs this sequence) now take
// 6% of the building's Structural Integrity Capacity and set it alight with the game's own fire
// (Mechanics\Fire.cpp "Set Fire to Building": a further 5 at once, then the fire burns, spreads to
// its neighbours and is put out by repairing peasants as any other). A house loses 23 of 300 at
// once (8%), a barracks 41 of 600 (7%), a castle a little over 6%: a scratch, the fire the harm.
// Characters struck are hurt as before (400 Hit Points, the ones around by distance).

DEFINE_VALUE "Lightning Building Damage Share" 0.06


				ACTION_SEQUENCE "Damage Done By Direct Lightning"

					ACTION PLAY_SPECIAL_EFFECT "Lightning AOG" FINDER_SELF LOCATION_OF FINDER_SELF


					ACTION UPON_CONDITION EQUATION_RESULT "I am a Building" FINDER_SELF

						ACTION DECREASE_ITEM "Structural Integrity" FINDER_SELF EQUATION_RESULT "Lightning Damage To a Building" FINDER_SELF
						ACTION EXECUTE_SEQUENCE "Set Fire to Building" FINDER_SELF

					ELSE

						ACTION DECREASE_ITEM "Hit Points" FINDER_SELF CONSTANT 400

					END_UPON_CONDITION

					ACTION EXECUTE_SEQUENCE "Damage Done By Lightning" FINDER_RESULTS "All Characters within 12 metres"

					ACTION EXECUTE_SEQUENCE_AFTER_DELAY "Lightning Effect" FINDER_RESULTS "Random 4 Characters within 12 metres" EQUATION_RESULT "Random Number Up to 1.5" FINDER_SELF

					ACTION SEND_EVENT "Non Specific Attack" FINDER_SELF

				END_ACTION_SEQUENCE


				EQUATION "Lightning Damage To a Building"

					ENODE MULTIPLICATION
						ENODE ITEM_VALUE "Structural Integrity Capacity" FINDER_SELF
						ENODE DEFINED_VALUE "Lightning Building Damage Share"

				END_EQUATION
