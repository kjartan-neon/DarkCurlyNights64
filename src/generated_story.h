#ifndef GENERATED_STORY_H
#define GENERATED_STORY_H

#include <stdint.h>

#define STORY_COND_NONE 0
#define STORY_COND_HAS_MULTITOOL 1
#define STORY_COND_HAS_COFFEE 2
#define STORY_FLAG_MULTITOOL 1
#define STORY_FLAG_COFFEE 2

typedef struct {
    const char* text;
    uint8_t target_scene;
    uint8_t alt_target_scene;
    uint8_t condition;
} StoryOption;

typedef struct {
    uint8_t id;
    const char* title;
    const char* description;
    uint8_t first_option;
    uint8_t option_count;
    uint8_t grants_flags;
} StoryScene;

static const uint8_t STORY_SCENE_COUNT = 30;
static const uint8_t STORY_OPTION_COUNT = 53;

static const StoryOption STORY_OPTIONS[] = {
    {"Try to wipe the condensation from the inside of the visor", 2, 255, STORY_COND_NONE},
    {"Panic and thrash against the constraints", 2, 255, STORY_COND_NONE},
    {"Wait for the drainage sequence to complete", 2, 255, STORY_COND_NONE},
    {"Check the console next to your pod", 3, 255, STORY_COND_NONE},
    {"Ignore the console and seek a way out", 4, 255, STORY_COND_NONE},
    {"Enter Protocol 'GULF SHIFT'", 2, 255, STORY_COND_NONE},
    {"Enter Protocol 'WHITE OSLO'", 5, 255, STORY_COND_NONE},
    {"Enter Protocol 'ATOM FALL'", 2, 255, STORY_COND_NONE},
    {"Locate the manual bypass wheel", 6, 255, STORY_COND_NONE},
    {"Look for another route", 6, 255, STORY_COND_NONE},
    {"Take the service elevator up", 7, 255, STORY_COND_NONE},
    {"Try to access other crew logs first", 7, 255, STORY_COND_NONE},
    {"Push through the doorway quickly", 7, 255, STORY_COND_NONE},
    {"Try to seal the leak first", 7, 255, STORY_COND_NONE},
    {"Keep climbing toward the hum", 8, 255, STORY_COND_NONE},
    {"Stop to rest on a maintenance platform", 8, 255, STORY_COND_NONE},
    {"Search for clothes or equipment", 9, 255, STORY_COND_NONE},
    {"Advance down the main corridor", 10, 255, STORY_COND_NONE},
    {"Continue to the main corridor, fully equipped", 10, 255, STORY_COND_NONE},
    {"\"I am K-81, Leader Class. Just awakened.\"", 11, 255, STORY_COND_NONE},
    {"\"Who are you?\"", 11, 255, STORY_COND_NONE},
    {"\"The pod system seems to be malfunctioning.\"", 12, 255, STORY_COND_NONE},
    {"\"Take me to the Station Commander.\"", 12, 255, STORY_COND_NONE},
    {"Ask about the state of the world", 13, 255, STORY_COND_NONE},
    {"Ask what happened to the Norwegian government", 13, 255, STORY_COND_NONE},
    {"\"And the Worker class?\"", 14, 255, STORY_COND_NONE},
    {"Attempt to apologize for the system", 15, 255, STORY_COND_NONE},
    {"Change the subject to your duty", 15, 255, STORY_COND_NONE},
    {"Code: 1999", 14, 255, STORY_COND_NONE},
    {"Code: 2021", 14, 255, STORY_COND_NONE},
    {"Code: 2038", 16, 255, STORY_COND_NONE},
    {"Offer to help with the core's diagnostics", 17, 18, STORY_COND_HAS_MULTITOOL},
    {"Watch her work from a distance", 18, 255, STORY_COND_NONE},
    {"Continue working together", 19, 255, STORY_COND_NONE},
    {"Help Elara", 19, 255, STORY_COND_NONE},
    {"Accept assignment", 20, 255, STORY_COND_NONE},
    {"Engage Elara in conversation about her life", 21, 255, STORY_COND_NONE},
    {"Keep your relationship strictly professional", 21, 255, STORY_COND_NONE},
    {"Share your secret stash of pre-collapse coffee", 22, 23, STORY_COND_HAS_COFFEE},
    {"Ask her if she resents you", 23, 255, STORY_COND_NONE},
    {"Hold her hand", 24, 255, STORY_COND_NONE},
    {"Tell her she's beautiful", 24, 255, STORY_COND_NONE},
    {"Apologize and change the subject", 24, 255, STORY_COND_NONE},
    {"Embrace the love", 25, 255, STORY_COND_NONE},
    {"The months pass", 26, 255, STORY_COND_NONE},
    {"5 Years Awake / 1 Year Asleep", 25, 255, STORY_COND_NONE},
    {"1 Year Awake / 5 Years Asleep", 27, 255, STORY_COND_NONE},
    {"1 Year Awake / 10 Years Asleep", 25, 255, STORY_COND_NONE},
    {"Seek out Elara immediately", 28, 255, STORY_COND_NONE},
    {"\"I will sabotage my pod. I will stay awake with you.\"", 29, 255, STORY_COND_NONE},
    {"\"The Station AI won't allow it.\"", 29, 255, STORY_COND_NONE},
    {"Attempt the dangerous AI hack", 28, 255, STORY_COND_NONE},
    {"Acceptance. Make the most of your final days", 30, 255, STORY_COND_NONE},
};

