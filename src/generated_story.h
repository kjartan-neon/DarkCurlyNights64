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
static const uint8_t STORY_OPTION_COUNT = 55;

static const StoryOption STORY_OPTIONS[] = {
    {"Try to wipe the condensation", 2, 255, STORY_COND_NONE},
    {"Panic and thrash in the pod", 2, 255, STORY_COND_NONE},
    {"Wait for the drainage sequence", 2, 255, STORY_COND_NONE},
    {"Check the console next to your pod", 3, 255, STORY_COND_NONE},
    {"Ignore the console and seek a way out", 4, 255, STORY_COND_NONE},
    {"Enter Protocol 'GULF SHIFT'", 2, 255, STORY_COND_NONE},
    {"Enter Protocol 'WHITE OSLO'", 5, 255, STORY_COND_NONE},
    {"Enter Protocol 'ATOM FALL'", 2, 255, STORY_COND_NONE},
    {"Locate the manual bypass wheel", 6, 255, STORY_COND_NONE},
    {"Look for another route", 5, 255, STORY_COND_NONE},
    {"Take the service elevator up", 7, 255, STORY_COND_NONE},
    {"Try to open a bulkhead", 6, 255, STORY_COND_NONE},
    {"Push through the doorway quickly", 7, 255, STORY_COND_NONE},
    {"Go back to the terminal", 5, 255, STORY_COND_NONE},
    {"Keep climbing toward the hum", 8, 255, STORY_COND_NONE},
    {"Stop to rest on maintenance platform", 8, 255, STORY_COND_NONE},
    {"Search for clothes or equipment", 9, 255, STORY_COND_NONE},
    {"Advance down the main corridor", 10, 255, STORY_COND_NONE},
    {"Continue to the main corridor", 10, 255, STORY_COND_NONE},
    {"\"I am K-81, Leader Class. Just awakened.\"", 11, 255, STORY_COND_NONE},
    {"\"Who are you?\"", 11, 255, STORY_COND_NONE},
    {"\"The pod system is malfunctioning.\"", 12, 255, STORY_COND_NONE},
    {"\"How can I help?\"", 12, 255, STORY_COND_NONE},
    {"Ask about the state of the world", 13, 255, STORY_COND_NONE},
    {"Ask what happened to the Norway?", 13, 255, STORY_COND_NONE},
    {"\"What is the Worker class?\"", 14, 255, STORY_COND_NONE},
    {"Attempt to apologize for the system", 15, 255, STORY_COND_NONE},
    {"Ask about the dying Core", 15, 255, STORY_COND_NONE},
    {"Code: 1999", 14, 255, STORY_COND_NONE},
    {"Code: 2021", 14, 255, STORY_COND_NONE},
    {"Code: 2028", 16, 255, STORY_COND_NONE},
    {"Offer to help with the core's diagnostics", 17, 255, STORY_COND_NONE},
    {"She knows what to do. Watch her work from a distance", 18, 255, STORY_COND_NONE},
    {"Continue working together", 19, 255, STORY_COND_NONE},
    {"Help Elara", 17, 255, STORY_COND_NONE},
    {"Accept assignment", 20, 255, STORY_COND_NONE},
    {"Engage Elara in conversation", 21, 255, STORY_COND_NONE},
    {"Keep your relationship professional", 21, 255, STORY_COND_NONE},
    {"Share your secret stash of pre-collapse coffee", 22, 255, STORY_COND_HAS_COFFEE},
    {"Ask her if she resents you", 23, 255, STORY_COND_NONE},
    {"Hold her hand", 24, 255, STORY_COND_NONE},
    {"Tell her she's beautiful", 24, 255, STORY_COND_NONE},
    {"Apologize and share the secret coffee", 24, 255, STORY_COND_HAS_COFFEE},
    {"Embrace Elara", 25, 255, STORY_COND_NONE},
    {"Kiss Elara", 25, 255, STORY_COND_NONE},
    {"The weeks pass", 26, 255, STORY_COND_NONE},
    {"5 Years Awake / 1 Year Asleep", 25, 255, STORY_COND_NONE},
    {"1 Year Awake / 5 Years Asleep", 27, 255, STORY_COND_NONE},
    {"1 Year Awake / 10 Years Asleep", 25, 255, STORY_COND_NONE},
    {"Seek out Elara immediately", 28, 255, STORY_COND_NONE},
    {"Run and kiss Elara", 29, 255, STORY_COND_NONE},
    {"\"I will sabotage my pod. I will stay\"", 29, 255, STORY_COND_NONE},
    {"\"Maybe I can fix it on my next shift\"", 30, 255, STORY_COND_NONE},
    {"Attempt to hide from the AI", 24, 255, STORY_COND_NONE},
    {"Acceptance. Make the most of your final days", 30, 255, STORY_COND_NONE},
};

