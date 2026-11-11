VERSION  3.1
NAME "wkbai - King of the Hill: the winner wins"
FILE_TYPE COMPONENT

// A wkbai patch to the game set (loaded after it, see wkbai/gameset/gameset.cpp: each definition
// here replaces the game set's of that name). A mod can replace this file with its own copy at
// "Warrior Kings Game Set\wkbai patches\King of the Hill.cpp".
//
// King of the Hill (Valhalla's Victory Point Limit, Mechanics\Valhalla Win Condition.cpp): when a
// player's Victory Time reaches its Victory Time Limit, its Victory Timer task sets its own "Alive
// Multiplayer Player" to 1 ("Excludes this player from the following event", which it doesn't)
// and sends "You have lost the Valhalla Game" to every alive player, itself included. So the
// winner was shown the defeat box and removed as beaten too, and which one the game named the
// winner hung on the order the players were taken in: with the winner first, the loser was left
// the last one alive and given the win box ("A Player Has Won the Valhalla Game").
//
// Here a player at its Victory Point Limit doesn't take that event (the winner keeps its own
// VALHALLA_WIN box from the task), and the "last one alive" win box goes to the others alive only.
// The beaten are still removed by their own reaction; the last one's check removes the winner
// (and its allies) while alive: won (wkbai's Server::finishedPlayers). The Victory Time only grows
// on the hills, so the armies-destroyed ending (Victory Time below the limit) is as before.


		// a player (self) at its Victory Point Limit: King of the Hill's winner
		EQUATION "wkbai Short of the Victory Point Limit"

			ENODE NOT
				ENODE AND
					ENODE IS_POSITIVE
						ENODE ITEM_VALUE "Victory Time Limit" FINDER_SELF
					ENODE GREATER_THAN_OR_EQUAL_TO
						ENODE ITEM_VALUE "Victory Time" FINDER_SELF
						ENODE ITEM_VALUE "Victory Time Limit" FINDER_SELF

		END_EQUATION


		// the same of a candidate (a finder's filter)
		EQUATION "wkbai Candidate Short of the Victory Point Limit"

			ENODE NOT
				ENODE AND
					ENODE IS_POSITIVE
						ENODE ITEM_VALUE "Victory Time Limit" FINDER_CANDIDATE
					ENODE GREATER_THAN_OR_EQUAL_TO
						ENODE ITEM_VALUE "Victory Time" FINDER_CANDIDATE
						ENODE ITEM_VALUE "Victory Time Limit" FINDER_CANDIDATE

		END_EQUATION


		OBJECT_FINDER_DEFINITION "wkbai Alive Players Short of the Victory Point Limit"

			FINDER_FILTER "wkbai Candidate Short of the Victory Point Limit"
				FINDER_RESULTS "Alive Multiplayer Players"

		END_OBJECT_FINDER_DEFINITION



	REACTION "Valhalla Lose if all Units are Destroyed"  //Sits in Players

		TRIGGERED_BY "I am the Last Unit to Die"
		TRIGGERED_BY "You have lost the Valhalla Game"

		// (not the winner, which sends "You have lost the Valhalla Game" to everyone alive)
		ACTION UPON_CONDITION EQUATION_RESULT "wkbai Short of the Victory Point Limit" FINDER_SELF

			ACTION DISPLAY_GAME_TEXT_WINDOW "VALHALLA_ARMIES_DESTROYED_TEXT" FINDER_PLAYER
			ACTION PLAY_SOUND "Valhalla Game Lost" FINDER_RESULTS "Alive Multiplayer Players" FINDER_RESULTS "Alive Multiplayer Players"
			ACTION SET_ITEM "Alive Multiplayer Player" FINDER_PLAYER CONSTANT 0
			ACTION SEND_EVENT "Has The Valhalla Game Been Won?" FINDER_PLAYER
			ACTION REMOVE_MULTIPLAYER_PLAYER FINDER_PLAYER

		END_UPON_CONDITION

	END_REACTION


		REACTION "A Player Has Won the Valhalla Game" //Sits in Players

			TRIGGERED_BY "Has The Valhalla Game Been Won?"

				ACTION UPON_CONDITION EQUATION_RESULT "The Valhalla Game has been Won" FINDER_SELF

					// (a King of the Hill winner has its own box, VALHALLA_WIN)
					ACTION DISPLAY_GAME_TEXT_WINDOW "VALHALLA_ARMIES_WIN" FINDER_RESULTS "wkbai Alive Players Short of the Victory Point Limit"

					ACTION PLAY_SOUND "Valhalla Game Won" FINDER_RESULTS "Alive Multiplayer Players"  FINDER_RESULTS "Alive Multiplayer Players"

					ACTION REMOVE_MULTIPLAYER_PLAYER FINDER_RESULTS "Alive Multiplayer Players"

				END_UPON_CONDITION

		END_REACTION
