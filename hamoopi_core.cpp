#include "hamoopi_core.h"
#include "libretro.h"
#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Global state variables
static BITMAP* screen_buffer = NULL;
static BITMAP* game_buffer = NULL;
static bool initialized = false;
static bool running = false;
static int frame_count = 0;

// Input state for two players
hamoopi_input_t hamoopi_input[2] = {};

// Collision box types
typedef struct {
    float x, y;     // World position (absolute coordinates)
    float w, h;     // Width and height
} CollisionBox;

// Sprite animation system
#define MAX_ANIM_FRAMES 30
#define MAX_ANIMATIONS 20

typedef struct {
    BITMAP* frames[MAX_ANIM_FRAMES];
    int frame_count;
    int state_id;  // Animation state ID (e.g., 100 for walk, 200 for attack)
} Animation;

typedef struct {
    Animation animations[MAX_ANIMATIONS];
    int anim_count;
    bool loaded;
} SpriteSet;

// Sprite cache for all characters
static SpriteSet character_sprites[4];  // One for each character (FIRE, WATER, EARTH, WIND)
static bool sprites_loaded = false;
static bool use_sprite_animations = true;  // Can be toggled with SELECT + START

// INI-based character configuration system
#define MAX_CHAR_ANIMATIONS 50
#define MAX_COLLISION_BOXES 10

typedef struct {
    int state_id;          // Animation state (e.g., 000, 151, 420)
    int xalign, yalign;    // Sprite alignment points
    int frame_times[MAX_ANIM_FRAMES];  // Frame timing for each frame
    int frame_count;       // Number of frames in animation
    float hspeed, vspeed;  // Horizontal/vertical speed during animation
    float gravity;         // Gravity applied during animation
} AnimationConfig;

typedef struct {
    int state_id;          // Animation state
    int frame;             // Frame number
    CollisionBox hurtboxes[MAX_COLLISION_BOXES];
    int hurtbox_count;
    CollisionBox hitboxes[MAX_COLLISION_BOXES];
    int hitbox_count;
} CollisionBoxConfig;

typedef struct {
    char name[64];
    int command_sequence[10];  // Button/direction commands
    int command_count;
    int damage;
    int type;  // 0=projectile, 1=melee, 2=buff
} SpecialMoveConfig;

typedef struct {
    AnimationConfig animations[MAX_CHAR_ANIMATIONS];
    int animation_count;
    CollisionBoxConfig collision_boxes[100];
    int collision_box_count;
    SpecialMoveConfig special_moves[10];
    int special_move_count;
    bool loaded;
} CharacterConfig;

static CharacterConfig character_configs[4];  // One for each character

// Game state
typedef struct {
    float x, y;
    float vx, vy;
    int health;
    int state; // 0=idle, 1=walk, 2=jump, 3=attack, 4=hit, 5=crouch, 6=crouch_attack
    int anim_frame;
    int anim_timer;  // Timer for animation frame updates
    int facing; // 1=right, -1=left
    bool on_ground;
    int character_id; // Character selection (0-3)
    bool is_blocking; // Whether player is currently blocking
    bool is_crouching; // Whether player is currently crouching
    int special_move_cooldown; // Cooldown for special moves
    bool is_dashing; // For WIND character dash attack
    int dash_timer; // Duration of dash
    int attack_frame; // Current frame of attack animation
} Player;

// Projectile system for special moves
typedef struct {
    bool active;
    float x, y;
    float vx, vy;
    int owner; // 0=P1, 1=P2
    int type; // Character-specific projectile type
    int lifetime;
    CollisionBox hitbox; // Projectile hitbox
} Projectile;

#define MAX_PROJECTILES 4
static Projectile projectiles[MAX_PROJECTILES];

// Helper function to find collision boxes from loaded INI data
static CollisionBoxConfig* find_collision_box_config(int char_id, int state_id, int frame)
{
    if (char_id < 0 || char_id >= 4 || !character_configs[char_id].loaded)
        return NULL;
    
    CharacterConfig* config = &character_configs[char_id];
    for (int i = 0; i < config->collision_box_count; i++)
    {
        if (config->collision_boxes[i].state_id == state_id && 
            config->collision_boxes[i].frame == frame)
        {
            return &config->collision_boxes[i];
        }
    }
    return NULL;
}

// Collision box definitions
// Body collision box (for pushing)
static CollisionBox get_body_box(Player* p)
{
    CollisionBox box;
    box.x = p->x - 15;
    box.y = p->y - 40;
    box.w = 30;
    box.h = 40;
    return box;
}

// Hurtbox (vulnerable area) - now loads from chbox.ini
static CollisionBox get_hurtbox(Player* p)
{
    CollisionBox box;
    
    // Get current animation state ID
    int state_id = 0;  // Default idle
    if (p->state == 1) state_id = p->facing > 0 ? 420 : 410;  // Walk
    else if (p->state == 2) state_id = 300;  // Jump
    else if (p->state == 3) state_id = 151;  // Attack
    else if (p->state == 5) state_id = 200;  // Crouch
    else if (p->state == 6) state_id = 201;  // Crouch attack
    else if (p->is_blocking && p->is_crouching) state_id = 208;  // Crouch block
    
    // Try to load from INI data
    CollisionBoxConfig* config = find_collision_box_config(p->character_id, state_id, p->anim_frame);
    if (config && config->hurtbox_count > 0)
    {
        // Use first hurtbox from INI, adjusted for player position and facing
        CollisionBox ini_box = config->hurtboxes[0];
        if (p->facing > 0)
        {
            box.x = p->x + ini_box.x;
            box.y = p->y + ini_box.y;
        }
        else
        {
            // Mirror for left-facing
            box.x = p->x - ini_box.x - ini_box.w;
            box.y = p->y + ini_box.y;
        }
        box.w = ini_box.w;
        box.h = ini_box.h;
        return box;
    }
    
    // Fallback to hardcoded values if INI not found
    if (p->is_crouching)
    {
        // Smaller hurtbox when crouching (only 50% height)
        box.x = p->x - 12;
        box.y = p->y - 19;  // Half height, closer to ground
        box.w = 24;
        box.h = 19;
    }
    else if (p->is_blocking)
    {
        // Smaller hurtbox when blocking
        box.x = p->x - 10;
        box.y = p->y - 35;
        box.w = 20;
        box.h = 35;
    }
    else
    {
        // Normal hurtbox
        box.x = p->x - 12;
        box.y = p->y - 38;
        box.w = 24;
        box.h = 38;
    }
    return box;
}

// Hitbox (attacking area) - now loads from chbox.ini
static CollisionBox get_hitbox(Player* p)
{
    CollisionBox box;
    
    // Only check for hitbox during attack states
    if (p->state != 3 && p->state != 6)
    {
        // No active hitbox
        box.x = p->x;
        box.y = p->y;
        box.w = 0;
        box.h = 0;
        return box;
    }
    
    // Get current animation state ID
    int state_id = (p->state == 3) ? 151 : 201;  // Attack or crouch attack
    
    // Try to load from INI data
    CollisionBoxConfig* config = find_collision_box_config(p->character_id, state_id, p->anim_frame);
    if (config && config->hitbox_count > 0 && p->attack_frame >= 2 && p->attack_frame <= 6)
    {
        // Use first hitbox from INI, adjusted for player position and facing
        CollisionBox ini_box = config->hitboxes[0];
        if (p->facing > 0)
        {
            box.x = p->x + ini_box.x;
            box.y = p->y + ini_box.y;
        }
        else
        {
            // Mirror for left-facing
            box.x = p->x - ini_box.x - ini_box.w;
            box.y = p->y + ini_box.y;
        }
        box.w = ini_box.w;
        box.h = ini_box.h;
        return box;
    }
    
    // Fallback to hardcoded values if INI not found
    if (p->state == 3 && p->attack_frame >= 2 && p->attack_frame <= 6)
    {
        // Active attack frames
        if (p->facing > 0)
        {
            box.x = p->x + 10;
            box.y = p->y - 30;
            box.w = 35;
            box.h = 20;
        }
        else
        {
            box.x = p->x - 45;
            box.y = p->y - 30;
            box.w = 35;
            box.h = 20;
        }
    }
    else if (p->state == 6 && p->attack_frame >= 2 && p->attack_frame <= 6)
    {
        // Crouch attack hitbox
        if (p->facing > 0)
        {
            box.x = p->x + 10;
            box.y = p->y - 15;
            box.w = 35;
            box.h = 15;
        }
        else
        {
            box.x = p->x - 45;
            box.y = p->y - 15;
            box.w = 35;
            box.h = 15;
        }
    }
    else
    {
        // No active hitbox
        box.x = p->x;
        box.y = p->y;
        box.w = 0;
        box.h = 0;
    }
    return box;
}

// Clash/Priority box (for attack clashing)
static CollisionBox get_clash_box(Player* p)
{
    CollisionBox box;
    // Clash box is active during attack startup and active frames
    if (p->state == 3 && p->attack_frame >= 1 && p->attack_frame <= 7)
    {
        if (p->facing > 0)
        {
            box.x = p->x;
            box.y = p->y - 30;
            box.w = 45;
            box.h = 25;
        }
        else
        {
            box.x = p->x - 45;
            box.y = p->y - 30;
            box.w = 45;
            box.h = 25;
        }
    }
    else
    {
        box.x = p->x;
        box.y = p->y;
        box.w = 0;
        box.h = 0;
    }
    return box;
}

// Box collision detection
static bool boxes_overlap(CollisionBox a, CollisionBox b)
{
    return (a.x < b.x + b.w && 
            a.x + a.w > b.x && 
            a.y < b.y + b.h && 
            a.y + a.h > b.y);
}

// Debug visualization for hitboxes
static bool show_debug_boxes = false;

static void draw_debug_box(BITMAP* dest, CollisionBox box, int color)
{
    if (show_debug_boxes && box.w > 0 && box.h > 0)
    {
        rect(dest, (int)box.x, (int)box.y, 
             (int)(box.x + box.w), (int)(box.y + box.h), color);
    }
}