static const StoryScene STORY_SCENES[] = {
    {1, "THE COLD AWAKENING", "Your consciousness returns as a freezing sensation. You are floating in viscous gel, lungs burning. Thump-hiss, thump-hiss. An amber emergency light flickers beyond a cracked visor. You are trapped in a stasis pod. The gel is draining, revealing dim, archaic control panels.", 0, 3, 0},
    {2, "EXHUMATION", "The pod hiss-pops open. The transition from zero-G gel to heavy artificial gravity is brutal. You collapse onto cold metal grating. The air smells sterile and ancient. You cough up residual fluid, your limbs weak as wet noodles. You are alone in a room full of dormant pods.", 3, 2, 0},
    {3, "AMBER MESSAGES", "The green CRT monitor is cracked but functional. You boot the system. LOG START: STATION: AETHELRED-HULL 4. CLASS: LEADER. STATUS: MANDATORY AWAKENING CYCLE, YEAR 2142. The interface demands confirmation of the last global protocol. You search your hazy memory.", 5, 3, 0},
    {4, "SUBMERGED CORRIDOR", "You stumble into a corridor. The structure groans under immense pressure. Through a thick viewport, you see only deep blue darkness. Small, bio-luminescent creatures drift by. You aren't just in a base; you are deep underwater. A sealed bulkhead blocks the path.", 8, 2, 0},
    {5, "CONFIRMED IDENTITY", "IDENTITY CONFIRMED: SUBJECT K-81. The screen flashes. A compressed audio log plays: \"The Middle East exchange... the thermal bloom... the Gulf Stream has collapsed. We are activating AETHELRED. Welcome to the new Ice Age, K-81. Report to the Core.\"", 10, 2, 0},
    {6, "MECHANICAL FAILURE", "You seize the bypass wheel. It's seized with rust. You strain, your weak muscles screaming. With a sharp crack, it gives, but you also hear a hiss of water leaking near the seal.", 12, 2, 0},
    {7, "THE ASCENT", "You enter the elevator shaft. The lift is broken. You must climb the emergency ladder. It feels like miles. Your hands blister. The groan of the metal deepens as you ascend toward the distant hum of massive machinery.", 14, 2, 0},
    {8, "DRY LAND", "You reach a hatch and pull yourself through. This level is dry. The lighting is stable, cast in functional white LED. The air is slightly less stale. You are in a large maintenance bay.", 16, 2, 0},
    {9, "UNIFORM", "You find a locker. Inside is a grey jumpsuit. The patch reads: AETHELRED CORE - LEADER CLASS. A small medical kit heals your blisters. You also find a basic multi-tool.", 18, 1, STORY_FLAG_MULTITOOL | STORY_FLAG_COFFEE},
    {10, "FIRST CONTACT", "You step into the corridor. A figure in a bulkier blue jumpsuit approaches. It's a woman, her face tense. She holds a plasma welder defensively. \"Identify! Leader Class or Worker Class?\"", 19, 2, 0},
    {11, "ELARA", "She lowers the welder slightly. \"I am Elara, Worker Class. Maintenance, Sector 4.\" Her expression is cautious. \"You are early. The rotation schedule said next month.\"", 21, 2, 0},
    {12, "THE TRUTH OF AETHELRED", "Elara leads you to an observation deck. She punches a command into a retro-styled console. The blast shutters open. You are on an enormous oil platform. The ocean below is a solid, fractured plain of white ice.", 23, 2, 0},
    {13, "THE HISTORY LESSON", "Elara stares at the ice. \"The government? They died in Oslo. But Project AETHELRED survived. This oil complex was secretly reinforced for atmospheric collapse. We are all that’s left.\"", 25, 1, 0},
    {14, "THE ROTATION SCHEME", "A shadow crosses her face. \"Workers like me... we don't have stasis pods. We maintain the reactor. We grow the food. We live and die in the core.\"", 26, 2, 0},
    {15, "MEMORY CHECK I", "Elara needs to access the AI core to update your awakening log. \"The access code for this cycle is based on the year the AETHELRED project was secretly initiated.\"", 28, 3, 0},
    {16, "THE AI CORE", "The code works. Inside, massive, outdated 1980s-era mainframes fill a cavernous room. The hum is deafening. Elara begins working quickly at a terminal.", 31, 2, 0},
    {17, "TECHNICAL BOND", "You use your multi-tool to bypass a faulty relay. Elara looks up, surprised by your competence. A genuine smile breaks across her face. \"Not all Leaders are useless politicians,\" she quips.", 33, 1, STORY_FLAG_MULTITOOL},
    {18, "CHILLY OBSERVATION", "You stand back. Elara works with efficient, practiced movements. The core stabilizes. She looks at you, her expression again guarded. You are just another Leader.", 34, 1, 0},
    {19, "ASSIGNMENT", "Your awakening is logged. The AI Core flashes your duties. K-81 ASSIGNMENT: OPTIMIZE FUSION STABILIZER, SECTOR 7. DURATION: 1 YEAR. PREP FOR STASIS RETURN: YEAR 2143.", 35, 1, 0},
    {20, "SHARED SECTOR", "The Fusion Stabilizer is located in Sector 7—the same sector Elara maintains. Over the next few weeks, you work in close proximity. The mutual isolation draws you together.", 36, 2, 0},
    {21, "THE OBSERVATION DECK MET", "During your scarce downtime, you meet her on the Observation Deck. She talks about how she used to dream of the blue sky. You share your own memories of the world before the ice.", 38, 2, 0},
    {22, "COFFEE AND COMPLICITY", "The aroma of real coffee fills the sterile deck. Her eyes widen. \"This... this is contraband, K-81.\" She laughs, a rare, beautiful sound. You drink the bitter brew together.", 40, 2, STORY_FLAG_COFFEE},
    {23, "RESENTMENT", "\"Resent you? We all do our job. You sleep to preserve the intellect for the future. I work to keep us alive now. It is what it is.\" The honesty is real.", 42, 1, 0},
    {24, "THE KISS", "The proximity. You look at her, and she is the only other human that matters. Your lips meet. It feels forbidden, it is felectric.", 43, 1, 0},
    {25, "THE HIDDEN SEASON", "Months pass. Your romance remains hidden. You meet in maintenance tunnels and the shadows of the fusion core. The complex runs at peak efficiency.", 44, 1, 0},
    {26, "MEMORY CHECK II", "It is now 11 months since your awakening. The Fusion Stabilizer requires a final calibration code based on your stasis rotation pattern.", 45, 3, 0},
    {27, "T-MINUS ONE MONTH", "The code is correct. The fusion reactor stabilizes at 100%. A notification flashes: STASIS PROTOCOL INITIATING IN T-MINUS 720 HOURS. PREPARE FOR DEEP FREEZE.", 48, 1, 0},
    {28, "IMPOSSIBLE CHOICES", "You find Elara in the hidden tunnel. \"When you wake up, I’ll be 32, K-81. And you... you’ll still be you. My whole youth will happen while you sleep.\"", 49, 2, 0},
    {29, "SABOTAGE (A CHANCE?)", "\"If you stay,\" Elara whispers, \"we will be executed. Unless we hack the population registry. If we can convince the AI I am a Leader...\"", 51, 2, 0},
    {30, "THE DEEP FREEZE", "The final hour. You are standing outside your stasis pod. Elara stands beside you. \"I’ll be here when you wake up,\" she promises. The visor closes. The thick green gel begins to fill the chamber. Her face is blurred by the rising fluid.", 53, 0, 0},
};

#endif
