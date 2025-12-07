# HAMOOPI Libretro Core

This directory contains the libretro port of HAMOOPI, allowing it to run on RetroArch and other libretro frontends.

## Features

The libretro core includes:
- ✅ **Animated Sprite System** - Real character sprites loaded from PCX files with frame-by-frame animation
- ✅ **Advanced Collision System** - Proper hitboxes, hurtboxes, body collision, and attack clashing
- ✅ **Character Selection Screen** - Choose from 4 unique fighters
- ✅ **Special Moves System** - Character-specific powerful abilities with cooldowns
- ✅ Full 2-player fighting game implementation
- ✅ **Multiple Rounds System** - Best of 3 rounds per match
- ✅ **Animated Stage Backgrounds** - Themed stages for each character
- ✅ Physics-based movement (walking, jumping)
- ✅ **Block/Defend Mechanic** - Defend against attacks with B button
- ✅ **Audio Effects** - Sound effects for attacks, jumps, hits, blocks, and specials
- ✅ Combat system with health tracking
- ✅ Real-time gameplay at 60 FPS
- ✅ Title screen and winner announcement
- ✅ Controller/keyboard support for both players
- ✅ **Debug Visualization** - Toggle collision boxes with SELECT, sprite animations with SELECT+START

## Building the Libretro Core

### Prerequisites

- GCC/G++ compiler
- Allegro 4 development libraries (`liballegro4-dev` on Debian/Ubuntu)
- Make

### Build Instructions

```bash
make -f Makefile.libretro
```

This will produce `hamoopi_libretro.so` (on Linux), `hamoopi_libretro.dylib` (on macOS), or `hamoopi_libretro.dll` (on Windows).

### Platform-Specific Builds

**Linux:**
```bash
make -f Makefile.libretro platform=unix
```

**macOS:**
```bash
make -f Makefile.libretro platform=osx
```

**Windows (MinGW):**
```bash
make -f Makefile.libretro platform=win
```

## Installing the Core

1. Build the core as described above
2. Copy `hamoopi_libretro.so` to your RetroArch cores directory:
   - Linux: `~/.config/retroarch/cores/`
   - Windows: `retroarch/cores/`
   - macOS: `~/Library/Application Support/RetroArch/cores/`
3. Copy `hamoopi_libretro.info` to your RetroArch info directory:
   - Linux: `~/.config/retroarch/info/`
   - Windows: `retroarch/info/`
   - macOS: `~/Library/Application Support/RetroArch/info/`

## Using the Core

1. Launch RetroArch
2. Go to "Load Core" and select "HAMOOPI"
3. Since HAMOOPI doesn't require content files, you can start it directly from "Start Core"
4. Press START to begin
5. Select your character!

## Game Flow

1. **Title Screen** → Press START to continue
2. **Character Selection** → Choose your fighter (4 characters available)
   - Use LEFT/RIGHT to browse characters
   - Press A button to confirm selection
   - Both players must select before continuing
3. **Fight!** → Battle begins when both players are ready
   - **Best of 3 Rounds**: First to win 2 rounds wins the match
   - Round indicators shown at top of screen
   - Health resets between rounds
   - 2-second transition between rounds
4. **Match Winner Screen** → Shows final score, press START to return to character selection

## Characters & Stages

Choose from 4 unique fighters, each with distinct colors, special moves, and themed stage:
- **FIRE** (Red) - Aggressive fighter
  - **Special Move**: Fireball Projectile (10 damage) - Travels across screen
  - Stage: Volcano with lava glow and dark mountains
- **WATER** (Blue) - Balanced fighter
  - **Special Move**: Healing Wave - Restores 15 HP (max 100)
  - Stage: Ocean beach with animated waves
- **EARTH** (Green) - Defensive fighter
  - **Special Move**: Ground Stomp (12 damage) - Hits grounded opponents within 80 pixels
  - Stage: Forest with trees and grass details
- **WIND** (Yellow) - Speed fighter
  - **Special Move**: Dash Attack (8 damage) - Quick forward rush
  - Stage: Sky with floating clouds and platforms

Each stage features animated elements and parallax layers for depth!

## Controls

The libretro core maps standard RetroArch controller buttons to HAMOOPI controls:

### Player 1
- **D-Pad**: 
  - Left/Right: Character selection navigation OR Movement in fight
  - Up: Jump (in fight)
- **A Button**: Confirm character selection OR Punch attack (in fight)
- **B Button**: Block/Defend (in fight) - Reduces damage by 80%
- **Y Button**: Special Move (in fight) - Character-specific special ability
- **Start**: Begin game / Continue

### Player 2
- Same layout as Player 1

## Gameplay

- **Title Screen**: Press START to begin
- **Character Select**: Choose from 4 fighters, press A to confirm
- **Fight**: 
  - Move with D-pad
  - Jump with UP
  - Attack with A button
  - **Block with B button** - Hold to defend (reduces damage to 1 HP, but slows movement)
  - **Special Move with Y button** - Character-specific powerful ability (3-second cooldown)
- **Objective**: Win 2 out of 3 rounds by reducing opponent's health to zero each round
- **Winner Screen**: Press START to return to character selection

Each player starts with 100 HP per round. Land attacks to damage your opponent!

### Blocking Mechanics
- Hold B button to block incoming attacks
- Blocked attacks deal only 1 damage (vs 5 damage unblocked) - 80% damage reduction
- Movement speed reduced to 50% while blocking
- Cannot jump while blocking
- Cannot attack while blocking
- Visual shield indicator appears when blocking