// Sprite loading and animation system
static void load_animation(SpriteSet* sprites, int state_id, const char* char_name)
{
    char filename[256];
    Animation* anim = &sprites->animations[sprites->anim_count];
    anim->state_id = state_id;
    anim->frame_count = 0;
    
    // Load up to MAX_ANIM_FRAMES for this animation state
    for (int frame = 0; frame < MAX_ANIM_FRAMES && anim->frame_count < MAX_ANIM_FRAMES; frame++)
    {
        // HAMOOPI specification uses 3-digit state IDs (e.g., 000, 151, 420)
        snprintf(filename, sizeof(filename), "chars/%s/%03d_%02d.pcx", char_name, state_id, frame);
        
        // Try to load the sprite
        BITMAP* sprite = load_bitmap(filename, NULL);
        if (sprite)
        {
            anim->frames[anim->frame_count] = sprite;
            anim->frame_count++;
        }
        else
        {
            // No more frames for this animation
            break;
        }
    }
    
    // Only count the animation if it has at least one frame
    if (anim->frame_count > 0)
    {
        sprites->anim_count++;
    }
}

static void load_character_sprites(int char_id)
{
    if (character_sprites[char_id].loaded)
    {
        return;  // Already loaded
    }
    
    SpriteSet* sprites = &character_sprites[char_id];
    sprites->anim_count = 0;
    sprites->loaded = false;
    
    // Use CharTemplate as the character folder (all chars use same animations)
    const char* char_name = "CharTemplate";
    
    // Load essential animations based on HAMOOPI specification
    // State 100: Stance/Idle
    load_animation(sprites, 100, char_name);
    
    // State 420: Walking forward
    load_animation(sprites, 420, char_name);
    
    // State 410: Walking backward
    load_animation(sprites, 410, char_name);
    
    // State 300: Neutral jump
    load_animation(sprites, 300, char_name);
    
    // State 320: Forward jump
    load_animation(sprites, 320, char_name);
    
    // State 310: Backward jump
    load_animation(sprites, 310, char_name);
    
    // State 151: Close range weak punch
    load_animation(sprites, 151, char_name);
    
    // State 152: Close range medium punch
    load_animation(sprites, 152, char_name);
    
    // State 153: Close range strong punch
    load_animation(sprites, 153, char_name);
    
    // State 200: Crouching
    load_animation(sprites, 200, char_name);

    // 201 – Crouching Light Punch
    load_animation(sprites, 201, char_name);
    // 202 – Crouching Medium Punch
    load_animation(sprites, 202, char_name);
    // 203 – Crouching Heavy Punch
    load_animation(sprites, 203, char_name);
    // 204 – Crouching Light Kick
    load_animation(sprites, 204, char_name);
    // 205 – Crouching Medium Kick
    load_animation(sprites, 205, char_name);
    // 206 – Crouching Heavy Kick
    load_animation(sprites, 206, char_name);
    // 207 – Start of Crouch Guard
    load_animation(sprites, 207, char_name);
    // 208 – Guarding While Crouched
    load_animation(sprites, 208, char_name);
    // 209 – End of Crouch Guard
    load_animation(sprites, 209, char_name);
    // 210 – Crouch Guard, Applied
    load_animation(sprites, 210, char_name);
    
    
    // State 501: Getting hit type 1 weak
    load_animation(sprites, 501, char_name);
    
    // State 502: Getting hit type 1 medium
    load_animation(sprites, 502, char_name);
    
    // State 700: Special move 1
    load_animation(sprites, 700, char_name);
    
    // State 610: Intro
    load_animation(sprites, 610, char_name);
    
    // State 611: Victory 1
    load_animation(sprites, 611, char_name);
    
    sprites->loaded = true;
}

static Animation* get_animation(SpriteSet* sprites, int state_id)
{
    for (int i = 0; i < sprites->anim_count; i++)
    {
        if (sprites->animations[i].state_id == state_id)
        {
            return &sprites->animations[i];
        }
    }
    return NULL;
}

static BITMAP* get_sprite_frame(Player* p)
{
    if (!sprites_loaded)
    {
        return NULL;
    }
    
    SpriteSet* sprites = &character_sprites[p->character_id];
    if (!sprites->loaded)
    {
        return NULL;
    }
    
    // Map game state to sprite animation state (HAMOOPI specification)
    int sprite_state = 0;
    
    if (p->state == 6)  // Crouching attack
    {
        sprite_state = 201;  // Soco Fraco Abaixado (Crouching weak punch)
    }
    else if (p->state == 5)  // Crouching
    {
        if (p->is_blocking)
        {
            sprite_state = 208;  // Defendendo Abaixado (Blocking crouched)
        }
        else
        {
            sprite_state = 200;  // Abaixado (Crouch stance)
        }
    }
    else if (p->is_blocking)
    {
        sprite_state = 208;  // Blocking crouched (208) - defensive stance
    }
    else if (p->state == 3)  // Attack
    {
        sprite_state = 151;  // Close range weak punch (151)
    }
    else if (p->state == 2)  // Jump
    {
        // Determine jump direction based on velocity
        if (p->vx > 0.5f)
        {
            sprite_state = 320;  // Forward jump
        }
        else if (p->vx < -0.5f)
        {
            sprite_state = 310;  // Backward jump
        }
        else
        {
            sprite_state = 300;  // Neutral jump
        }
    }
    else if (p->state == 1)  // Walk
    {
        // Determine walk direction relative to facing
        // Moving in facing direction = forward walk (420)
        // Moving against facing = backward walk (410)
        if ((p->facing > 0 && p->vx > 0) || (p->facing < 0 && p->vx < 0))
        {
            sprite_state = 420;  // Walking forward
        }
        else
        {
            sprite_state = 410;  // Walking backward
        }
    }
    else  // Idle
    {
        sprite_state = 100;  // Stance
    }
    
    Animation* anim = get_animation(sprites, sprite_state);
    if (!anim || anim->frame_count == 0)
    {
        // Fallback to idle if animation not found
        anim = get_animation(sprites, 0);
        if (!anim || anim->frame_count == 0)
        {
            return NULL;
        }
    }
    
    // Get current frame (wrap around if needed)
    int frame_index = p->anim_frame % anim->frame_count;
    return anim->frames[frame_index];
}

static void init_sprite_system()
{
    if (sprites_loaded)
    {
        return;
    }
    
    // Initialize all sprite sets
    for (int i = 0; i < 4; i++)
    {
        character_sprites[i].loaded = false;
        character_sprites[i].anim_count = 0;
        for (int j = 0; j < MAX_ANIMATIONS; j++)
        {
            character_sprites[i].animations[j].frame_count = 0;
            for (int k = 0; k < MAX_ANIM_FRAMES; k++)
            {
                character_sprites[i].animations[j].frames[k] = NULL;
            }
        }
    }
    
    sprites_loaded = true;
}

static void cleanup_sprite_system()
{
    if (!sprites_loaded)
    {
        return;
    }
    
    // Free all loaded sprites
    for (int i = 0; i < 4; i++)
    {
        SpriteSet* sprites = &character_sprites[i];
        if (sprites->loaded)
        {
            for (int j = 0; j < sprites->anim_count; j++)
            {
                Animation* anim = &sprites->animations[j];
                for (int k = 0; k < anim->frame_count; k++)
                {
                    if (anim->frames[k])
                    {
                        destroy_bitmap(anim->frames[k]);
                        anim->frames[k] = NULL;
                    }
                }
            }
            sprites->loaded = false;
        }
    }
    
    sprites_loaded = false;
}

static Player players[2];
static int game_mode = 0; // 0=title, 1=character_select, 2=fight, 3=winner

// Round system (best of 3)
static int p1_rounds_won = 0;
static int p2_rounds_won = 0;
static int current_round = 1;
static int round_transition_timer = 0;

// Character selection state
static int p1_cursor = 0;
static int p2_cursor = 1;
static bool p1_ready = false;
static bool p2_ready = false;

// Input tracking for character selection
static bool p1_left_pressed = false;
static bool p1_right_pressed = false;
static bool p1_a_pressed = false;
static bool p2_left_pressed = false;
static bool p2_right_pressed = false;
static bool p2_a_pressed = false;

// Character system constants
#define NUM_CHARACTERS 4

// Combat balance constants
#define NORMAL_DAMAGE 5
#define BLOCKED_DAMAGE 1
#define BLOCKING_SPEED_MULTIPLIER 0.5f
#define BLOCKING_COLOR_DIVISOR 2
#define ATTACK_DAMAGE_FRAME 2  // Frame on which damage is actually applied

// Special move constants
#define SPECIAL_MOVE_COOLDOWN 180  // 3 seconds @ 60 FPS
#define FIRE_PROJECTILE_DAMAGE 10
#define WATER_HEAL_AMOUNT 15
#define EARTH_STOMP_DAMAGE 12
#define EARTH_STOMP_RANGE 80.0f
#define WIND_DASH_DAMAGE 8
#define WIND_DASH_SPEED 12.0f
#define WIND_DASH_DURATION 15  // frames
#define WIND_DASH_HIT_RANGE 50.0f
#define PROJECTILE_HIT_RADIUS 30.0f

// Stage/background animation
static int stage_animation_frame = 0;

// Background system - dynamic loading from config.ini
#define MAX_BACKGROUNDS 4
typedef struct {
    BITMAP* image;  // Background image loaded from PCX
    int map_pos_x;  // Horizontal position from config.ini
    int map_pos_y;  // Vertical position from config.ini
    bool loaded;
    char name[64];
} Background;

static Background backgrounds[MAX_BACKGROUNDS];
static int background_count = 0;
static bool backgrounds_initialized = false;

// Audio constants
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE 735  // ~60 FPS: 44100/60 = 735 samples per frame

// Sound effect types
enum SoundEffect {
    SOUND_NONE = 0,
    SOUND_JUMP = 1,
    SOUND_ATTACK = 2,
    SOUND_HIT = 3,
    SOUND_BLOCK = 4,
    SOUND_SPECIAL = 5  // Special move sound
};

// Audio state
// Sound effect queue (simple system)
static enum SoundEffect sound_queue[4] = {SOUND_NONE, SOUND_NONE, SOUND_NONE, SOUND_NONE};
static int sound_effect_timer[4] = {0, 0, 0, 0};
static int sound_effect_duration[4] = {0, 0, 0, 0};

// Character colors for visual distinction
static const int char_colors[NUM_CHARACTERS][3] = {
    {255, 100, 100},  // Red - FIRE
    {100, 100, 255},  // Blue - WATER
    {100, 255, 100},  // Green - EARTH
    {255, 255, 100}   // Yellow - WIND
};

// Fonts
static FONT* game_font = NULL;

