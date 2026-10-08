 MazeAttack

MazeAttack is a simple terminal-based shooting game written in C. Blast your advesaries away before they get to you through a maze. It’s a fun exercise in real-time input, game logic, and terminal rendering — all in under a few hundred lines of C code!
NOTE: The original game was authored in 2020(game.c) but a reimplemented version was released in 2026(reimplemented_game.c) to fix glitches including the varying of game speed on different machines.
 Gameplay:

    Kill your enemies as they rush towards you

    Their speed increases the more you kill them

    As levels progresses, different types of enemies spawn

    Avoid enemy fire in some levels as they carry guns

    When you are shot, your character freezes for 3 secs

    Kill them all before they reach your position.

 Built With:

    C (C99 standard)

 How to Run:
1. Clone the repository

git clone https://github.com/ArnoldFromancius/MazeAttack
cd MazeAttack

2. Compile and run

gcc -o maze reimplemented_game.c 
./maze

 Controls:

    LEFT (left), RIGHT (right), UP (shoot), DOWN (grenade) 

    Any other key: Pause

    Q: Quit the game

 Files:

MazeAttack/
├── game.c         # Game logic and rendering
├── maze.h         # (Optional) header file for modularity
└── README.md

 Learning Goals:

    Handling real-time keyboard input in C

    Creating dynamic game loops

    Improving structuring and modularity in C programs

 License:

This project is licensed under the [MIT License](LICENSE). Feel free to play, modify, or build upon it!