### Special Moves
- Press Y button to activate your character's special move
- Each special has a 3-second (180 frames) cooldown
- "SPECIAL READY!" indicator shows when cooldown is finished
- Yellow cooldown bar shows remaining time
- **FIRE - Fireball**: Projectile that travels forward (10 damage)
- **WATER - Healing Wave**: Restores 15 HP instantly
- **EARTH - Ground Stomp**: Area damage to grounded opponents (12 damage, 80-pixel range)
- **WIND - Dash Attack**: Fast dash forward with attack (8 damage)

### Multiple Rounds System
- Best of 3 rounds per match
- First player to win 2 rounds wins the match
- Health resets between rounds
- Round indicators (3 circles per player) show match progress
- 2-second round transition screen
- Character selections preserved across rounds
- Final score displayed on winner screen

### Collision System

HAMOOPI implements a professional fighting game collision system with multiple box types:

**Collision Box Types:**
- **Hurtbox** (Green) - Vulnerable area where player can be hit
  - Shrinks when blocking for defensive advantage
  - Always active when player is alive
- **Hitbox** (Red) - Attack area that can damage opponents
  - Only active during attack frames (frames 2-6 of 10-frame animation)
  - Extends in front of player based on facing direction
  - Checks against opponent's hurtbox for hits
- **Body Collision Box** (Yellow) - Physical presence
  - Prevents players from walking through each other
  - Provides push-back force when overlapping
- **Clash/Priority Box** (Orange) - Attack clashing area
  - Active during attack startup and active frames (frames 1-7)
  - When two clash boxes overlap, both attacks cancel out
  - Players pushed back slightly on clash
  - Clash sound effect plays
- **Projectile Hitbox** (Magenta) - Fireball collision area
  - 30-pixel radius around projectile center
  - Checks against opponent's hurtbox
  - Respects blocking mechanics

**Debug Visualization:**
- Press **SELECT** button during fight to toggle debug collision boxes
- Press **SELECT + START** buttons together to toggle sprite animations on/off
- Color-coded boxes show all active collision areas
- Legend displays at bottom of screen
- Helps understand attack ranges and timing
- Professional fighting game precision

**Collision Features:**
- Frame-accurate hit detection
- Proper attack priority system
- Body pushing prevents overlap
- Attack clashing for mind games
- Smaller hurtbox when blocking rewards defensive play

## Files

- `libretro.cpp` - Main libretro API implementation
- `hamoopi_core.cpp` - Fighting game logic with character selection
- `hamoopi_core.h` - Header file for core functions
- `libretro.h` - Official libretro API header
- `Makefile.libretro` - Build system for the libretro core
- `link.T` - Version script for symbol visibility (Linux)
- `hamoopi_libretro.info` - Core metadata file

## Current Status

This is a fully functional libretro fighting game:
- ✅ Core builds successfully (40KB)
- ✅ Video output working (640x480 @ 60fps)
- ✅ **Audio output fully implemented** - Procedural sound effects at 44.1kHz
- ✅ **Animated stage backgrounds** - Themed stages with parallax effects
- ✅ **Advanced collision system** - Hitboxes, hurtboxes, body collision, attack clashing, projectile hitboxes
- ✅ Input handling implemented (2 players)
- ✅ Frame-based execution
- ✅ **Character selection screen fully integrated**
- ✅ **4 playable characters with unique colors and stages**
- ✅ **Block/Defend mechanic fully implemented**
- ✅ **Multiple rounds system** - Best of 3 rounds with score tracking
- ✅ **Audio effects for all actions** (jump, attack, hit, block)
- ✅ **Game logic fully integrated**
- ✅ **Physics engine (gravity, movement, collision)**
- ✅ **Combat system with health management and blocking**
- ✅ **Game states (title, character select, fight, round transitions, match winner)**
- ✅ **Debug visualization mode** - Toggle collision boxes with SELECT, sprites with SELECT+START
- ❌ Save states not implemented
- ⚠️ Full HAMOOPI character system pending (using simple sprites for now)

## Technical Details

The core implements a complete fighting game with:
- **Character Selection**: 4 fighters with distinct visual styles
- **Multiple Rounds System**: Best of 3 rounds per match
  - Round indicators displayed at top of screen
  - Health resets between rounds
  - Score tracking (e.g., 2-1, 2-0)
  - 2-second transition between rounds showing round result
  - First to win 2 rounds wins the match
- **Stage Backgrounds**: Character-themed animated stages
  - **FIRE**: Volcano with lava glow and mountain silhouettes
  - **WATER**: Ocean beach with animated wave effects
  - **EARTH**: Forest with parallax trees and grass details
  - **WIND**: Sky arena with floating clouds and platforms
  - Animated elements at 60 FPS for dynamic atmosphere
- **Physics System**: Gravity-based movement, ground collision detection
- **Combat Mechanics**: Attack range detection, health tracking, blocking with damage reduction
- **Blocking System**: B button to defend, 80% damage reduction, visual shield indicator
- **Audio System**: Procedural sound generation at 44.1kHz stereo
  - **Jump Sound**: Rising pitch sweep (200Hz → 600Hz)
  - **Attack Sound**: Sharp percussive with falling pitch
  - **Hit Sound**: Impact noise burst
  - **Block Sound**: Metallic clang effect
- **Game Flow**: Title screen → Character selection → Fight → Winner → Repeat
- **Real-time Rendering**: Direct Allegro rendering at 60 FPS
- **Input Processing**: Frame-accurate controller input via libretro API

## Development Notes

The current implementation uses simplified sprite rendering (rectangles and circles) with color-coding per character to demonstrate the fighting game mechanics and character selection system. The architecture supports extending to full HAMOOPI character sprites and animations by integrating the original game's asset loading and rendering systems.

## License

The libretro core follows the same GPL v2 license as HAMOOPI.

## Contributing

Contributions are welcome! Please see the main HAMOOPI repository for contribution guidelines.