// Function to play a sound effect
static void play_sound(enum SoundEffect effect)
{
    // Find an empty slot in the sound queue
    for (int i = 0; i < 4; i++)
    {
        if (sound_effect_timer[i] <= 0)
        {
            sound_queue[i] = effect;
            
            // Set duration based on effect type
            switch (effect)
            {
                case SOUND_JUMP:
                    sound_effect_duration[i] = AUDIO_SAMPLE_RATE / 20; // 0.05 seconds
                    break;
                case SOUND_ATTACK:
                    sound_effect_duration[i] = AUDIO_SAMPLE_RATE / 15; // 0.067 seconds
                    break;
                case SOUND_HIT:
                    sound_effect_duration[i] = AUDIO_SAMPLE_RATE / 25; // 0.04 seconds
                    break;
                case SOUND_BLOCK:
                    sound_effect_duration[i] = AUDIO_SAMPLE_RATE / 30; // 0.033 seconds
                    break;
                case SOUND_SPECIAL:
                    sound_effect_duration[i] = AUDIO_SAMPLE_RATE / 10; // 0.1 seconds
                    break;
                default:
                    sound_effect_duration[i] = 0;
                    break;
            }
            
            // Initialize timer to duration
            sound_effect_timer[i] = sound_effect_duration[i];
            break;
        }
    }
}

// Generate a single audio sample for a sound effect
static int16_t generate_sound_sample(enum SoundEffect effect, int position, int duration)
{
    if (effect == SOUND_NONE || duration == 0)
        return 0;
    
    float t = (float)position / (float)duration;
    float amplitude = (1.0f - t) * 0.15f; // Decay envelope
    int16_t sample = 0;
    
    switch (effect)
    {
        case SOUND_JUMP:
            // Rising pitch sweep
            {
                float freq = 200.0f + t * 400.0f; // 200Hz to 600Hz sweep
                float phase = (float)position * freq * 2.0f * 3.14159f / AUDIO_SAMPLE_RATE;
                sample = (int16_t)(sin(phase) * amplitude * 32767.0f);
            }
            break;
            
        case SOUND_ATTACK:
            // Sharp percussive sound
            {
                float freq = 150.0f * (1.0f - t * 0.5f); // Falling pitch
                float phase = (float)position * freq * 2.0f * 3.14159f / AUDIO_SAMPLE_RATE;
                sample = (int16_t)(sin(phase) * amplitude * 32767.0f);
            }
            break;
            
        case SOUND_HIT:
            // Impact sound with noise
            {
                float noise = ((float)(rand() % 1000) / 500.0f - 1.0f);
                sample = (int16_t)(noise * amplitude * 32767.0f);
            }
            break;
            
        case SOUND_BLOCK:
            // Metallic clang
            {
                float freq = 800.0f + (float)(rand() % 200);
                float phase = (float)position * freq * 2.0f * 3.14159f / AUDIO_SAMPLE_RATE;
                sample = (int16_t)(sin(phase) * amplitude * 32767.0f * 0.5f);
            }
            break;
            
        case SOUND_SPECIAL:
            // Special move power-up sound
            {
                float freq = 300.0f + t * 500.0f; // Rising sweep 300Hz to 800Hz
                float phase = (float)position * freq * 2.0f * 3.14159f / AUDIO_SAMPLE_RATE;
                float harmonic = sin(phase * 2.0f) * 0.3f; // Add harmonic
                sample = (int16_t)((sin(phase) + harmonic) * amplitude * 32767.0f);
            }
            break;
            
        default:
            sample = 0;
            break;
    }
    
    return sample;
}

// Fill audio buffer with generated sound effects
void hamoopi_get_audio_samples(int16_t* buffer, size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        int16_t left = 0;
        int16_t right = 0;
        
        // Mix all active sound effects
        for (int j = 0; j < 4; j++)
        {
            if (sound_effect_timer[j] > 0)
            {
                int pos = sound_effect_duration[j] - sound_effect_timer[j];
                int16_t sample = generate_sound_sample(sound_queue[j], pos, sound_effect_duration[j]);
                
                left += sample;
                right += sample;
                
                sound_effect_timer[j]--;
                
                if (sound_effect_timer[j] <= 0)
                {
                    sound_queue[j] = SOUND_NONE;
                    sound_effect_duration[j] = 0;
                }
            }
        }
        
        // Clamp to prevent overflow
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        
        buffer[i * 2] = left;
        buffer[i * 2 + 1] = right;
    }
}

// Projectile helper functions
static void spawn_projectile(int owner, int type, float x, float y, float vx, float vy)
{
    // Find an empty projectile slot
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        if (!projectiles[i].active)
        {
            projectiles[i].active = true;
            projectiles[i].owner = owner;
            projectiles[i].type = type;
            projectiles[i].x = x;
            projectiles[i].y = y;
            projectiles[i].vx = vx;
            projectiles[i].vy = vy;
            projectiles[i].lifetime = 180; // 3 seconds max
            
            // Set projectile hitbox based on type
            projectiles[i].hitbox.x = x - 15;
            projectiles[i].hitbox.y = y - 15;
            projectiles[i].hitbox.w = 30;
            projectiles[i].hitbox.h = 30;
            break;
        }
    }
}

static void update_projectiles(void)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        if (!projectiles[i].active) continue;
        
        // Update position
        projectiles[i].x += projectiles[i].vx;
        projectiles[i].y += projectiles[i].vy;
        projectiles[i].lifetime--;
        
        // Update hitbox position
        projectiles[i].hitbox.x = projectiles[i].x - 15;
        projectiles[i].hitbox.y = projectiles[i].y - 15;
        
        // Deactivate if out of bounds or expired
        if (projectiles[i].x < 0 || projectiles[i].x > 640 ||
            projectiles[i].y < 0 || projectiles[i].y > 480 ||
            projectiles[i].lifetime <= 0)
        {
            projectiles[i].active = false;
            continue;
        }
        
        // Check collision with players using hitbox vs hurtbox
        int target = (projectiles[i].owner == 0) ? 1 : 0;
        Player* target_player = &players[target];
        
        CollisionBox target_hurtbox = get_hurtbox(target_player);
        
        if (boxes_overlap(projectiles[i].hitbox, target_hurtbox) && target_player->health > 0)
        {
            // Hit!
            if (target_player->is_blocking)
            {
                target_player->health -= BLOCKED_DAMAGE;
                play_sound(SOUND_BLOCK);
            }
            else
            {
                target_player->health -= FIRE_PROJECTILE_DAMAGE;
                play_sound(SOUND_HIT);
            }
            if (target_player->health < 0) target_player->health = 0;
            projectiles[i].active = false;
        }
    }
}

static void draw_projectiles(BITMAP* buffer)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        if (!projectiles[i].active) continue;
        
        int x = (int)projectiles[i].x;
        int y = (int)projectiles[i].y;
        
        // FIRE projectile - fireball
        if (projectiles[i].type == 0)
        {
            // Draw fireball with glow effect
            circlefill(buffer, x, y, 12, makecol(255, 100, 0));
            circlefill(buffer, x, y, 8, makecol(255, 200, 0));
            circle(buffer, x, y, 12, makecol(255, 150, 0));
        }
        
        // Draw debug hitbox
        draw_debug_box(buffer, projectiles[i].hitbox, makecol(255, 0, 255));
    }
}

// Key mapping for players
// Player 1 keys
static int p1_up_key = KEY_W;
static int p1_down_key = KEY_S;
static int p1_left_key = KEY_A;
static int p1_right_key = KEY_D;
static int p1_bt1_key = KEY_J;
static int p1_bt2_key = KEY_K;
static int p1_bt3_key = KEY_L;
static int p1_bt4_key = KEY_U;
static int p1_bt5_key = KEY_I;
static int p1_bt6_key = KEY_O;
static int p1_select_key = KEY_1;
static int p1_start_key = KEY_ENTER;

// Player 2 keys
static int p2_up_key = KEY_UP;
static int p2_down_key = KEY_DOWN;
static int p2_left_key = KEY_LEFT;
static int p2_right_key = KEY_RIGHT;
static int p2_bt1_key = KEY_1_PAD;
static int p2_bt2_key = KEY_2_PAD;
static int p2_bt3_key = KEY_3_PAD;
static int p2_bt4_key = KEY_4_PAD;
static int p2_bt5_key = KEY_5_PAD;
static int p2_bt6_key = KEY_6_PAD;
static int p2_select_key = KEY_7_PAD;
static int p2_start_key = KEY_8_PAD;

// Initialize player state
static void init_player(Player* p, int player_num)
{
    p->x = (player_num == 0) ? 150.0f : 490.0f;
    p->y = 350.0f;
    p->vx = 0.0f;
    p->vy = 0.0f;
    p->health = 100;
    p->state = 0;
    p->anim_frame = 0;
    p->anim_timer = 0;
    p->facing = (player_num == 0) ? 1 : -1;
    p->on_ground = true;
    p->is_blocking = false;
    p->is_crouching = false;
    p->special_move_cooldown = 0;
    p->is_dashing = false;
    p->dash_timer = 0;
    p->attack_frame = 0;
    // character_id is preserved from selection
}

// Execute special move for a player
static void execute_special_move(Player* player, Player* opponent, int player_num)
{
    play_sound(SOUND_SPECIAL); // Special move sound
    
    switch (player->character_id)
    {
        case 0: // FIRE - Fireball projectile
            spawn_projectile(player_num, 0, player->x + 30.0f * player->facing, player->y, 
                           8.0f * player->facing, 0.0f);
            break;
            
        case 1: // WATER - Healing wave
            player->health += WATER_HEAL_AMOUNT;
            if (player->health > 100) player->health = 100;
            break;
            
        case 2: // EARTH - Ground stomp
            {
                float dist = fabs(player->x - opponent->x);
                if (dist < EARTH_STOMP_RANGE && opponent->on_ground && opponent->health > 0)
                {
                    if (opponent->is_blocking)
                    {
                        opponent->health -= BLOCKED_DAMAGE;
                        play_sound(SOUND_BLOCK);
                    }
                    else
                    {
                        opponent->health -= EARTH_STOMP_DAMAGE;
                        play_sound(SOUND_HIT);
                    }
                    if (opponent->health < 0) opponent->health = 0;
                }
            }
            break;
            
        case 3: // WIND - Dash attack
            player->is_dashing = true;
            player->dash_timer = WIND_DASH_DURATION;
            // Check for dash hit
            {
                float dist = fabs(player->x - opponent->x);
                if (dist < WIND_DASH_HIT_RANGE && opponent->health > 0)
                {
                    if (opponent->is_blocking)
                    {
                        opponent->health -= BLOCKED_DAMAGE;
                        play_sound(SOUND_BLOCK);
                    }
                    else
                    {
                        opponent->health -= WIND_DASH_DAMAGE;
                        play_sound(SOUND_HIT);
                    }
                    if (opponent->health < 0) opponent->health = 0;
                }
            }
            break;
    }
    
    player->special_move_cooldown = SPECIAL_MOVE_COOLDOWN;
}