static const StoryScene STORY_SCENES[] = {
    {1, "THE COLD AWAKENING", "Your consciousness returns as a freezing sensation. You are floating in viscous gel. An amber emergency light flickers. You are trapped in a stasis pod. The gel is draining, revealing dim, archaic control panels.", 0, 3, 0},
    {2, "EXHUMATION", "The pod hiss-pops open. The transition from zero-G gel to heavy artificial gravity is brutal. You collapse onto cold metal grating. The air smells sterile and ancient. You cough up residual fluid, your limbs weak as wet noodles. You are alone in a room.", 3, 2, 0},
    {3, "AMBER MESSAGES", "The green CRT monitor is cracked but functional. You boot the system. LOG START YEAR 2037: STATION: AETHELRED-HULL 4. CLASS: LEADER. STATUS: EMERGENCY AWAKENING CYCLE. The interface demands confirmation of the last global protocol. You search your hazy memory.", 5, 3, 0},
    {4, "SUBMERGED CORRIDOR", "You stumble into a corridor. The structure groans under immense pressure. Through a thick viewport, you see only deep blue darkness. Small, bio- luminescent creatures drift by. You aren't just in a base; you are deep underwater. A bulkhead blocks the path.", 8, 2, 0},
    {5, "CONFIRMED IDENTITY", "IDENTITY CONFIRMED: SUBJECT K-81. The screen flashes. A compressed audio log plays: \"The Middle East exchange... the thermal bloom... the Gulf Stream has collapsed. We are activating AETHELRED. Welcome to the new Ice Age, K-81. Report to the Core.\"", 10, 2, 0},
    {6, "MECHANICAL FAILURE", "You seize the bypass wheel. It's seized with rust. You strain, your weak muscles screaming. With a sharp crack, it gives. You hear a scary sound from the other side.", 12, 2, 0},
    {7, "THE ASCENT", "You enter the elevator shaft. The lift is broken. You must climb the emergency ladder. It feels like miles. Your hands blister. The groan of the metal deepens as you ascend toward the distant hum of massive machinery.", 14, 2, 0},
    {8, "DRY LAND", "You reach a hatch and pull yourself through. This level is dry. The lighting is stable, cast in functional white LED. The air is slightly less stale. You are in a large maintenance bay.", 16, 2, 0},
    {9, "UNIFORM", "You find a locker. Inside is a grey jumpsuit. The patch reads: AETHELRED CORE - LEADER CLASS. A small medical kit heals your blisters. You also find a digital multi-tool.", 18, 1, STORY_FLAG_MULTITOOL | STORY_FLAG_COFFEE},
    {10, "FIRST CONTACT", "You step into the corridor. A figure in a blue jumpsuit approaches. It's a woman, her face tense. She holds a plasma welder defensively. \"Identify yourself!\"", 19, 2, 0},
    {11, "ELARA", "She lowers the welder slightly. \"I am Elara, Worker Class. Maintenance, Sector 7.\" Her expression is cautious. \"You are early. The rotation schedule said next month. But I need your help.\"", 21, 2, 0},
    {12, "THE TRUTH OF AETHELRED", "Elara leads you to an observation deck. She punches a command. The blast shutters open. You are on an enormous oil platform. The ocean below is pure ice. Elara looks out. \"The government? They died in Oslo. But Project AETHELRED survived.\"", 23, 2, 0},
    {13, "THE HISTORY LESSON", "It was after the Iranian war. The burning oil refinery and the dirty nuclear bombs. It all collapsed in 2028. But these ocean platforms was secretly rebuilt for a new ice age. Now we are all that is left.\"", 25, 1, 0},
    {14, "THE ROTATION SCHEME", "A shadow crosses her face. \"Workers like me... we don't have stasis pods. We maintain the reactor. We grow the food. We live and die in the Core. But now the Core is dying. To keep everyone alive, we need to fix the Core.\"", 26, 2, 0},
    {15, "MEMORY CHECK I", "\"But only a few have access, and the leader on this shift is in a coma after the virus started to spread. I need your fingerprint and the access code. It is the year the AETHELRED project was secretly initiated in the old calendar, but only leaders know.\"", 28, 3, 0},
    {16, "THE AI CORE", "The code works. Inside, massive, outdated mainframes fill a cavernous room. Elara begins working quickly at a terminal. \"The weather is worse, and still too cold outside. The core needs new parameters.\"", 31, 2, 0},
    {17, "TECHNICAL BOND", "You use your multi-tool to bypass a faulty relay. Now the screen boots, and you can fix the algorithm. Elara looks up, surprised by your competence. A genuine smile breaks across her face. \"Not all Leaders are useless politicians,\" she quips.", 33, 1, STORY_FLAG_MULTITOOL},
    {18, "CHILLY OBSERVATION", "You stand back. Elara works with efficient, practiced movements. But the core does not stabilize. She looks at you, her expression again guarded. You are just another Leader.", 34, 1, 0},
    {19, "ASSIGNMENT", "You can log in. K-81: EMERGENCY - AWAKE EARLY TO OPTIMIZE FUSION STABILIZER, SECTOR 7. DURATION: 1 YEAR. PREP FOR STASIS RETURN: YEAR 2143.", 35, 1, 0},
    {20, "SHARED SECTOR", "The Fusion Terminal is located in Sector 7—the same sector Elara maintains. Over the next few weeks, you work in close proximity. You are tierd, but the mutual isolation draws you together.", 36, 2, 0},
    {21, "THE OBSERVATION DECK", "During your scarce downtime, you meet her on the Observation Deck. She talks about how she used to dream of the blue sky. You share your own memories of the world before the ice and grey clouds. You remember your private stuff in storage.", 38, 2, 0},
    {22, "COFFEE AND COMPLICITY", "The aroma of real coffee fills the sterile deck. Her eyes widen. \"This... this is contraband, K-81.\" She laughs, a rare, beautiful sound. You drink the aromatic brew together.", 40, 2, STORY_FLAG_COFFEE},
    {23, "RESENTMENT", "\"Do I Resent you? We all do our job. You sleep to preserve the intellect for the future. I work to keep us alive now. It is what it is.\" The honesty is real.", 42, 1, 0},
    {24, "THE KISS", "The proximity. You look at her, and realize she is the only other human that matters in this world of ice. It feels forbidden, the air is electric.", 43, 2, 0},
    {25, "THE HIDDEN SEASON", "You meet hidden in maintenance tunnels and the shadows of the fusion core. Your tuned algorithm makes the corn run again at peak efficiency. But the AI wants to put you back in stasis.", 45, 1, 0},
    {26, "MEMORY CHECK II", "\"STASIS UNSTABLE\" AI controlled robots force you to the terminal. The Fusion core needs a reset command based on your stasis rotation pattern.", 46, 3, 0},
    {27, "T-MINUS ONE MONTH", "The code is correct. The fusion reactor stabilizes at 100%. A notification flashes: STASIS PROTOCOL INITIATING IN T-MINUS 720 HOURS. PREPARE FOR DEEP FREEZE.", 49, 2, 0},
    {28, "IMPOSSIBLE CHOICES", "You find Elara in the hidden tunnel. \"When you wake up, I will be older, K-81. And you... you will still be you. My whole life will happen while you sleep.\" \"It does not matter.\"", 51, 2, 0},
    {29, "SABOTAGE (A CHANCE?)", "\"If you stay,\" Elara whispers, \"we will have to hide from the AI supervision, or else you will be forced into stasis. And another leader will be woken.\"", 53, 2, 0},
    {30, "THE DEEP FREEZE", "The final hour. You are standing outside your stasis pod. Elara stands beside you. \"I’ll be here when you wake up,\" she promises. The visor closes. The thick green gel begins to fill the chamber. Her face is blurred by the rising fluid. \"I love you!\"", 55, 0, 0},
};

#endif
