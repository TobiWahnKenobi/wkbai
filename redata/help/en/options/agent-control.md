---
title: Agent control
ids: [ui/frontend/options/agent, option/agentControl#agent-control, option/agentPort#its-port, option/agent_apm#human-limit-actions-per-minute, option/agent_reactionMs#human-limit-reaction-delay]
order: 70
---

# Agent control

Lets a program on this computer, such as an AI assistant or a test script, play wkbai the way you do: it moves the cursor, clicks and presses keys, and the game turns that into orders exactly as it does yours. It never gives the game orders any other way. Off by default.

When agent control is on, everyone can see it: "[AGENT]" after the player's name in the lobby, a line under the lobby and under the game's top bar naming who has it on, a chat line when it is switched on or off, and on your own screen a banner and the drawn cursor with an "AGENT" tag. A host can refuse such players (see [Agent players](../ui/frontend/multiplayer/host.md#agent-players)).

![The Agent control page](shot:options-agent)

## Agent control

The label reads "Agent control (a program on this computer plays as you)". On: wkbai listens for a program on this computer at the port below. Off (the default): nothing can connect. Applied when you press OK.

While the program has the input, your own mouse and keyboard are ignored in the game window, except for **Ctrl+Alt+F12** ([`agentTakeOver`](help:key/agentTakeOver)), which takes the input back; press it again to hand it back to the program.

## Its port

The label reads "Its port (127.0.0.1 only)": the network port the program connects to, 1024 to 65535. Empty: 7790. Only programs on this same computer can connect.

## Human limit: actions per minute

The label reads "Human limit: actions per minute (empty: none)": at most this many actions a minute (up to 2,000), so the program can't play faster than a person could. An action is a mouse button or a key (other than Ctrl, Shift and Alt) going down. Empty: no limit.

## Human limit: reaction delay

The label reads "Human limit: reaction delay in ms (empty: none)": each thing the program asks for starts only this many milliseconds later (up to 5,000), as a person needs time to react. Empty: no delay.

> wkbai: agent control is wkbai's own, for testing the game and for AI players that play fair.