// Draw a simple fighter sprite with character color
static void draw_player(BITMAP* dest, Player* p)
{
    int x = (int)p->x;
    int y = (int)p->y;
    
    // Get character color for tinting/fallback
    int color = makecol(char_colors[p->character_id][0], 
                        char_colors[p->character_id][1], 
                        char_colors[p->character_id][2]);
    
    // Try to get sprite frame (only if sprite animations are enabled)
    BITMAP* sprite = use_sprite_animations ? get_sprite_frame(p) : NULL;
    
    if (sprite)
    {
        // Draw sprite
        int sprite_x = x - (sprite->w / 2);
        int sprite_y = y - sprite->h;
        
        // Draw sprite with horizontal flip for left-facing characters
        if (p->facing < 0)
        {
            // Create temporary flipped bitmap
            BITMAP* flipped = create_bitmap(sprite->w, sprite->h);
            if (flipped)
            {
                clear_to_color(flipped, makecol(255, 0, 255));  // Transparent color
                // Manually flip horizontally
                for (int sy = 0; sy < sprite->h; sy++)
                {
                    for (int sx = 0; sx < sprite->w; sx++)
                    {
                        putpixel(flipped, sprite->w - 1 - sx, sy, getpixel(sprite, sx, sy));
                    }
                }
                draw_sprite(dest, flipped, sprite_x, sprite_y);
                destroy_bitmap(flipped);
            }
            else
            {
                // Fallback if flipped bitmap creation fails
                draw_sprite(dest, sprite, sprite_x, sprite_y);
            }
        }
        else
        {
            draw_sprite(dest, sprite, sprite_x, sprite_y);
        }
        
        // Apply character color tint by drawing a semi-transparent overlay
        // This preserves sprite details while adding character color
        if (p->is_blocking)
        {
            // Darker tint when blocking
            int dark_color = makecol(char_colors[p->character_id][0] / BLOCKING_COLOR_DIVISOR, 
                                      char_colors[p->character_id][1] / BLOCKING_COLOR_DIVISOR, 
                                      char_colors[p->character_id][2] / BLOCKING_COLOR_DIVISOR);
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
            set_trans_blender(0, 0, 0, 128);
            rectfill(dest, sprite_x, sprite_y, sprite_x + sprite->w, sprite_y + sprite->h, dark_color);
            solid_mode();
        }
        
        // Draw shield in front if blocking
        if (p->is_blocking)
        {
            int shield_x = x + (p->facing * 30);
            int shield_y = y - 40;
            int shield_color = makecol(150, 150, 255);
            circlefill(dest, shield_x, shield_y, 15, shield_color);
            circle(dest, shield_x, shield_y, 16, makecol(255, 255, 255));
            circle(dest, shield_x, shield_y, 17, makecol(255, 255, 255));
        }
    }
    else
    {
        // Fallback to geometric shapes if sprites not loaded
        if (p->is_blocking)
        {
            int dark_color = makecol(char_colors[p->character_id][0] / BLOCKING_COLOR_DIVISOR, 
                                      char_colors[p->character_id][1] / BLOCKING_COLOR_DIVISOR, 
                                      char_colors[p->character_id][2] / BLOCKING_COLOR_DIVISOR);
            rectfill(dest, x - 15, y - 50, x + 15, y, dark_color);
            circlefill(dest, x, y - 60, 10, dark_color);
            
            int shield_x = x + (p->facing * 20);
            int shield_color = makecol(150, 150, 255);
            circlefill(dest, shield_x, y - 30, 15, shield_color);
            circle(dest, shield_x, y - 30, 16, makecol(255, 255, 255));
            circle(dest, shield_x, y - 30, 17, makecol(255, 255, 255));
        }
        else
        {
            rectfill(dest, x - 15, y - 50, x + 15, y, color);
            circlefill(dest, x, y - 60, 10, color);
        }
        
        int dir = p->facing;
        line(dest, x, y - 60, x + dir * 20, y - 60, makecol(255, 255, 0));
    }
    
    // Draw health bar above player
    int bar_width = 60;
    int health_width = (p->health * bar_width) / 100;
    rect(dest, x - 30, y - 80, x + 30, y - 75, makecol(255, 255, 255));
    rectfill(dest, x - 30, y - 80, x - 30 + health_width, y - 75, makecol(0, 255, 0));
    
    // Draw special effects
    if (p->is_dashing)
    {
        int dir = p->facing;
        for (int i = 1; i <= 3; i++)
        {
            int offset = i * 15;
            line(dest, x - dir * offset, y - 30, x - dir * offset, y + 20, 
                 makecol(200, 200, 255));
            line(dest, x - dir * offset, y, x - dir * offset, y + 40, 
                 makecol(150, 150, 255));
        }
    }
    
    // Draw debug collision boxes if enabled
    draw_debug_box(dest, get_body_box(p), makecol(255, 255, 0));      // Yellow for body
    draw_debug_box(dest, get_hurtbox(p), makecol(0, 255, 0));         // Green for hurtbox
    draw_debug_box(dest, get_hitbox(p), makecol(255, 0, 0));          // Red for hitbox
    draw_debug_box(dest, get_clash_box(p), makecol(255, 165, 0));     // Orange for clash box
}

// Draw round indicators (circles for wins)
static void draw_round_indicators(BITMAP* dest)
{
    // P1 rounds (left side)
    int p1_x = 100;
    int y = 60;
    for (int i = 0; i < 3; i++)
    {
        int x = p1_x + i * 25;
        if (i < p1_rounds_won)
        {
            circlefill(dest, x, y, 8, makecol(255, 200, 100)); // Won rounds (filled)
        }
        else
        {
            circle(dest, x, y, 8, makecol(150, 150, 150)); // Unwon rounds (outline)
        }
    }
    
    // P2 rounds (right side)
    int p2_x = 540;
    for (int i = 0; i < 3; i++)
    {
        int x = p2_x - i * 25;
        if (i < p2_rounds_won)
        {
            circlefill(dest, x, y, 8, makecol(100, 200, 255)); // Won rounds (filled)
        }
        else
        {
            circle(dest, x, y, 8, makecol(150, 150, 150)); // Unwon rounds (outline)
        }
    }
    
    // Current round text
    char round_text[32];
    sprintf(round_text, "ROUND %d", current_round);
    textout_centre_ex(dest, font, round_text, 320, 55, makecol(255, 255, 255), -1);
}

// Draw character selection box
static void draw_character_box(BITMAP* dest, int char_id, int x, int y, bool selected, bool ready)
{
    int color = makecol(char_colors[char_id][0], 
                        char_colors[char_id][1], 
                        char_colors[char_id][2]);
    
    // Draw character preview
    rectfill(dest, x, y, x + 80, y + 100, color);
    
    // Draw character body in box
    rectfill(dest, x + 25, y + 40, x + 55, y + 80, color);
    circlefill(dest, x + 40, y + 30, 8, color);
    
    // Draw selection border
    if (selected)
    {
        rect(dest, x - 2, y - 2, x + 82, y + 102, makecol(255, 255, 255));
        rect(dest, x - 3, y - 3, x + 83, y + 103, makecol(255, 255, 255));
    }
    
    // Draw ready indicator
    if (ready)
    {
        textout_centre_ex(dest, font, "READY!", x + 40, y + 85, makecol(255, 255, 255), -1);
    }
    
    // Draw character name
    const char* names[] = {"FIRE", "WATER", "EARTH", "WIND"};
    textout_centre_ex(dest, font, names[char_id], x + 40, y - 12, makecol(255, 255, 255), -1);
}

// Load backgrounds from backgrounds/ directory
static void load_backgrounds()
{
    if (backgrounds_initialized) return;
    
    background_count = 0;
    
    // Try to load Background1, Background2, etc.
    for (int i = 1; i <= MAX_BACKGROUNDS; i++)
    {
        char dir_name[256];
        char config_path[256];
        char image_path[256];
        
        snprintf(dir_name, sizeof(dir_name), "backgrounds/Background%d", i);
        snprintf(config_path, sizeof(config_path), "%s/config.ini", dir_name);
        snprintf(image_path, sizeof(image_path), "%s/000_00.pcx", dir_name);
        
        // Check if config file exists
        PACKFILE* test_file = pack_fopen(config_path, "r");
        if (!test_file) continue;
        pack_fclose(test_file);
        
        // Load config.ini using Allegro config functions
        set_config_file(config_path);
        
        Background* bg = &backgrounds[background_count];
        bg->map_pos_x = get_config_int("DATA", "MapPosX", 0);
        bg->map_pos_y = get_config_int("DATA", "MapPosY", 0);
        
        // Load background image
        bg->image = load_bitmap(image_path, NULL);
        if (bg->image)
        {
            bg->loaded = true;
            snprintf(bg->name, sizeof(bg->name), "Background%d", i);
            background_count++;
        }
        else
        {
            bg->loaded = false;
        }
    }
    
    backgrounds_initialized = true;
}

// Free all loaded backgrounds
static void free_backgrounds()
{
    for (int i = 0; i < background_count; i++)
    {
        if (backgrounds[i].loaded && backgrounds[i].image)
        {
            destroy_bitmap(backgrounds[i].image);
            backgrounds[i].image = NULL;
            backgrounds[i].loaded = false;
        }
    }
    background_count = 0;
    backgrounds_initialized = false;
}

