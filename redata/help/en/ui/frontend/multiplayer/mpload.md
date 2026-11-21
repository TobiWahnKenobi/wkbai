---
title: Hosting a saved game
ids: [ui/frontend/mpload, ui/frontend/mpload/players#saved-players, ui/frontend/mpload/me#me, ui/frontend/mpload/ai#ai-general, ui/frontend/mpload/guests#the-guests, ui/frontend/mpload/chat#chat, ui/frontend/mpload/offer#offer-on, ui/frontend/mpload/start#start, ui/frontend/mpload/back#back]
order: 30
---

# Hosting a saved game

Carries on with a saved multiplayer game. Each human player of the saved game gets a row; you decide who plays it: you, a guest, or an AI General.

![The saved game's lobby](shot:frontend-mpload)

## Saved players

One row per human player in the save: "saved player -> played by". Players are matched by name by themselves: your own name takes your old side, and a guest who joins with the name of a saved player takes that player's side. Click a row to select it.

## Me

You play the selected saved player. The side you played before goes to an AI General.

## AI General

An AI General plays the selected saved player.

## The guests

The guests who have joined. A click on a guest gives them the selected saved player; the number of the side they play follows their name.

## Chat

Under the guests: type and press Enter to send.

## Offer on

As in the [host's lobby](host.md#offer-on): where the game is offered.

## Start

Loads the saved game with these players. You need a side of your own, and every guest needs one. Sides nobody takes are played by AI Generals.

## Back

Stops hosting and returns to the Multiplayer screen. Esc does the same.
