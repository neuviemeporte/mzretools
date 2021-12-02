dostrace
========

-- This is work in progress, nothing works yet! --
--------------------------------------------------

What is it?
-----------

dostrace is a utility to help with reverse engineering MS-DOS games. 

I started out disassembling the game I was interested in, with plans to rewrite it in C, as I went along, but the outcome was not guaranteed to be correct, with a lot of potential
for introducing mistakes along the way.

Next idea was to ensure a binary identical compilation (since I know the compiler originally used), but I ultimately dropped it due to difficulties (code/data reordering, 
infuence of optimizations, necessity to write an OMF parser for the libraries etc.). I had some wild ideas for relaxing the requirement of the compilation artifact being binarily
identical to the original by using edit distance comparison on the opcodes in the individual functions, ignoring the immediate values etc., but decided it just wasn't practical.

I realized what really matters is the *outcome* being identical (the operation of the game), not the code itself. Here is where dostrace comes in.

The intended workflow goes like this:
1. The game runs inside dostrace at playable speed, which provides an emulated environment for executing x86 code with DOS syscalls
2. The state of the emulated machine is saved before starting the game
3. All inputs (keyboard, joystick) and outputs (writes to memory, ports etc.) are recorded as the game is played in dostrace. Code coverage output can be generated
to identify blind spots in the analysis.
4. The game's C reimplementation is coded based on disassembly analysis, doesn't need to be too deep, as long as it does the same thing.
5. The compiled reimplementation is run in dostrace at unlimited speed using the prerecorded inputs, all outputs are recorded and compared to the original. 
We halt on detecting a discrepancy and point to its location, so the reimplementation can be iteratively improved until (theoretical) 100% accuracy is achieved.

What is it not?
---------------

It's not intended as a replacement for Dosbox, or any other software that lets you play the games for fun.

Why not use a modified Dosbox?
------------------------------

1. It's more fun. I read too much of other people's code at work, I want to do something of my own.
2. I'm too lazy to read into Dosbox code and plant my instrumentation.
3. ... but I might end up doing that if dostrace is unsuccessful.