// INI Character Configuration Loading System
static void load_char_ini(int char_id, const char* char_name)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "chars/%s/char.ini", char_name);
    
    PACKFILE* fp = pack_fopen(filepath, F_READ_PACKED);
    if (!fp)
    {
        fprintf(stderr, "char.ini not found for %s, using defaults\n", char_name);
        return;
    }
    
    CharacterConfig* config = &character_configs[char_id];
    config->animation_count = 0;
    
    // Parse char.ini file manually (simple INI parser)
    char line[256];
    int current_state_id = -1;
    AnimationConfig* current_anim = NULL;
    
    while (pack_fgets(line, sizeof(line), fp))
    {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\0' || *start == '\n' || *start == ';' || *start == '#') continue;
        
        // Check for section header [NNN]
        if (*start == '[')
        {
            int state_id;
            if (sscanf(start, "[%d]", &state_id) == 1)
            {
                current_state_id = state_id;
                if (config->animation_count < MAX_CHAR_ANIMATIONS)
                {
                    current_anim = &config->animations[config->animation_count++];
                    current_anim->state_id = state_id;
                    current_anim->frame_count = 0;
                    current_anim->hspeed = 0.0f;
                    current_anim->vspeed = 0.0f;
                    current_anim->gravity = 0.5f;
                    current_anim->xalign = 0;
                    current_anim->yalign = 0;
                }
            }
        }
        else if (current_anim)
        {
            // Parse key=value pairs
            char key[64], value[64];
            if (sscanf(start, "%[^=]=%s", key, value) == 2)
            {
                if (strcmp(key, "XAlign") == 0)
                    current_anim->xalign = atoi(value);
                else if (strcmp(key, "YAlign") == 0)
                    current_anim->yalign = atoi(value);
                else if (strcmp(key, "Hspeed") == 0)
                    current_anim->hspeed = atof(value);
                else if (strcmp(key, "Vspeed") == 0)
                    current_anim->vspeed = atof(value);
                else if (strcmp(key, "Gravity") == 0)
                    current_anim->gravity = atof(value);
                else if (strncmp(key, "FrameTime_", 10) == 0)
                {
                    int frame_num = atoi(key + 10);
                    if (frame_num < MAX_ANIM_FRAMES)
                    {
                        current_anim->frame_times[frame_num] = atoi(value);
                        if (frame_num >= current_anim->frame_count)
                            current_anim->frame_count = frame_num + 1;
                    }
                }
            }
        }
    }
    
    pack_fclose(fp);
    config->loaded = true;
    fprintf(stderr, "Loaded char.ini for %s: %d animations\n", char_name, config->animation_count);
}

static void load_chbox_ini(int char_id, const char* char_name)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "chars/%s/chbox.ini", char_name);
    
    PACKFILE* fp = pack_fopen(filepath, F_READ_PACKED);
    if (!fp)
    {
        fprintf(stderr, "chbox.ini not found for %s, using defaults\n", char_name);
        return;
    }
    
    CharacterConfig* config = &character_configs[char_id];
    config->collision_box_count = 0;
    
    char line[256];
    int current_state_id = -1;
    int current_frame = -1;
    CollisionBoxConfig* current_box_config = NULL;
    
    while (pack_fgets(line, sizeof(line), fp))
    {
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\0' || *start == '\n' || *start == ';' || *start == '#') continue;
        
        // Check for section header [NNN_FF]
        if (*start == '[')
        {
            int state_id, frame;
            if (sscanf(start, "[%d_%d]", &state_id, &frame) == 2)
            {
                current_state_id = state_id;
                current_frame = frame;
                if (config->collision_box_count < 100)
                {
                    current_box_config = &config->collision_boxes[config->collision_box_count++];
                    current_box_config->state_id = state_id;
                    current_box_config->frame = frame;
                    current_box_config->hurtbox_count = 0;
                    current_box_config->hitbox_count = 0;
                }
            }
        }
        else if (current_box_config)
        {
            // Parse collision box coordinates
            char key[64];
            int x1, y1, x2, y2;
            if (sscanf(start, "%[^=]=%d,%d,%d,%d", key, &x1, &y1, &x2, &y2) == 5)
            {
                CollisionBox box;
                box.x = x1;
                box.y = y1;
                box.w = x2 - x1;
                box.h = y2 - y1;
                
                if (strncmp(key, "HurtBox", 7) == 0 && current_box_config->hurtbox_count < MAX_COLLISION_BOXES)
                {
                    current_box_config->hurtboxes[current_box_config->hurtbox_count++] = box;
                }
                else if (strncmp(key, "HitBox", 6) == 0 && current_box_config->hitbox_count < MAX_COLLISION_BOXES)
                {
                    current_box_config->hitboxes[current_box_config->hitbox_count++] = box;
                }
            }
        }
    }
    
    pack_fclose(fp);
    fprintf(stderr, "Loaded chbox.ini for %s: %d box configs\n", char_name, config->collision_box_count);
}

static void load_special_ini(int char_id, const char* char_name)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "chars/%s/special.ini", char_name);
    
    PACKFILE* fp = pack_fopen(filepath, F_READ_PACKED);
    if (!fp)
    {
        fprintf(stderr, "special.ini not found for %s, using defaults\n", char_name);
        return;
    }
    
    CharacterConfig* config = &character_configs[char_id];
    config->special_move_count = 0;
    
    char line[256];
    SpecialMoveConfig* current_special = NULL;
    
    while (pack_fgets(line, sizeof(line), fp))
    {
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\0' || *start == '\n' || *start == ';' || *start == '#') continue;
        
        // Check for section header [NNN]
        if (*start == '[')
        {
            int special_id;
            if (sscanf(start, "[%d]", &special_id) == 1)
            {
                if (config->special_move_count < 10)
                {
                    current_special = &config->special_moves[config->special_move_count++];
                    current_special->command_count = 0;
                    current_special->damage = 0;
                    current_special->type = 0;
                    strcpy(current_special->name, "Special");
                }
            }
        }
        else if (current_special)
        {
            char key[64], value[128];
            if (sscanf(start, "%[^=]=%[^\n]", key, value) == 2)
            {
                if (strcmp(key, "name") == 0)
                {
                    strncpy(current_special->name, value, sizeof(current_special->name) - 1);
                }
                else if (strncmp(key, "c", 1) == 0 && strlen(key) > 1)
                {
                    // Command sequence (c1, c2, c3, ...)
                    int cmd_num = atoi(key + 1);
                    if (cmd_num > 0 && cmd_num <= 10)
                    {
                        current_special->command_sequence[cmd_num - 1] = atoi(value);
                        if (cmd_num > current_special->command_count)
                            current_special->command_count = cmd_num;
                    }
                }
                else if (strcmp(key, "V1_Damage") == 0 || strcmp(key, "V2_Damage") == 0 || strcmp(key, "V3_Damage") == 0)
                {
                    current_special->damage = atoi(value);
                }
            }
        }
    }
    
    pack_fclose(fp);
    fprintf(stderr, "Loaded special.ini for %s: %d special moves\n", char_name, config->special_move_count);
}

static void load_character_config(int char_id)
{
    const char* char_names[] = {"CharTemplate", "CharTemplate", "CharTemplate", "CharTemplate"};
    if (char_id < 0 || char_id >= 4) return;
    
    character_configs[char_id].loaded = false;
    character_configs[char_id].animation_count = 0;
    character_configs[char_id].collision_box_count = 0;
    character_configs[char_id].special_move_count = 0;
    
    load_char_ini(char_id, char_names[char_id]);
    load_chbox_ini(char_id, char_names[char_id]);
    load_special_ini(char_id, char_names[char_id]);
}

static void init_character_configs()
{
    for (int i = 0; i < 4; i++)
    {
        load_character_config(i);
    }
}

// Draw stage background based on characters
static void draw_stage_background(BITMAP* dest, int p1_char, int p2_char)
{
    // Determine stage theme based on P1's character (simpler than blending two themes)
    int stage_theme = p1_char;
    
    // Animated background frame (only increments when drawing stages)
    stage_animation_frame++;
    if (stage_animation_frame >= 360) stage_animation_frame = 0;
    
    // Check if we have a loaded background for this stage
    if (backgrounds_initialized && stage_theme < background_count && backgrounds[stage_theme].loaded)
    {
        // Use dynamic background from config.ini
        Background* bg = &backgrounds[stage_theme];
        
        // Draw background image at configured position
        // Apply scrolling animation based on MapPos values
        int draw_x = bg->map_pos_x + (stage_animation_frame / 10);
        int draw_y = bg->map_pos_y;
        
        // Draw the background image (may be larger than screen)
        blit(bg->image, dest, 0, 0, draw_x, draw_y, 640, 480);
        
        // Draw ground line
        hline(dest, 0, 400, 640, makecol(100, 70, 30));
        return;
    }
    
    // Fallback to procedural backgrounds if no custom background loaded
    // Animation constants
    const int CLOUD_SPACING = 120;
    const int CLOUD_WRAP = 1280;
    const int CLOUD_SCREEN_WIDTH = 800;
    const int CLOUD_OFFSET = 100;
    
    // Sky/background layer
    switch (stage_theme)
    {
        case 0: // FIRE stage - Volcano/Lava
            {
                // Red-orange gradient sky
                for (int y = 0; y < 300; y++)
                {
                    int r = 180 + (y * 75 / 300);
                    int g = 50 + (y * 30 / 300);
                    int b = 20;
                    hline(dest, 0, y, 640, makecol(r, g, b));
                }
                
                // Distant mountains (dark)
                // Pre-calculate sine values for better performance
                for (int x = 0; x < 640; x += 4)
                {
                    int height = 250 + (int)(20 * sin((x + stage_animation_frame) * 0.02f));
                    // Fill 4 pixels at once for performance
                    for (int px = x; px < x + 4 && px < 640; px++)
                    {
                        vline(dest, px, height, 300, makecol(60, 20, 10));
                    }
                }
                
                // Lava glow effect (animated)
                int glow = 200 + (int)(30 * sin(stage_animation_frame * 0.1f));
                int glow_dim = (glow > 20) ? glow - 20 : 0; // Clamp to prevent negative values
                hline(dest, 0, 395, 640, makecol(glow, 100, 30));
                hline(dest, 0, 396, 640, makecol(glow_dim, 80, 20));
            }
            break;
            
        case 1: // WATER stage - Ocean/Beach
            {
                // Blue gradient sky
                for (int y = 0; y < 300; y++)
                {
                    int r = 100 + (y * 55 / 300);
                    int g = 150 + (y * 55 / 300);
                    int b = 220 - (y * 20 / 300);
                    hline(dest, 0, y, 640, makecol(r, g, b));
                }
                
                // Ocean waves (animated)
                for (int x = 0; x < 640; x++)
                {
                    int wave1 = 200 + (int)(15 * sin((x + stage_animation_frame) * 0.03f));
                    int wave2 = 240 + (int)(10 * sin((x + stage_animation_frame * 1.5f) * 0.04f));
                    
                    vline(dest, x, wave1, wave2, makecol(60, 100, 180));
                    vline(dest, x, wave2, 300, makecol(40, 80, 150));
                }
                
                // Beach/sand
                rectfill(dest, 0, 300, 640, 400, makecol(220, 200, 140));
            }
            break;
            
        case 2: // EARTH stage - Forest
            {
                // Green-blue sky
                for (int y = 0; y < 300; y++)
                {
                    int r = 120 - (y * 20 / 300);
                    int g = 180 - (y * 30 / 300);
                    int b = 140 - (y * 40 / 300);
                    hline(dest, 0, y, 640, makecol(r, g, b));
                }
                
                // Distant trees (dark green)
                for (int i = 0; i < 20; i++)
                {
                    int x = i * 35 + ((stage_animation_frame / 2) % 35);
                    int y = 220 + (i % 3) * 10;
                    triangle(dest, x, y, x - 15, y + 60, x + 15, y + 60, makecol(30, 80, 30));
                }
                
                // Grass ground
                rectfill(dest, 0, 300, 640, 400, makecol(80, 140, 60));
                
                // Grass blades (simple details)
                for (int i = 0; i < 40; i++)
                {
                    int x = (i * 16 + stage_animation_frame) % 640;
                    vline(dest, x, 380, 385, makecol(100, 160, 80));
                }
            }
            break;
            
        case 3: // WIND stage - Sky/Clouds
            {
                // Light blue sky gradient
                for (int y = 0; y < 300; y++)
                {
                    int r = 150 + (y * 55 / 300);
                    int g = 200 + (y * 35 / 300);
                    int b = 255 - (y * 25 / 300);
                    hline(dest, 0, y, 640, makecol(r, g, b));
                }
                
                // Floating clouds (animated)
                for (int i = 0; i < 6; i++)
                {
                    int x = ((i * CLOUD_SPACING) - stage_animation_frame + CLOUD_WRAP) % CLOUD_SCREEN_WIDTH - CLOUD_OFFSET;
                    int y = 80 + i * 30;
                    
                    // Cloud puffs
                    circlefill(dest, x, y, 25, makecol(255, 255, 255));
                    circlefill(dest, x + 20, y, 20, makecol(255, 255, 255));
                    circlefill(dest, x + 40, y, 25, makecol(255, 255, 255));
                    circlefill(dest, x - 20, y, 20, makecol(255, 255, 255));
                }
                
                // Distant platforms/mountains
                for (int i = 0; i < 8; i++)
                {
                    int x = i * 90 + ((stage_animation_frame / 3) % 90);
                    int y = 260 + (i % 2) * 20;
                    rectfill(dest, x - 40, y, x + 40, y + 10, makecol(180, 180, 200));
                }
                
                // Ground platform
                rectfill(dest, 0, 300, 640, 310, makecol(200, 200, 220));
            }
            break;
    }
    
    // Draw ground line (common for all stages)
    hline(dest, 0, 400, 640, makecol(80, 80, 80));
}

void hamoopi_init(void)
{
    if (initialized)
        return;
    
    // Create screen buffer for rendering
    screen_buffer = create_bitmap(640, 480);
    game_buffer = create_bitmap(640, 480);
    
    if (!screen_buffer || !game_buffer)
    {
        fprintf(stderr, "Failed to create screen buffers\n");
        return;
    }
    
    clear_to_color(screen_buffer, makecol(0, 0, 0));
    clear_to_color(game_buffer, makecol(0, 0, 0));
    
    // Override the global Allegro 'screen' variable
    screen = screen_buffer;
    
    // Initialize keyboard state
    install_keyboard();
    
    // Initialize game font
    game_font = font;
    
    // Initialize sprite system
    init_sprite_system();
    
    // Load backgrounds from config files
    load_backgrounds();
    
    // Load character configurations from INI files
    init_character_configs();
    
    // Initialize players with default characters
    init_player(&players[0], 0);
    players[0].character_id = 0;
    init_player(&players[1], 1);
    players[1].character_id = 1;
    
    game_mode = 0; // Start at title screen
    frame_count = 0;
    
    // Reset character selection state
    p1_cursor = 0;
    p2_cursor = 1;
    p1_ready = false;
    p2_ready = false;
    
    // Initialize projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        projectiles[i].active = false;
    }
    
    initialized = true;
    running = false;
}

void hamoopi_deinit(void)
{
    if (!initialized)
        return;
    
    // Cleanup backgrounds
    free_backgrounds();
    
    // Cleanup sprite system
    cleanup_sprite_system();
    
    if (game_buffer)
    {
        destroy_bitmap(game_buffer);
        game_buffer = NULL;
    }
    
    if (screen_buffer)
    {
        destroy_bitmap(screen_buffer);
        screen_buffer = NULL;
    }
    
    screen = NULL;
    initialized = false;
    running = false;
}

void hamoopi_reset(void)
{
    // Reset game state
    init_player(&players[0], 0);
    players[0].character_id = 0;
    init_player(&players[1], 1);
    players[1].character_id = 1;
    
    game_mode = 0;
    frame_count = 0;
    
    // Reset character selection state
    p1_cursor = 0;
    p2_cursor = 1;
    p1_ready = false;
    p2_ready = false;
    
    if (screen_buffer)
        clear_to_color(screen_buffer, makecol(0, 0, 0));
    if (game_buffer)
        clear_to_color(game_buffer, makecol(0, 0, 0));
}

void hamoopi_run_frame(void)
{
    if (!initialized || !screen_buffer || !game_buffer)
        return;
    
    frame_count++;
    
    // Update the Allegro key array based on hamoopi_input state
    key[p1_up_key] = hamoopi_input[0].up;
    key[p1_down_key] = hamoopi_input[0].down;
    key[p1_left_key] = hamoopi_input[0].left;
    key[p1_right_key] = hamoopi_input[0].right;
    key[p1_bt1_key] = hamoopi_input[0].a;
    key[p1_bt2_key] = hamoopi_input[0].b;
    key[p1_bt3_key] = hamoopi_input[0].y;
    key[p1_bt4_key] = hamoopi_input[0].x;
    key[p1_bt5_key] = hamoopi_input[0].l;
    key[p1_bt6_key] = hamoopi_input[0].r;
    key[p1_select_key] = hamoopi_input[0].select;
    key[p1_start_key] = hamoopi_input[0].start;
    
    key[p2_up_key] = hamoopi_input[1].up;
    key[p2_down_key] = hamoopi_input[1].down;
    key[p2_left_key] = hamoopi_input[1].left;
    key[p2_right_key] = hamoopi_input[1].right;
    key[p2_bt1_key] = hamoopi_input[1].a;
    key[p2_bt2_key] = hamoopi_input[1].b;
    key[p2_bt3_key] = hamoopi_input[1].y;
    key[p2_bt4_key] = hamoopi_input[1].x;
    key[p2_bt5_key] = hamoopi_input[1].l;
    key[p2_bt6_key] = hamoopi_input[1].r;
    key[p2_select_key] = hamoopi_input[1].select;
    key[p2_start_key] = hamoopi_input[1].start;
    
    // Clear game buffer
    clear_to_color(game_buffer, makecol(20, 40, 80));
    
    // Game mode logic
    if (game_mode == 0)
    {
        // Title screen
        textout_centre_ex(game_buffer, game_font, "HAMOOPI", 320, 150, makecol(255, 255, 255), -1);
        textout_centre_ex(game_buffer, game_font, "Libretro Core - Fighting Game Demo", 320, 180, makecol(200, 200, 200), -1);
        textout_centre_ex(game_buffer, game_font, "Press START to begin", 320, 240, makecol(150, 200, 150), -1);
        textout_centre_ex(game_buffer, game_font, "Player 1: WASD + JKL", 320, 300, makecol(150, 150, 200), -1);
        textout_centre_ex(game_buffer, game_font, "Player 2: Arrows + Numpad", 320, 320, makecol(150, 150, 200), -1);
        
        if (key[p1_start_key] || key[p2_start_key])
        {
            game_mode = 1; // Go to character select
            p1_ready = false;
            p2_ready = false;
        }
    }
    else if (game_mode == 1)
    {
        // Character selection screen
        textout_centre_ex(game_buffer, game_font, "SELECT YOUR FIGHTER", 320, 30, makecol(255, 255, 255), -1);
        
        // Draw character selection boxes
        int start_x = 120;
        int start_y = 100;
        int spacing = 100;
        
        for (int i = 0; i < NUM_CHARACTERS; i++)
        {
            int x = start_x + (i * spacing);
            draw_character_box(game_buffer, i, x, start_y, 
                             (i == p1_cursor), p1_ready);
        }
        
        // Draw second row for Player 2
        for (int i = 0; i < NUM_CHARACTERS; i++)
        {
            int x = start_x + (i * spacing);
            draw_character_box(game_buffer, i, x, start_y + 150, 
                             (i == p2_cursor), p2_ready);
        }
        
        // Draw player labels
        textout_ex(game_buffer, game_font, "PLAYER 1", 50, start_y + 40, makecol(255, 100, 100), -1);
        textout_ex(game_buffer, game_font, "PLAYER 2", 50, start_y + 190, makecol(100, 100, 255), -1);
        
        // Instructions
        textout_centre_ex(game_buffer, game_font, "Left/Right to select, A to confirm", 320, 420, makecol(200, 200, 200), -1);
        
        // Player 1 input (only if not ready)
        if (!p1_ready)
        {
            if (key[p1_left_key] && !p1_left_pressed)
            {
                p1_cursor = (p1_cursor - 1 + NUM_CHARACTERS) % NUM_CHARACTERS;
                p1_left_pressed = true;
            }
            if (!key[p1_left_key]) p1_left_pressed = false;
            
            if (key[p1_right_key] && !p1_right_pressed)
            {
                p1_cursor = (p1_cursor + 1) % NUM_CHARACTERS;
                p1_right_pressed = true;
            }
            if (!key[p1_right_key]) p1_right_pressed = false;
            
            if (key[p1_bt1_key] && !p1_a_pressed)
            {
                p1_ready = true;
                players[0].character_id = p1_cursor;
                p1_a_pressed = true;
            }
            if (!key[p1_bt1_key]) p1_a_pressed = false;
        }
        
        // Player 2 input (only if not ready)
        if (!p2_ready)
        {
            if (key[p2_left_key] && !p2_left_pressed)
            {
                p2_cursor = (p2_cursor - 1 + NUM_CHARACTERS) % NUM_CHARACTERS;
                p2_left_pressed = true;
            }
            if (!key[p2_left_key]) p2_left_pressed = false;
            
            if (key[p2_right_key] && !p2_right_pressed)
            {
                p2_cursor = (p2_cursor + 1) % NUM_CHARACTERS;
                p2_right_pressed = true;
            }
            if (!key[p2_right_key]) p2_right_pressed = false;
            
            if (key[p2_bt1_key] && !p2_a_pressed)
            {
                p2_ready = true;
                players[1].character_id = p2_cursor;
                p2_a_pressed = true;
            }
            if (!key[p2_bt1_key]) p2_a_pressed = false;
        }
        
        // Both players ready - start fight
        if (p1_ready && p2_ready)
        {
            game_mode = 2; // Go to fight
            init_player(&players[0], 0);
            players[0].character_id = p1_cursor;
            init_player(&players[1], 1);
            players[1].character_id = p2_cursor;
            
            // Load sprites for selected characters
            load_character_sprites(p1_cursor);
            load_character_sprites(p2_cursor);
            
            // Reset round system for new match
            p1_rounds_won = 0;
            p2_rounds_won = 0;
            current_round = 1;
            round_transition_timer = 0;
        }
    }
    else if (game_mode == 2)
    {
        // Fighting game logic
        
        // Toggle debug boxes with SELECT button (P1 only)
        // Toggle sprite animations with SELECT + START combo (P1 only)
        static bool select_pressed = false;
        static bool combo_pressed = false;
        
        bool select_down = key[p1_select_key];
        bool start_down = key[p1_start_key];
        
        if (select_down && start_down)
        {
            // SELECT + START combo: toggle sprite animations
            if (!combo_pressed)
            {
                use_sprite_animations = !use_sprite_animations;
                combo_pressed = true;
                select_pressed = true;  // Prevent single SELECT from triggering
            }
        }
        else if (select_down && !start_down)
        {
            // Just SELECT: toggle debug boxes
            if (!select_pressed && !combo_pressed)
            {
                show_debug_boxes = !show_debug_boxes;
                select_pressed = true;
            }
        }
        else
        {
            // Release
            select_pressed = false;
            combo_pressed = false;
        }
        
        // Draw stage background
        draw_stage_background(game_buffer, players[0].character_id, players[1].character_id);
        
        // Update Player 1
        Player* p1 = &players[0];
        static int p1_attack_cooldown = 0;
        if (p1_attack_cooldown > 0) p1_attack_cooldown--;
        
        if (p1->health > 0)
        {
            // Check if crouching (DOWN button)
            p1->is_crouching = key[p1_down_key] && p1->on_ground;
            
            // Check if blocking (B button / bt2)
            bool was_blocking = p1->is_blocking;
            p1->is_blocking = key[p1_bt2_key];
            
            // Block sound effect (when starting to block)
            if (p1->is_blocking && !was_blocking)
            {
                play_sound(SOUND_BLOCK);
            }
            
            // Movement (slower when blocking, can't move horizontally when crouching)
            if (!p1->is_crouching)
            {
                float speed_multiplier = p1->is_blocking ? BLOCKING_SPEED_MULTIPLIER : 1.0f;
                if (key[p1_left_key]) { p1->vx = -3.0f * speed_multiplier; p1->facing = -1; }
                else if (key[p1_right_key]) { p1->vx = 3.0f * speed_multiplier; p1->facing = 1; }
                else { p1->vx *= 0.8f; }
            }
            else
            {
                // Can't move horizontally while crouching
                p1->vx *= 0.8f;
            }
            
            // Jump (can't jump while blocking or crouching)
            if (key[p1_up_key] && p1->on_ground && !p1->is_blocking && !p1->is_crouching)
            {
                p1->vy = -12.0f;
                p1->on_ground = false;
                play_sound(SOUND_JUMP); // Jump sound effect
            }
            
            // Attack with cooldown (can't attack while blocking)
            if (key[p1_bt1_key] && p1_attack_cooldown == 0 && !p1->is_blocking)
            {
                play_sound(SOUND_ATTACK); // Attack sound effect
                if (p1->is_crouching)
                {
                    p1->state = 6; // Crouch attack state
                }
                else
                {
                    p1->state = 3; // Normal attack state
                }
                p1->attack_frame = 0; // Start attack animation
                p1_attack_cooldown = 15; // 15 frames cooldown (~0.25 seconds)
            }
            
            // Update attack animation
            if (p1->state == 3 || p1->state == 6)
            {
                p1->attack_frame++;
                if (p1->attack_frame >= 10) // Attack lasts 10 frames
                {
                    p1->state = 0; // Return to idle
                    p1->attack_frame = 0;
                }
                
                // Check hitbox collision during active frames
                if (p1->attack_frame >= 2 && p1->attack_frame <= 6)
                {
                    Player* p2 = &players[1];
                    if (p2->health > 0)
                    {
                        CollisionBox p1_hitbox = get_hitbox(p1);
                        CollisionBox p2_hurtbox = get_hurtbox(p2);
                        
                        // Check for hit
                        if (boxes_overlap(p1_hitbox, p2_hurtbox))
                        {
                            // Only apply damage once per attack
                            if (p1->attack_frame == 2)
                            {
                                if (p2->is_blocking)
                                {
                                    p2->health -= BLOCKED_DAMAGE;
                                    if (p2->health < 0) p2->health = 0;
                                    play_sound(SOUND_BLOCK);
                                }
                                else
                                {
                                    p2->health -= NORMAL_DAMAGE;
                                    if (p2->health < 0) p2->health = 0;
                                    play_sound(SOUND_HIT);
                                }
                            }
                        }
                    }
                }
            }
            
            // Special move (Y button / bt3)
            if (p1->special_move_cooldown > 0)
            {
                p1->special_move_cooldown--;
            }
            
            // Handle WIND dash
            if (p1->is_dashing)
            {
                p1->dash_timer--;
                p1->vx = WIND_DASH_SPEED * p1->facing;
                if (p1->dash_timer <= 0)
                {
                    p1->is_dashing = false;
                }
            }
            
            if (key[p1_bt3_key] && p1->special_move_cooldown == 0 && !p1->is_blocking)
            {
                Player* p2 = &players[1];
                execute_special_move(p1, p2, 0);
            }
            
            // Physics
            p1->vy += 0.5f; // Gravity
            p1->x += p1->vx;
            p1->y += p1->vy;
            
            // Collision with ground
            if (p1->y >= 350.0f)
            {
                p1->y = 350.0f;
                p1->vy = 0.0f;
                p1->on_ground = true;
            }
            
            // Boundary check
            if (p1->x < 20.0f) p1->x = 20.0f;
            if (p1->x > 620.0f) p1->x = 620.0f;
        }
        
        // Update Player 2
        Player* p2 = &players[1];
        static int p2_attack_cooldown = 0;
        if (p2_attack_cooldown > 0) p2_attack_cooldown--;
        
        if (p2->health > 0)
        {
            // Check if crouching (DOWN button)
            p2->is_crouching = key[p2_down_key] && p2->on_ground;
            
            // Check if blocking (B button / bt2)
            bool was_blocking = p2->is_blocking;
            p2->is_blocking = key[p2_bt2_key];
            
            // Block sound effect (when starting to block)
            if (p2->is_blocking && !was_blocking)
            {
                play_sound(SOUND_BLOCK);
            }
            
            // Movement (slower when blocking, can't move horizontally when crouching)
            if (!p2->is_crouching)
            {
                float speed_multiplier = p2->is_blocking ? BLOCKING_SPEED_MULTIPLIER : 1.0f;
                if (key[p2_left_key]) { p2->vx = -3.0f * speed_multiplier; p2->facing = -1; }
                else if (key[p2_right_key]) { p2->vx = 3.0f * speed_multiplier; p2->facing = 1; }
                else { p2->vx *= 0.8f; }
            }
            else
            {
                // Can't move horizontally while crouching
                p2->vx *= 0.8f;
            }
            
            // Jump (can't jump while blocking or crouching)
            if (key[p2_up_key] && p2->on_ground && !p2->is_blocking && !p2->is_crouching)
            {
                p2->vy = -12.0f;
                p2->on_ground = false;
                play_sound(SOUND_JUMP); // Jump sound effect
            }
            
            // Attack with cooldown (can't attack while blocking)
            if (key[p2_bt1_key] && p2_attack_cooldown == 0 && !p2->is_blocking)
            {
                play_sound(SOUND_ATTACK); // Attack sound effect
                if (p2->is_crouching)
                {
                    p2->state = 6; // Crouch attack state
                }
                else
                {
                    p2->state = 3; // Normal attack state
                }
                p2->attack_frame = 0; // Start attack animation
                p2_attack_cooldown = 15; // 15 frames cooldown (~0.25 seconds)
            }
            
            // Update attack animation
            if (p2->state == 3 || p2->state == 6)
            {
                p2->attack_frame++;
                if (p2->attack_frame >= 10) // Attack lasts 10 frames
                {
                    p2->state = 0; // Return to idle
                    p2->attack_frame = 0;
                }
                
                // Check hitbox collision during active frames
                if (p2->attack_frame >= 2 && p2->attack_frame <= 6)
                {
                    if (p1->health > 0)
                    {
                        CollisionBox p2_hitbox = get_hitbox(p2);
                        CollisionBox p1_hurtbox = get_hurtbox(p1);
                        
                        // Check for hit
                        if (boxes_overlap(p2_hitbox, p1_hurtbox))
                        {
                            // Only apply damage once per attack
                            if (p2->attack_frame == 2)
                            {
                                if (p1->is_blocking)
                                {
                                    p1->health -= BLOCKED_DAMAGE;
                                    if (p1->health < 0) p1->health = 0;
                                    play_sound(SOUND_BLOCK);
                                }
                                else
                                {
                                    p1->health -= NORMAL_DAMAGE;
                                    if (p1->health < 0) p1->health = 0;
                                    play_sound(SOUND_HIT);
                                }
                            }
                        }
                    }
                }
            }
            
            // Special move (Y button / bt3)
            if (p2->special_move_cooldown > 0)
            {
                p2->special_move_cooldown--;
            }
            
            // Handle WIND dash
            if (p2->is_dashing)
            {
                p2->dash_timer--;
                p2->vx = WIND_DASH_SPEED * p2->facing;
                if (p2->dash_timer <= 0)
                {
                    p2->is_dashing = false;
                }
            }
            
            if (key[p2_bt3_key] && p2->special_move_cooldown == 0 && !p2->is_blocking)
            {
                execute_special_move(p2, p1, 1);
            }
            
            // Physics
            p2->vy += 0.5f; // Gravity
            p2->x += p2->vx;
            p2->y += p2->vy;
            
            // Collision with ground
            if (p2->y >= 350.0f)
            {
                p2->y = 350.0f;
                p2->vy = 0.0f;
                p2->on_ground = true;
            }
            
            // Boundary check
            if (p2->x < 20.0f) p2->x = 20.0f;
            if (p2->x > 620.0f) p2->x = 620.0f;
        }
        
        // Body collision - prevent players from walking through each other
        CollisionBox p1_body = get_body_box(p1);
        CollisionBox p2_body = get_body_box(p2);
        if (boxes_overlap(p1_body, p2_body))
        {
            // Push players apart
            float push_force = 2.0f;
            if (p1->x < p2->x)
            {
                p1->x -= push_force;
                p2->x += push_force;
            }
            else
            {
                p1->x += push_force;
                p2->x -= push_force;
            }
        }
        
        // Update animation frames
        p1->anim_timer++;
        p2->anim_timer++;
        
        // Update animation frame every 5 game frames (12 FPS animation at 60 FPS game)
        if (p1->anim_timer >= 5)
        {
            p1->anim_timer = 0;
            p1->anim_frame++;
        }
        if (p2->anim_timer >= 5)
        {
            p2->anim_timer = 0;
            p2->anim_frame++;
        }
        
        // Update player states based on movement
        if (p1->health > 0)
        {
            if (p1->state != 3 && p1->state != 6 && !p1->is_blocking)  // Not attacking or blocking
            {
                if (!p1->on_ground)
                {
                    p1->state = 2;  // Jump
                }
                else if (p1->is_crouching)
                {
                    p1->state = 5;  // Crouch
                }
                else if (fabs(p1->vx) > 0.5f)
                {
                    p1->state = 1;  // Walk
                }
                else
                {
                    p1->state = 0;  // Idle
                }
            }
        }
        
        if (p2->health > 0)
        {
            if (p2->state != 3 && p2->state != 6 && !p2->is_blocking)  // Not attacking or blocking
            {
                if (!p2->on_ground)
                {
                    p2->state = 2;  // Jump
                }
                else if (p2->is_crouching)
                {
                    p2->state = 5;  // Crouch
                }
                else if (fabs(p2->vx) > 0.5f)
                {
                    p2->state = 1;  // Walk
                }
                else
                {
                    p2->state = 0;  // Idle
                }
            }
        }
        
        // Attack clashing - check if both players are attacking (normal or crouch attacks)
        if ((p1->state == 3 || p1->state == 6) && (p2->state == 3 || p2->state == 6))
        {
            CollisionBox p1_clash = get_clash_box(p1);
            CollisionBox p2_clash = get_clash_box(p2);
            
            if (boxes_overlap(p1_clash, p2_clash) && p1_clash.w > 0 && p2_clash.w > 0)
            {
                // Attacks clash! Cancel both attacks
                p1->state = 0;
                p1->attack_frame = 0;
                p2->state = 0;
                p2->attack_frame = 0;
                play_sound(SOUND_BLOCK); // Clash sound
                
                // Push players back slightly
                p1->vx = -4.0f * p1->facing;
                p2->vx = -4.0f * p2->facing;
            }
        }
        
        // Update projectiles
        update_projectiles();
        
        // Draw players
        draw_player(game_buffer, p1);
        draw_player(game_buffer, p2);
        
        // Draw projectiles
        draw_projectiles(game_buffer);
        
        // Draw HUD
        textout_ex(game_buffer, game_font, "P1", 50, 20, makecol(255, 100, 100), -1);
        char p1_health_str[32];
        sprintf(p1_health_str, "HP: %d", p1->health);
        textout_ex(game_buffer, game_font, p1_health_str, 50, 35, makecol(255, 255, 255), -1);
        
        // P1 Special move cooldown indicator
        if (p1->special_move_cooldown > 0)
        {
            int cooldown_width = (int)((float)p1->special_move_cooldown / SPECIAL_MOVE_COOLDOWN * 60.0f);
            rectfill(game_buffer, 50, 50, 50 + cooldown_width, 55, makecol(150, 150, 0));
        }
        else
        {
            textout_ex(game_buffer, game_font, "SPECIAL READY!", 50, 50, makecol(255, 255, 0), -1);
        }
        
        textout_ex(game_buffer, game_font, "P2", 550, 20, makecol(100, 100, 255), -1);
        char p2_health_str[32];
        sprintf(p2_health_str, "HP: %d", p2->health);
        textout_ex(game_buffer, game_font, p2_health_str, 550, 35, makecol(255, 255, 255), -1);
        
        // P2 Special move cooldown indicator
        if (p2->special_move_cooldown > 0)
        {
            int cooldown_width = (int)((float)p2->special_move_cooldown / SPECIAL_MOVE_COOLDOWN * 60.0f);
            rectfill(game_buffer, 550, 50, 550 + cooldown_width, 55, makecol(150, 150, 0));
        }
        else
        {
            textout_ex(game_buffer, game_font, "SPECIAL READY!", 550, 50, makecol(255, 255, 0), -1);
        }
        
        // Draw round indicators
        draw_round_indicators(game_buffer);
        
        // Debug info
        if (show_debug_boxes)
        {
            textout_ex(game_buffer, game_font, "DEBUG MODE - SELECT to toggle", 10, 460, makecol(255, 255, 0), -1);
            textout_ex(game_buffer, game_font, "Yellow=Body Green=Hurtbox Red=Hitbox Orange=Clash", 10, 470, makecol(255, 255, 255), -1);
        }
        
        // Display sprite animation status
        if (!use_sprite_animations)
        {
            textout_ex(game_buffer, game_font, "SPRITES OFF - SELECT+START to toggle", 200, 460, makecol(255, 128, 0), -1);
        }
        
        // Check for round winner
        if (round_transition_timer > 0)
        {
            // Display round result
            round_transition_timer--;
            
            if (p1->health <= 0)
            {
                textout_centre_ex(game_buffer, game_font, "ROUND OVER!", 320, 200, makecol(255, 255, 255), -1);
                textout_centre_ex(game_buffer, game_font, "PLAYER 2 WINS ROUND!", 320, 230, makecol(100, 200, 255), -1);
            }
            else
            {
                textout_centre_ex(game_buffer, game_font, "ROUND OVER!", 320, 200, makecol(255, 255, 255), -1);
                textout_centre_ex(game_buffer, game_font, "PLAYER 1 WINS ROUND!", 320, 230, makecol(255, 200, 100), -1);
            }
            
            // After timer expires, check if match is over or start next round
            if (round_transition_timer == 0)
            {
                if (p1_rounds_won >= 2 || p2_rounds_won >= 2)
                {
                    // Match over - go to winner screen
                    game_mode = 3;
                }
                else
                {
                    // Start next round - preserve character selections
                    current_round++;
                    int p1_char = players[0].character_id;
                    int p2_char = players[1].character_id;
                    init_player(&players[0], 0);
                    players[0].character_id = p1_char;
                    init_player(&players[1], 1);
                    players[1].character_id = p2_char;
                }
            }
        }
        else if (p1->health <= 0 || p2->health <= 0)
        {
            // Round just ended - award point and start transition
            if (p1->health <= 0)
            {
                p2_rounds_won++;
            }
            else
            {
                p1_rounds_won++;
            }
            round_transition_timer = 120; // 2 seconds at 60 FPS
        }
    }
    else if (game_mode == 3)
    {
        // Match winner screen
        clear_to_color(game_buffer, makecol(20, 20, 40));
        
        // Draw round indicators
        draw_round_indicators(game_buffer);
        
        if (p1_rounds_won > p2_rounds_won)
        {
            textout_centre_ex(game_buffer, game_font, "PLAYER 1 WINS THE MATCH!", 320, 200, makecol(255, 200, 100), -1);
            char score_text[64];
            sprintf(score_text, "Score: %d - %d", p1_rounds_won, p2_rounds_won);
            textout_centre_ex(game_buffer, game_font, score_text, 320, 230, makecol(200, 200, 200), -1);
        }
        else
        {
            textout_centre_ex(game_buffer, game_font, "PLAYER 2 WINS THE MATCH!", 320, 200, makecol(100, 200, 255), -1);
            char score_text[64];
            sprintf(score_text, "Score: %d - %d", p1_rounds_won, p2_rounds_won);
            textout_centre_ex(game_buffer, game_font, score_text, 320, 230, makecol(200, 200, 200), -1);
        }
        
        textout_centre_ex(game_buffer, game_font, "Press START for rematch", 320, 250, makecol(200, 200, 200), -1);
        
        if (key[p1_start_key] || key[p2_start_key])
        {
            game_mode = 1; // Back to character select
            p1_ready = false;
            p2_ready = false;
            p1_cursor = players[0].character_id;
            p2_cursor = players[1].character_id;
        }
    }
    
    // Copy game buffer to screen buffer
    blit(game_buffer, screen_buffer, 0, 0, 0, 0, 640, 480);
}

BITMAP* hamoopi_get_screen_buffer(void)
{
   return screen_buffer;
}

void hamoopi_set_input_state(unsigned port, unsigned device, unsigned index, unsigned id, int16_t state)
{
   if (port > 1)
      return;
   
   switch (id)
   {
      case RETRO_DEVICE_ID_JOYPAD_UP:
         hamoopi_input[port].up = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_DOWN:
         hamoopi_input[port].down = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_LEFT:
         hamoopi_input[port].left = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_RIGHT:
         hamoopi_input[port].right = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_A:
         hamoopi_input[port].a = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_B:
         hamoopi_input[port].b = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_X:
         hamoopi_input[port].x = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_Y:
         hamoopi_input[port].y = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_L:
         hamoopi_input[port].l = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_R:
         hamoopi_input[port].r = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_SELECT:
         hamoopi_input[port].select = state;
         break;
      case RETRO_DEVICE_ID_JOYPAD_START:
         hamoopi_input[port].start = state;
         break;
   }
}
