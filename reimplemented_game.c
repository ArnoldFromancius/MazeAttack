/*	TITLE: MAZE ATTACK.
	AUTHOR: ARNOLD FROMANCIUS.
	
	DATE: 23/08/2020.
	
	DESCRIPTION: COMAND LINE CONSOLE BASED GAME, USES WIN32_API FOR ITS SIMPLE
				 GRAPHICS(ALL ASCII CHARACTER).
	
	DEVELOPMENT: USES DOUBLE BUFFERING. BASICALLY _PLOT() FUNCTIONS DRAW TO 
				 A TEMPORARY BUFFER SCREEN, AND THE DRAW() FUNCTION COPIES THE 
				 TEMPORARY BUFFER SCREEN TO THE MAIN SCREEN.

*/

#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<stdarg.h>
#include<windows.h>
#include<mmsystem.h>	//MCI: background music and sound effects
#include<conio.h>	//only for the getch() on the hiscore-file error path

/*	All gameplay rates below are expressed in MILLISECONDS, not in loop
	iterations. The game loop measures real elapsed time each frame and
	accumulates it, so behaviour is identical on fast and slow machines.
	The old names (enemy_speed, bullet_speed, ...) were iteration counts,
	higher meaning slower. The new *_interval_ms names make that explicit:
	higher means slower. */
#define MAX_FRAME_MS 250		//clamp: never simulate more than this per frame
#define SIM_DT_MS 16.6666667	//simulation slice: 60 steps per second
#define MAX_STEPS_PER_FRAME 8	//cap on catch-up steps after a slow frame

#define UP 72
#define DOWN 80
#define LEFT 75
#define RIGHT 77
#define MAX_ENEMIES 15
#define MAX_FIELD_LENGTH 2171
#define PERIMETER_BREACH_ROW 1962
#define PERIMETER_BREACH_POINT 2171
#define BOTTOM_WALL 2310
#define TOP_WALL 70
#define PLAYER_STAGE_END_POINT 2380
#define INFO_STAGE_END_POINT 2450
#define MAX_SHRAPNEL_TRAVEL 25
#define PLAYER_MAX_LEFT_POS 2312
#define PLAYER_MAX_RIGHT_POS 2378
#define FREEZE_DURATION_MS 3000	//player is immobilised for 3 seconds
#define MAX_ENEMY_BULLETS 5

/*	Number of aliases in the gunshot pool. Shots reuse a device, so a pool
	is what lets several sound at once instead of queueing behind each
	other. Declared up here because the audio state array needs its size. */
#define SHOT_POOL_SIZE 6

void clear(CHAR_INFO *);	//clears screen and plots game_background on screen
void draw(HANDLE *h,CHAR_INFO *ms,COORD *mss,COORD *msp,SMALL_RECT *ws);
void create_enemy_sprite();
void create_bullet_sprite(int);
void enemy_weapon_fire(int);
void enemy_weapon_computer();
void launch_grenade(int);
void delete_enemy_sprite();
void delete_bullet_sprite();
void detonate_grenade();
void plot_enemies(CHAR_INFO *);
void plot_enemy_fire(CHAR_INFO *);
void plot_bullets(CHAR_INFO *);
void plot_grenade(CHAR_INFO *);
void plot_shrapnel(CHAR_INFO *);
void enemy_init();
void update_enemy_fire(int);
void update_bullets();
void update_shrapnel();
void update_grenade();
void update_enemies();
void enemy_weapon_init();
void player_hit();
void plot_player(int,CHAR_INFO *);
void plot_score_board(CHAR_INFO *);
void loading();
void plot_game_over(CHAR_INFO *);
void plot_pause(CHAR_INFO *);
int audio_init();			//open audio devices and start the music
void audio_shutdown();		//close every open audio device
int audio_play_gunshot();	//play one gunshot from the pool
int audio_play_explosion();	//play the explosion
void audio_update_bgm();	//restart the track when it reaches the end
void compute_score(int *, char *);
void debug(int *);
void step_sim();			//one fixed timestep of game logic
void render_frame();		//clear, plot everything, blit to the console
void handle_key(int);		//apply a single keypress
int poll_input();			//drain queued key events; 1 if quit was pressed
void wait_any_key();		//block until a key is pressed
void flush_input();		//discard any queued key events
//enemy data
struct list{
	int head_pos;
	int body_pos;
	int tail_pos;
	int direction;
	struct list* next;
};
typedef struct list enemy;
//enemy* enemy_sprites=NULL;
enemy* enemy_sprites[20];

//bullet data
struct llist{
	int pos;
	struct llist *next;
};
typedef struct llist bullet;
bullet* bullet_sprites=NULL;
bullet* enemy_fire[MAX_ENEMY_BULLETS];

//grenade data
struct lllist{
	int pos;
};
typedef struct lllist bomb;
bomb* grenade=NULL;
struct llllist{
	int shrap1;
	int shrap2;
	int shrap3;
	int shrap4;
	int shrap5;
	int shrap6;
	int shrap7;
	int shrap8;
	int shrap9;
	int travel; //how far shrapnel lands max
};
typedef struct llllist shrap;
shrap *shrapnel=NULL;


int no_of_enemies=0;
int no_of_bullets=0;
/*	ms intervals between events; higher value == slower.
	seeded from the old iteration counts assuming a ~60Hz loop, then
	retuned after the switch to real time. */
double enemy_update_interval_ms=350;	//spawn a new enemy
double enemy_move_interval_ms=40;		//step an enemy
double bullet_step_interval_ms=12;		//step a player bullet
double enemy_bullet_interval_ms=12;	//step an enemy bullet
double grenade_step_interval_ms=60;	//step a grenade
/*	ms accumulators; each counts up to its interval then resets,
	mirroring the old "fps counter reaches threshold" logic */
double enemy_update_timer=0;
double enemy_move_timer=0;
double bullet_step_timer=0;
double enemy_bullet_timer=0;
double grenade_step_timer=0;
double enemy_weapon_timer=0;	//ms since the enemy guns last fired
double freeze_time_ms=-1;	//ms the player has been frozen; <0 means not frozen
int player_state=1; //healthy:1 vs wounded: 0
int gun_mode=1;	//semi auto:1 vs auto:>1
int heat=1; //two heat levels, in heat=2; enemies have weapons
double firing_interval_ms=1000;	//ms between enemy weapon shots
int heat_seeker_active=0;	//enemy rounds follow target when on:1;
int grenade_count=3;
int score=0;
int hiscore;
int gameover=0;
char Score[4];
char Hiscore[4];
int level;
int player_pos=2344;	//player's column on the player stage row
int game_quit=0;		//set when the player presses Q/ESC
int game_paused=0;	//1 while paused; toggled by any other key

/*	Which keys are physically held down. Windows keeps sending key-down
	events while a key is held, and those repeats keep arriving after the
	loading screen is dismissed. Shared between poll_input() and
	flush_input() so that the key used to dismiss the loading screen is
	recognised as still held, instead of its first repeat being treated as
	a fresh press and toggling pause. */
unsigned char key_is_down[256];

/*	audio state, see the audio section at the end of this file */
int audio_available=0;		//1 if any device opened successfully
int bgm_open=0;			//1 if the background music device is open
int blast_open=0;		//1 if the explosion device is open
int shot_open[SHOT_POOL_SIZE];	//per-alias open flags
int shot_next=0;			//next alias to use, cycled round-robin
int bgm_playing=0;			//1 while the background track is playing

/*	screen state, shared by main and render_frame() */
HANDLE console_out;
CHAR_INFO mirror_screen[70*35];
COORD mirror_screen_size={70,35};
COORD mirror_screen_pos={0,0};
SMALL_RECT WinSize={0,0,69,34};
COORD ScreenBufferSize={70,35};


int main(){
	/*	must be set before the first draw(): a file-scope HANDLE starts as
		NULL, and WriteConsoleOutputA fails silently on a null handle */
	console_out=GetStdHandle(STD_OUTPUT_HANDLE);
	/*	seed the RNG once, here. enemy_weapon_computer() used to call
		srand(time(0)) inside its own retry loop, which re-rolled the same
		value on every attempt. */
	srand((unsigned int)time(NULL));
	//setup window
	SetConsoleTitle("Maze Gunner...");
	//setup screen details
	SetConsoleWindowInfo(GetStdHandle(STD_OUTPUT_HANDLE),TRUE,&WinSize);
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE),ScreenBufferSize);

	loading(mirror_screen);
	draw(&console_out,mirror_screen,&mirror_screen_size,&mirror_screen_pos,&WinSize);
	wait_any_key();
	/*	drop the rest of the keystroke that dismissed the loading screen
		(an arrow key queues a prefix plus a scan code) so it does not
		leak into the first frame of play */
	flush_input();
	enemy_init();
	enemy_weapon_init();
	create_enemy_sprite();
	clear(mirror_screen);

	/*	Open the audio devices and start the background music. This is
		best-effort: if the files are missing the game runs silently rather
		than refusing to start. */
	audio_init();

	/*	Fixed timestep loop.

		The simulation always advances in SIM_DT_MS slices regardless of how
		fast the machine renders, so the game plays at the same speed on
		fast and slow hardware. Real elapsed time is measured with
		QueryPerformanceCounter, accumulated, and consumed one SIM_DT_MS
		slice at a time; leftover time carries into the next frame. */
	LARGE_INTEGER qpfreq,qpcnow;
	double last_ms;
	QueryPerformanceFrequency(&qpfreq);
	QueryPerformanceCounter(&qpcnow);
	last_ms=(double)qpcnow.QuadPart*1000.0/(double)qpfreq.QuadPart;

	double accumulator=0.0;

	while(game_quit==0){
		/*	Drain every pending keypress. poll_input() is non-blocking and
			handles the extended-key prefix internally, so input can never
			stall the simulation and no keystroke is discarded. */
		if(poll_input()){
			game_quit=1;
			break;
		}

		/*	measure real elapsed time since the previous frame */
		QueryPerformanceCounter(&qpcnow);
		double now_ms=(double)qpcnow.QuadPart*1000.0/(double)qpfreq.QuadPart;
		double frame_ms=now_ms-last_ms;
		last_ms=now_ms;
		/*	clamp after a stall (breakpoint, debugger, alt-tab) so the game
			cannot burst through minutes of simulation in one frame */
		if(frame_ms>MAX_FRAME_MS)
			frame_ms=MAX_FRAME_MS;
		if(frame_ms<0.0)
			frame_ms=0.0;

		accumulator+=frame_ms;

		/*	Paused: skip the simulation and drop the elapsed time, so
			resuming does not fast-forward through the pause. Rendering
			still runs, further below. */
		if(game_paused){
			accumulator=0.0;
		}
		else{
			/*	Run whole simulation slices. A fast machine runs 0 or 1 per
				frame; a slow one runs several to catch up on real time. */
			int steps=0;
			while(accumulator>=SIM_DT_MS){
				step_sim();
				accumulator-=SIM_DT_MS;
				steps++;
				if(gameover==1||game_quit)
					break;
			}
			/*	drop any unconsumed backlog rather than snowballing into an
				unbounded number of catch-up steps next frame */
			if(steps>=MAX_STEPS_PER_FRAME)
				accumulator=0.0;
		}

		/*	Render once per frame, outside the simulation loop, so the
			frame rate stays free while game speed stays fixed. */
		if(gameover==1){
			clear(mirror_screen);
			plot_player(player_pos,mirror_screen);
			plot_enemies(mirror_screen);
			plot_score_board(mirror_screen);
			plot_game_over(mirror_screen);
			draw(&console_out,mirror_screen,&mirror_screen_size,&mirror_screen_pos,&WinSize);
			audio_shutdown();
			wait_any_key();
			exit(0);
		}
		/*	restart the background track if it has finished; also the
			right place to update anything else audio-related each frame */
		audio_update_bgm();
		render_frame();
	}

	//player quit: show the final score, then exit
	clear(mirror_screen);
	plot_player(player_pos,mirror_screen);
	plot_score_board(mirror_screen);
	plot_game_over(mirror_screen);
	draw(&console_out,mirror_screen,&mirror_screen_size,&mirror_screen_pos,&WinSize);
	audio_shutdown();	//stop the music before the process goes away
	wait_any_key();
	return 0;
}

/*	Non-blocking keyboard input.

		conio's getch()/kbhit() pair is not used for gameplay input. getch()
		is a blocking call, and the arrow keys arrive as a two-byte extended
		sequence (0x00 prefix + scan code). Handling that reliably forces
		either a racy second read or a dropped keystroke, and the blocking
		read freezes the whole game loop.

		PeekConsoleInput()/ReadConsoleInput() return whole KEY_EVENT records
		with proper virtual-key codes, so a keypress is identified in a
		single read, extended keys need no prefix handling, and the call is
		strictly non-blocking because only already-queued events are read.

		Returns 1 if a quit key was pressed. */
int poll_input(){
	HANDLE hin=GetStdHandle(STD_INPUT_HANDLE);
	INPUT_RECORD rec;
	DWORD count=0;
	int quit=0;

	while(PeekConsoleInput(hin,&rec,1,&count)&&count>0){
		if(!ReadConsoleInput(hin,&rec,1,&count))
			break;
		if(rec.EventType!=KEY_EVENT)
			continue;			//ignore mouse and resize events

		int vk=(int)(rec.Event.KeyEvent.wVirtualKeyCode&0xFF);

		if(!rec.Event.KeyEvent.bKeyDown){
			key_is_down[vk]=0;	//released
			continue;
		}

		switch(rec.Event.KeyEvent.wVirtualKeyCode){
			case VK_UP:		handle_key(UP);		break;
			case VK_DOWN:	handle_key(DOWN);	break;
			case VK_LEFT:	handle_key(LEFT);	break;
			case VK_RIGHT:	handle_key(RIGHT);	break;
			case 'Q':
			case 'q':
			case VK_ESCAPE:	quit=1;			break;
			default:
			//any other key toggles pause, once per real press
			if(!key_is_down[vk]){
				key_is_down[vk]=1;
				game_paused=!game_paused;
			}
			break;
		}
		if(quit)
			break;
	}
	return quit;
}

/*	blocks until any key is pressed; used only for "press any key" prompts */
void wait_any_key(){
	INPUT_RECORD rec;
	DWORD count=0;
	for(;;){
		if(PeekConsoleInput(GetStdHandle(STD_INPUT_HANDLE),&rec,1,&count)&&count>0){
			if(ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE),&rec,1,&count)
				&&rec.EventType==KEY_EVENT&&rec.Event.KeyEvent.bKeyDown){
				/*	record it as held: the key that dismisses the loading
					screen may still be down, and its auto-repeat events
					must not be seen as a fresh press */
				key_is_down[rec.Event.KeyEvent.wVirtualKeyCode&0xFF]=1;
				return;
			}
		}
		Sleep(10);
	}
}

/*	Discards keys typed before play began, so presses made on the loading
	screen do not leak into the first frame of the game.

		Each discarded event still updates key_is_down[], so a key that was
		held when the loading screen was dismissed stays marked as held.
		Without this, the next auto-repeat event for that key would look
		like a fresh press and toggle pause on the first frame. */
void flush_input(){
	INPUT_RECORD rec;
	DWORD count=0;
	while(PeekConsoleInput(GetStdHandle(STD_INPUT_HANDLE),&rec,1,&count)&&count>0){
		if(!ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE),&rec,1,&count))
			break;
		if(rec.EventType!=KEY_EVENT)
			continue;
		key_is_down[rec.Event.KeyEvent.wVirtualKeyCode&0xFF]=
			rec.Event.KeyEvent.bKeyDown?1:0;
	}
}

/*	Applies one keypress. Separate from main so the input queue can be
	drained without blocking the simulation. Receives UP/DOWN/LEFT/RIGHT
	already resolved by poll_input(), never a raw prefix byte. */
void handle_key(int key){
	if(key==UP){
		if(player_state==1){
			if(no_of_bullets<gun_mode){
				create_bullet_sprite(player_pos-70);
				audio_play_gunshot();	//only when a bullet is really created
			}
		}
	}
	else if(key==LEFT){
		if(player_state==1){
			if(player_pos>PLAYER_MAX_LEFT_POS)
				player_pos-=2;
			else if(player_pos==PLAYER_MAX_LEFT_POS)
				player_pos-=1;
		}
	}
	else if(key==RIGHT){
		if(player_state==1){
			if(player_pos<PLAYER_MAX_RIGHT_POS&&player_pos+2<=PLAYER_MAX_RIGHT_POS)
				player_pos+=2;
			else if(player_pos==PLAYER_MAX_RIGHT_POS-1)
				player_pos+=1;
		}
	}
	else if(key==DOWN){
		if(player_state==1){
			if(grenade==NULL&&grenade_count>0){
				launch_grenade(player_pos-70);
				grenade_count--;
			}
			else{
				detonate_grenade();
				audio_play_explosion();
			}
		}
	}
}

/*	One fixed timestep of game logic.

		Each subsystem accumulates real elapsed time (SIM_DT_MS per call) and
		steps once its millisecond interval is reached, then carries the
		remainder forward so long intervals do not drift. Previously these
		were loop-iteration counters, which made the game speed depend on
		how fast the machine could spin the loop. */
void step_sim(){
	//check if to change level
	if(level==25){
		enemy_move_interval_ms-=10;
		bullet_step_interval_ms=10;
		enemy_update_interval_ms-=50;
		grenade_count=3;
		level=0;
	}
	else if(level==50){
		enemy_move_interval_ms-=5;
		bullet_step_interval_ms=7;
		enemy_update_interval_ms-=50;
		grenade_count=3;
		level=0;
	}
	else if(level==100){
		enemy_move_interval_ms-=10;
		bullet_step_interval_ms=5;
		enemy_update_interval_ms-=25;
		grenade_count=3;
		gun_mode=2;
		level=0;
	}
	//heat level 2 begins here
	else if(level==150){
		enemy_move_interval_ms=40;
		bullet_step_interval_ms=12;
		grenade_step_interval_ms=60;
		enemy_update_interval_ms=350;
		grenade_count=3;
		heat+=1;
		level=0;
	}
	else if(level==175){
		enemy_move_interval_ms-=10;
		bullet_step_interval_ms=10;
		enemy_bullet_interval_ms=9;
		enemy_update_interval_ms-=150;
		grenade_count=3;
		firing_interval_ms=500;
		level=0;
	}
	else if(level==200){
		enemy_move_interval_ms-=10;
		bullet_step_interval_ms=7;
		enemy_update_interval_ms-=50;
		grenade_count=5;
		firing_interval_ms=450;
		level=0;
	}
	else if(level==250){
		enemy_move_interval_ms=40;
		bullet_step_interval_ms=12;
		enemy_bullet_interval_ms=80;
		enemy_update_interval_ms=350;
		grenade_count=5;
		firing_interval_ms=1500;
		heat_seeker_active=1;
		level=0;
	}
	else if(level==275){
		enemy_move_interval_ms-=15;
		bullet_step_interval_ms=9;
		enemy_bullet_interval_ms=50;
		enemy_update_interval_ms=150;
		grenade_count=5;
		firing_interval_ms=1500;
		level=0;
	}

	//create more enemies if insufficient on screen
	enemy_update_timer+=SIM_DT_MS;
	if(enemy_update_timer>=enemy_update_interval_ms){
		if(no_of_enemies<6)
			create_enemy_sprite();
		enemy_update_timer=0.0;
	}

	//update enemy position
	enemy_move_timer+=SIM_DT_MS;
	if(enemy_move_timer>=enemy_move_interval_ms){
		update_enemies();
		enemy_move_timer=0.0;
	}

	//update grenade position
	grenade_step_timer+=SIM_DT_MS;
	if(grenade_step_timer>=grenade_step_interval_ms){
		update_grenade();
		grenade_step_timer=0.0;
	}

	//update bullet(and shrapnel) position
	bullet_step_timer+=SIM_DT_MS;
	if(bullet_step_timer>=bullet_step_interval_ms){
		update_bullets();
		update_shrapnel();
		bullet_step_timer=0.0;
	}

	//update enemy bullets
	enemy_bullet_timer+=SIM_DT_MS;
	if(enemy_bullet_timer>=enemy_bullet_interval_ms){
		update_enemy_fire(player_pos);
		enemy_bullet_timer=0.0;
	}

	//heat level:2 enemies have weapons
	if(heat==2){
		/*	fire on a fixed millisecond cadence instead of taking a
			modulus of a free-running iteration counter */
		enemy_weapon_timer+=SIM_DT_MS;
		if(enemy_weapon_timer>=firing_interval_ms){
			enemy_weapon_computer(player_pos);
			enemy_weapon_timer=0.0;
		}

		/*	count the player's immobilisation down in real milliseconds */
		if(freeze_time_ms>=0.0){
			freeze_time_ms+=SIM_DT_MS;
			if(freeze_time_ms>FREEZE_DURATION_MS&&player_state==0){
				freeze_time_ms=-1.0;
				player_state=1;	//unfreeze player
			}
		}
	}
}

/*	Draws the pause banner, over the last rendered frame.

		Two rows are used: on a single row the longer second line would
		overwrite the first, since both are centred on the same field. */
void plot_pause(CHAR_INFO *mirror_screen){
	const WORD a=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
	//"PAUSED" is 6 chars, centred in 70 columns: (70-6)/2 = 32
	int i=16*70+32;
	mirror_screen[i].Char.AsciiChar='P';	mirror_screen[i].Attributes=a;
	mirror_screen[i+1].Char.AsciiChar='A';	mirror_screen[i+1].Attributes=a;
	mirror_screen[i+2].Char.AsciiChar='U';	mirror_screen[i+2].Attributes=a;
	mirror_screen[i+3].Char.AsciiChar='S';	mirror_screen[i+3].Attributes=a;
	mirror_screen[i+4].Char.AsciiChar='E';	mirror_screen[i+4].Attributes=a;
	mirror_screen[i+5].Char.AsciiChar='D';	mirror_screen[i+5].Attributes=a;
	//"PRESS ANY KEY TO RESUME" is 23 chars, centred: (70-23)/2 = 23
	i=18*70+23;
	mirror_screen[i].Char.AsciiChar='P';	mirror_screen[i].Attributes=a;
	mirror_screen[i+1].Char.AsciiChar='R';	mirror_screen[i+1].Attributes=a;
	mirror_screen[i+2].Char.AsciiChar='E';	mirror_screen[i+2].Attributes=a;
	mirror_screen[i+3].Char.AsciiChar='S';	mirror_screen[i+3].Attributes=a;
	mirror_screen[i+4].Char.AsciiChar='S';	mirror_screen[i+4].Attributes=a;
	mirror_screen[i+5].Char.AsciiChar=' ';	mirror_screen[i+5].Attributes=a;
	mirror_screen[i+6].Char.AsciiChar='A';	mirror_screen[i+6].Attributes=a;
	mirror_screen[i+7].Char.AsciiChar='N';	mirror_screen[i+7].Attributes=a;
	mirror_screen[i+8].Char.AsciiChar='Y';	mirror_screen[i+8].Attributes=a;
	mirror_screen[i+9].Char.AsciiChar=' ';	mirror_screen[i+9].Attributes=a;
	mirror_screen[i+10].Char.AsciiChar='K';	mirror_screen[i+10].Attributes=a;
	mirror_screen[i+11].Char.AsciiChar='E';	mirror_screen[i+11].Attributes=a;
	mirror_screen[i+12].Char.AsciiChar='Y';	mirror_screen[i+12].Attributes=a;
	mirror_screen[i+13].Char.AsciiChar=' ';	mirror_screen[i+13].Attributes=a;
	mirror_screen[i+14].Char.AsciiChar='T';	mirror_screen[i+14].Attributes=a;
	mirror_screen[i+15].Char.AsciiChar='O';	mirror_screen[i+15].Attributes=a;
	mirror_screen[i+16].Char.AsciiChar=' ';	mirror_screen[i+16].Attributes=a;
	mirror_screen[i+17].Char.AsciiChar='R';	mirror_screen[i+17].Attributes=a;
	mirror_screen[i+18].Char.AsciiChar='E';	mirror_screen[i+18].Attributes=a;
	mirror_screen[i+19].Char.AsciiChar='S';	mirror_screen[i+19].Attributes=a;
	mirror_screen[i+20].Char.AsciiChar='U';	mirror_screen[i+20].Attributes=a;
	mirror_screen[i+21].Char.AsciiChar='M';	mirror_screen[i+21].Attributes=a;
	mirror_screen[i+22].Char.AsciiChar='E';	mirror_screen[i+22].Attributes=a;
}

/*	Draws one complete frame into the mirror buffer and blits it to the
	console. Called once per rendered frame, never once per sim step. */
void render_frame(){
	clear(mirror_screen);
	plot_grenade(mirror_screen);
	plot_shrapnel(mirror_screen);
	plot_enemies(mirror_screen);
	plot_enemy_fire(mirror_screen);
	plot_bullets(mirror_screen);
	plot_player(player_pos,mirror_screen);
	plot_score_board(mirror_screen);
	if(game_paused)
		plot_pause(mirror_screen);	//drawn last, on top of the frame
	draw(&console_out,mirror_screen,&mirror_screen_size,&mirror_screen_pos,&WinSize);
}

void player_hit(){
	if(player_state==0){	//if player already immobilised
		return;
	}	
	player_state=0;
}

void enemy_weapon_fire(int pos){
		int i=0;
		bullet* new_bullet;
		/*	find a free slot first: allocating up front leaked the
			new_bullet whenever every slot was already in use */
		while(i<MAX_ENEMY_BULLETS){
			if(enemy_fire[i]==NULL)
				break;
			i++;
		}
		if(i>=MAX_ENEMY_BULLETS)
			return;		//no free slot, do not fire
		new_bullet=malloc(sizeof(bullet));
		if(new_bullet==NULL)
			return;
		new_bullet->pos=pos;
		new_bullet->next=NULL;
		enemy_fire[i]=new_bullet;
}

void enemy_weapon_init(){
	int i=0;
	while(i<MAX_ENEMY_BULLETS){
		enemy_fire[i++]=NULL;
	}
}

void update_enemy_fire(int target){
	int i=0;
	/*	no early return on an empty first slot: later slots may still be
		in use and need to keep moving */
	//move each bullet to next position
	int tmp;
	while(i<MAX_ENEMY_BULLETS){
		if(enemy_fire[i]!=NULL){
			if(heat_seeker_active==1){
				tmp=enemy_fire[i]->pos;
				while(1){
					tmp+=70;
					if(tmp>PLAYER_STAGE_END_POINT-70&&tmp<PLAYER_STAGE_END_POINT)
						break;
				}
				if(target>tmp){
					enemy_fire[i]->pos+=71;
				}
				else if(target<tmp){
					enemy_fire[i]->pos+=69;
				}
				else{
					enemy_fire[i]->pos+=70;
				}
			}
			else enemy_fire[i]->pos+=70;
		}
		/*	skip empty slots rather than stopping: the pool is not
			dense, so breaking here left later bullets frozen in mid-air */
		i++;
	}

	//check if bullet hit wall
	i=0;
	while(i<MAX_ENEMY_BULLETS){
		if(enemy_fire[i]!=NULL){
			if(enemy_fire[i]->pos>=PLAYER_STAGE_END_POINT){
				free(enemy_fire[i]);
				enemy_fire[i]=NULL;
			}
		}
		i++;
	}
	//check if bullet hit target
	i=0;
	while(i<MAX_ENEMY_BULLETS){
		if(enemy_fire[i]!=NULL){
			if(enemy_fire[i]->pos==target-1||enemy_fire[i]->pos==target||enemy_fire[i]->pos==target+1){
				free(enemy_fire[i]);
				enemy_fire[i]=NULL;
				player_hit();
				freeze_time_ms=0.0;	//start counting as player is frozen
			}
		}
		i++;
	}
}

void plot_enemy_fire(CHAR_INFO *mirror_screen){

	int i=0;
	while(i<MAX_ENEMY_BULLETS){
		if(enemy_fire[i]!=NULL){
			if(heat_seeker_active==1){
				mirror_screen[enemy_fire[i]->pos].Char.AsciiChar='*';
				mirror_screen[enemy_fire[i]->pos].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			}
			else{
				mirror_screen[enemy_fire[i]->pos].Char.AsciiChar='*';
				mirror_screen[enemy_fire[i]->pos].Attributes=FOREGROUND_BLUE|BACKGROUND_RED|BACKGROUND_GREEN|BACKGROUND_INTENSITY;
			}
		}
		i++;
	}	
}

void enemy_weapon_computer(int target){
	/*selects an enemy to fire their weapon at any given time*/
	
	enemy *shooter;
	shooter=NULL;
	int i=0;
	//select an enemy in line of fire to player, to fire weapon
	while(i<no_of_enemies){	
		if(enemy_sprites[i]!=NULL){
			if(enemy_sprites[i]->body_pos==target){
				shooter=enemy_sprites[i];
				break;
			}
		}
		i++;
	}
	if(shooter==NULL){
		/*	No enemy is lined up with the player, so pick one at random.
			srand() belongs in main: re-seeding from time(0) inside this
			retry loop re-rolled the same value every attempt and made the
			loop spin when no enemy was available. */
		int x;
		int attempts=0;
		while(attempts<MAX_ENEMIES){
			x=rand()%MAX_ENEMIES;
			if(enemy_sprites[x]!=NULL){
				shooter=enemy_sprites[x];
				break;
			}
			attempts++;
		}
		/*	nothing alive to shoot from */
		if(shooter==NULL)
			return;
	}
	
	int weapon_cordinates=shooter->body_pos;
	//fire weapon
	enemy_weapon_fire(weapon_cordinates);
}

void launch_grenade(int pos){
	grenade=malloc(sizeof(bomb));
	if(grenade==NULL) return;
	grenade->pos=pos;
}

void detonate_grenade(){
		if(grenade==NULL) return;
		/*	only one shrapnel burst exists at a time; drop any previous
			one rather than leaking it */
		if(shrapnel!=NULL){
			free(shrapnel);
			shrapnel=NULL;
		}
		shrapnel=malloc(sizeof(shrap));
		shrapnel->shrap1=grenade->pos;
		shrapnel->shrap2=shrapnel->shrap1-1;
		shrapnel->shrap3=shrapnel->shrap1+1;
		shrapnel->shrap4=shrapnel->shrap1-70-1;
		shrapnel->shrap5=shrapnel->shrap1-70+1;
		shrapnel->shrap6=shrapnel->shrap1-70;
		shrapnel->shrap7=shrapnel->shrap1+70-1;
		shrapnel->shrap8=shrapnel->shrap1+70+1;
		shrapnel->shrap9=shrapnel->shrap1+70;
		shrapnel->travel=0;
		free(grenade);
		grenade=NULL;
}

void plot_shrapnel(CHAR_INFO *mirror_screen){
		if(shrapnel==NULL)	return;
		if(shrapnel->shrap1!=-1){
			mirror_screen[shrapnel->shrap1].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap1].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		if(shrapnel->shrap2!=-1){
			mirror_screen[shrapnel->shrap2].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap2].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		
		}
		if(shrapnel->shrap3!=-1){
			mirror_screen[shrapnel->shrap3].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap3].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		if(shrapnel->shrap4!=-1){
			mirror_screen[shrapnel->shrap4].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap4].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
			
		}
		if(shrapnel->shrap5!=-1){
			mirror_screen[shrapnel->shrap5].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap5].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		if(shrapnel->shrap6!=-1){
			mirror_screen[shrapnel->shrap6].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap6].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		if(shrapnel->shrap7!=-1){
			mirror_screen[shrapnel->shrap7].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap7].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		if(shrapnel->shrap8!=-1){
			mirror_screen[shrapnel->shrap8].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap8].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		if(shrapnel->shrap9!=-1){
			mirror_screen[shrapnel->shrap9].Char.AsciiChar='@';
			mirror_screen[shrapnel->shrap9].Attributes=FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_INTENSITY;
		}
		
}

void update_shrapnel(){
	if(shrapnel==NULL) return;
	if(shrapnel->travel==MAX_SHRAPNEL_TRAVEL){
		free(shrapnel);
		shrapnel=NULL;
		return;
	}
	//update shrap cordinates by 1
	{
		if(shrapnel->shrap1!=-1)
			shrapnel->shrap1-=70;
		if(shrapnel->shrap2!=-1)
			shrapnel->shrap2-=1;
		if(shrapnel->shrap3!=-1)
			shrapnel->shrap3+=1;
		if(shrapnel->shrap4!=-1)
			shrapnel->shrap4=shrapnel->shrap4-70-1;
		if(shrapnel->shrap5!=-1)
			shrapnel->shrap5=shrapnel->shrap5-70+1;
		if(shrapnel->shrap6!=-1)
			shrapnel->shrap6-=70;
		if(shrapnel->shrap7!=-1)
			shrapnel->shrap7=shrapnel->shrap7+70-1;
		if(shrapnel->shrap8!=-1)
			shrapnel->shrap8=shrapnel->shrap8+70+1;
		if(shrapnel->shrap9!=-1)
			shrapnel->shrap9+=70;
		shrapnel->travel++;
	}
	
	//if shrapnel hits the wall
	{
		if(shrapnel->shrap1<=70)
			shrapnel->shrap1=-1;
		if((shrapnel->shrap2%70)==0)
			shrapnel->shrap2=-1;
		if(((shrapnel->shrap3%70)-1)==0)
			shrapnel->shrap3=-1;
		if(((shrapnel->shrap4%70)==0)||shrapnel->shrap4<=70)
			shrapnel->shrap4=-1;
		if(((shrapnel->shrap5%70)==0)||shrapnel->shrap5<=70)
			shrapnel->shrap5=-1;
		if(shrapnel->shrap6<=70)
			shrapnel->shrap6=-1;
		if(shrapnel->shrap7>=BOTTOM_WALL||shrapnel->shrap7%70==0)
			shrapnel->shrap7=-1;
		if(shrapnel->shrap8>=BOTTOM_WALL||shrapnel->shrap8%70+1==0)
			shrapnel->shrap8=-1;
		if(shrapnel->shrap9>=BOTTOM_WALL)
			shrapnel->shrap9=-1;
	}
	
	//if shrapnel hits a target
	int i=0,j;
	while(i<20){
		j=0;
		while(j<MAX_ENEMIES){
			if(enemy_sprites[j]!=NULL){
				if(shrapnel->shrap1==enemy_sprites[j]->body_pos||
				  shrapnel->shrap1==enemy_sprites[j]->head_pos||
				  shrapnel->shrap1==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap1+i-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap1+i-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap1+i-140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap1-i-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap1-i-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap1-i-140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap1>=1){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap1=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap2-i==enemy_sprites[j]->body_pos||
				  shrapnel->shrap2-i==enemy_sprites[j]->head_pos||
				  shrapnel->shrap2-i==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap2-i+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap2-i+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap2-i+140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap2-i-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap2-i-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap2-i-140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap2>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap2=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap3+i==enemy_sprites[j]->body_pos||
				  shrapnel->shrap3+i==enemy_sprites[j]->head_pos||
				  shrapnel->shrap3+i==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap3+i-70==enemy_sprites[j]->body_pos||
				  shrapnel->shrap3+i-70==enemy_sprites[j]->head_pos||
				  shrapnel->shrap3+i-70==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap3+i+70==enemy_sprites[j]->body_pos||
				  shrapnel->shrap3+i+70==enemy_sprites[j]->head_pos||
				  shrapnel->shrap3+i+70==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap3>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap3=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap4-i==enemy_sprites[j]->body_pos||
				  shrapnel->shrap4-i==enemy_sprites[j]->head_pos||
				  shrapnel->shrap4-i==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap4-i-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap4-i-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap4-i-140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap4-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap4-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap4-140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap4>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap4=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap5+i==enemy_sprites[j]->body_pos||
				  shrapnel->shrap5+i==enemy_sprites[j]->head_pos||
				  shrapnel->shrap5+i==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap5+i-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap5+i-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap5+i-140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap5-140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap5-140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap5-140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap5>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap5=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap6==enemy_sprites[j]->body_pos||
				  shrapnel->shrap6==enemy_sprites[j]->head_pos||
				  shrapnel->shrap6==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap6>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap6=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap7-i==enemy_sprites[j]->body_pos||
				  shrapnel->shrap7-i==enemy_sprites[j]->head_pos||
				  shrapnel->shrap7-i==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap7-i+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap7-i+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap7-i+140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap7+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap7+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap7+140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap7>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap7=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap8+i==enemy_sprites[j]->body_pos||
				  shrapnel->shrap8+i==enemy_sprites[j]->head_pos||
				  shrapnel->shrap8+i==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap8+i+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap8+i+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap8+i+140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap8+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap8+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap8+140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap8>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap8=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
				if(shrapnel->shrap9+i+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap9+i+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap9+i+140==enemy_sprites[j]->tail_pos||
				  shrapnel->shrap9-i+140==enemy_sprites[j]->body_pos||
				  shrapnel->shrap9-i+140==enemy_sprites[j]->head_pos||
				  shrapnel->shrap9-i+140==enemy_sprites[j]->tail_pos)
				{
					if(shrapnel->shrap9>=0){
						delete_enemy_sprite(enemy_sprites[j]);
						j=0;
						shrapnel->shrap9=-1;	
						score++;
						compute_score(&score,Score);
						no_of_enemies--;
						break;
					}
				}
			}
		j++;
		}
		if(i>=20)break;	
		i+=3;
	}	
	
}

void update_grenade(){
	if(grenade==NULL) return;
	if(grenade->pos<=TOP_WALL){
		free(grenade);
		grenade=NULL;
		return;
	}
	grenade->pos-=70;
}

void plot_grenade(CHAR_INFO *mirror_screen){
	if(grenade==NULL) return;
	mirror_screen[grenade->pos].Char.AsciiChar='#';
	mirror_screen[grenade->pos].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
		
}

void compute_score(int *s, char *hiscore){
	//bad design here; we use compute_score() to
	//check for a level change
	int x,i=0,j,tmp;
	tmp=*s;
	
	if(tmp==25)
		level=25;
	else if(tmp==50)
		level=50;
	else if(tmp==100)
		level=100;
	else if(tmp==150)
		level=150;
	else if(tmp==175)
		level=175;
	else if(tmp==200)
		level=200;
    else if(tmp==250)
		level=250;
	else if(tmp==275)
		level=275;
	//now to continue with our function
	
	int temp[4];
	while(i<4){
		x=(*s)%10;
		(*s)/=10;
		hiscore[i++]=x+'0';
	}
	hiscore[i]='\0';

	i=0,j=3;
	while(i<4){
		temp[i++]=hiscore[j--];
	}
	i=0;
	while(i<4){
		hiscore[i]=temp[i];
		i++;
	}
	*s=tmp;
}

void loading(CHAR_INFO *mirror_screen){

	int i=0;
	//set bg color
	while(i<2450){
		mirror_screen[i].Char.AsciiChar=' ';
		mirror_screen[i].Attributes=FOREGROUND_INTENSITY;
		i++;
	}
	i=70;
	
	//set coorp trademark
	{
		i+=55;
		mirror_screen[i].Char.AsciiChar='B';
		mirror_screen[i].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar='i';
		mirror_screen[i+1].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+2].Char.AsciiChar='n';
		mirror_screen[i+2].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+3].Char.AsciiChar='a';
		mirror_screen[i+3].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+4].Char.AsciiChar='r';
		mirror_screen[i+4].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+5].Char.AsciiChar='y';
		mirror_screen[i+5].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
	    mirror_screen[i+6].Char.AsciiChar='B';
		mirror_screen[i+6].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+7].Char.AsciiChar='r';
		mirror_screen[i+7].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+8].Char.AsciiChar='o';
		mirror_screen[i+8].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+9].Char.AsciiChar='s';
		mirror_screen[i+9].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+10].Char.AsciiChar='.';
		mirror_screen[i+10].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+11].Char.AsciiChar='i';
		mirror_screen[i+11].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+12].Char.AsciiChar='n';
		mirror_screen[i+12].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+13].Char.AsciiChar='c';
		mirror_screen[i+13].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY;
	}
    
	//set game title
	{
		i=((35*70)/3)-22;
		mirror_screen[i].Char.AsciiChar='<';
		mirror_screen[i].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar='-';
		mirror_screen[i+1].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY;
	
		mirror_screen[i+2].Char.AsciiChar='M';
		mirror_screen[i+2].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+3].Char.AsciiChar=' ';
		mirror_screen[i+3].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+4].Char.AsciiChar='A';
		mirror_screen[i+4].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+5].Char.AsciiChar=' ';
		mirror_screen[i+5].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+6].Char.AsciiChar='Z';
		mirror_screen[i+6].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+7].Char.AsciiChar=' ';
		mirror_screen[i+7].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+8].Char.AsciiChar='E';
		mirror_screen[i+8].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
	    
		mirror_screen[i+9].Char.AsciiChar=' ';
		mirror_screen[i+9].Attributes=FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+10].Char.AsciiChar='*';
		mirror_screen[i+10].Attributes=FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+11].Char.AsciiChar=' ';
		mirror_screen[i+11].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		
		mirror_screen[i+12].Char.AsciiChar='A';
		mirror_screen[i+12].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+13].Char.AsciiChar=' ';
		mirror_screen[i+13].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		
		mirror_screen[i+14].Char.AsciiChar='T';
		mirror_screen[i+14].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+15].Char.AsciiChar=' ';
		mirror_screen[i+15].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+16].Char.AsciiChar='T';
		mirror_screen[i+16].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+17].Char.AsciiChar=' ';
		mirror_screen[i+17].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+18].Char.AsciiChar='A';
		mirror_screen[i+18].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+19].Char.AsciiChar=' ';
		mirror_screen[i+19].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+20].Char.AsciiChar='C';
		mirror_screen[i+20].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+21].Char.AsciiChar=' ';
		mirror_screen[i+21].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		mirror_screen[i+22].Char.AsciiChar='K';
		mirror_screen[i+22].Attributes=FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY|BACKGROUND_RED|BACKGROUND_BLUE;
		
		
		mirror_screen[i+23].Char.AsciiChar='-';
		mirror_screen[i+23].Attributes=FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
	    mirror_screen[i+24].Char.AsciiChar='>';
		mirror_screen[i+24].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
	}
	
	//set game instructiions
    {
    	i+=140+7;
		mirror_screen[i].Char.AsciiChar='C';
		mirror_screen[i].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+1].Char.AsciiChar='O';
		mirror_screen[i+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+2].Char.AsciiChar='N';
		mirror_screen[i+2].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+3].Char.AsciiChar='T';
		mirror_screen[i+3].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+4].Char.AsciiChar='R';
		mirror_screen[i+4].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+5].Char.AsciiChar='O';
		mirror_screen[i+5].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+6].Char.AsciiChar='L';
		mirror_screen[i+6].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+7].Char.AsciiChar='S';
		mirror_screen[i+7].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
	
		i+=140-7;
	    mirror_screen[i+10].Char.AsciiChar='^';
		mirror_screen[i+10].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;	
		mirror_screen[i+17].Char.AsciiChar='G';
		mirror_screen[i+17].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_GREEN;
		mirror_screen[i+18].Char.AsciiChar='U';
		mirror_screen[i+18].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+19].Char.AsciiChar='N';
		mirror_screen[i+19].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		
		i+=70;
		mirror_screen[i].Char.AsciiChar='L';
		mirror_screen[i].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_GREEN;
		mirror_screen[i+1].Char.AsciiChar='E';
		mirror_screen[i+1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+2].Char.AsciiChar='F';
		mirror_screen[i+2].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+3].Char.AsciiChar='T';
		mirror_screen[i+3].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+5].Char.AsciiChar='<';
		mirror_screen[i+5].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		
		mirror_screen[i+15].Char.AsciiChar='>';
		mirror_screen[i+15].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+17].Char.AsciiChar='R';
		mirror_screen[i+17].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_GREEN;
		mirror_screen[i+18].Char.AsciiChar='I';
		mirror_screen[i+18].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+19].Char.AsciiChar='G';
		mirror_screen[i+19].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+20].Char.AsciiChar='H';
		mirror_screen[i+20].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+21].Char.AsciiChar='T';
		mirror_screen[i+21].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		
		i+=70;
	    mirror_screen[i+10].Char.AsciiChar='v';
		mirror_screen[i+10].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		
		mirror_screen[i+17].Char.AsciiChar='G';
		mirror_screen[i+17].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_GREEN;
		mirror_screen[i+18].Char.AsciiChar='R';
		mirror_screen[i+18].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+19].Char.AsciiChar='E';
		mirror_screen[i+19].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+20].Char.AsciiChar='N';
		mirror_screen[i+20].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+21].Char.AsciiChar='A';
		mirror_screen[i+21].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+22].Char.AsciiChar='D';
		mirror_screen[i+22].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+23].Char.AsciiChar='E';
		mirror_screen[i+23].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		
		i+=140;
	    mirror_screen[i+1].Char.AsciiChar='Q';
		mirror_screen[i+1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+2].Char.AsciiChar='/';
		mirror_screen[i+2].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+3].Char.AsciiChar='E';
		mirror_screen[i+3].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+4].Char.AsciiChar='S';
		mirror_screen[i+4].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+5].Char.AsciiChar='C';
		mirror_screen[i+5].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		
		mirror_screen[i+17].Char.AsciiChar='Q';
		mirror_screen[i+17].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_GREEN;
		mirror_screen[i+18].Char.AsciiChar='U';
		mirror_screen[i+18].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+19].Char.AsciiChar='I';
		mirror_screen[i+19].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+20].Char.AsciiChar='T';
		mirror_screen[i+20].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+21].Char.AsciiChar='!';
		mirror_screen[i+21].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		
		i+=140;
	    mirror_screen[i+1].Char.AsciiChar='[';
		mirror_screen[i+1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+2].Char.AsciiChar='A';
		mirror_screen[i+2].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+3].Char.AsciiChar='O';
		mirror_screen[i+3].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+4].Char.AsciiChar='K';
		mirror_screen[i+4].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		mirror_screen[i+5].Char.AsciiChar=']';
		mirror_screen[i+5].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_RED;
		
		mirror_screen[i+17].Char.AsciiChar='P';
		mirror_screen[i+17].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_GREEN;
		mirror_screen[i+18].Char.AsciiChar='A';
		mirror_screen[i+18].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+19].Char.AsciiChar='U';
		mirror_screen[i+19].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+20].Char.AsciiChar='S';
		mirror_screen[i+20].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+21].Char.AsciiChar='E';
		mirror_screen[i+21].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
	}
	
	
	//set game info
	{
		i+=148;
		mirror_screen[i].Char.AsciiChar='*';
		mirror_screen[i].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+1].Char.AsciiChar='K';
		mirror_screen[i+1].Attributes=FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+2].Char.AsciiChar='E';
		mirror_screen[i+2].Attributes=FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+3].Char.AsciiChar='Y';
		mirror_screen[i+3].Attributes=FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		mirror_screen[i+4].Char.AsciiChar='*';
		mirror_screen[i+4].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
		
		i+=137;
		mirror_screen[i].Char.AsciiChar='M';
		mirror_screen[i].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar=':';
		mirror_screen[i+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+3].Char.AsciiChar='G';
		mirror_screen[i+3].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+4].Char.AsciiChar='u';
		mirror_screen[i+4].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+5].Char.AsciiChar='n';
		mirror_screen[i+5].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+6].Char.AsciiChar='-';
		mirror_screen[i+6].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+7].Char.AsciiChar='M';
		mirror_screen[i+7].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+8].Char.AsciiChar='o';
		mirror_screen[i+8].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+9].Char.AsciiChar='d';
		mirror_screen[i+9].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+10].Char.AsciiChar='e';
		mirror_screen[i+10].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		
		i+=73;
		mirror_screen[i].Char.AsciiChar='S';
		mirror_screen[i].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar=':';
		mirror_screen[i+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+3].Char.AsciiChar='S';
		mirror_screen[i+3].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+4].Char.AsciiChar='e';
		mirror_screen[i+4].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+5].Char.AsciiChar='m';
		mirror_screen[i+5].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+6].Char.AsciiChar='i';
		mirror_screen[i+6].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		
		i+=70;
		mirror_screen[i].Char.AsciiChar='A';
		mirror_screen[i].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar=':';
		mirror_screen[i+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+3].Char.AsciiChar='A';
		mirror_screen[i+3].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+4].Char.AsciiChar='u';
		mirror_screen[i+4].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+5].Char.AsciiChar='t';
		mirror_screen[i+5].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+6].Char.AsciiChar='o';
		mirror_screen[i+6].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		
		i+=137;
		mirror_screen[i].Char.AsciiChar='G';
		mirror_screen[i].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar=':';
		mirror_screen[i+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+3].Char.AsciiChar='G';
		mirror_screen[i+3].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+4].Char.AsciiChar='r';
		mirror_screen[i+4].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+5].Char.AsciiChar='e';
		mirror_screen[i+5].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+6].Char.AsciiChar='n';
		mirror_screen[i+6].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+7].Char.AsciiChar='a';
		mirror_screen[i+7].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+8].Char.AsciiChar='d';
		mirror_screen[i+8].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+9].Char.AsciiChar='e';
		mirror_screen[i+9].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+10].Char.AsciiChar='s';
		mirror_screen[i+10].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		
		i+=139;
		mirror_screen[i].Char.AsciiChar='R';
		mirror_screen[i].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+1].Char.AsciiChar='/';
		mirror_screen[i+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+2].Char.AsciiChar='T';
		mirror_screen[i+2].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[i+3].Char.AsciiChar=':';
		mirror_screen[i+3].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+5].Char.AsciiChar='F';
		mirror_screen[i+5].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+6].Char.AsciiChar='i';
		mirror_screen[i+6].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+7].Char.AsciiChar='r';
		mirror_screen[i+7].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+8].Char.AsciiChar='e';
		mirror_screen[i+8].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+10].Char.AsciiChar='R';
		mirror_screen[i+10].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+11].Char.AsciiChar='a';
		mirror_screen[i+11].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+12].Char.AsciiChar='t';
		mirror_screen[i+12].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[i+13].Char.AsciiChar='e';
		mirror_screen[i+13].Attributes=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		
	}
	
	//load hisscore
	i=(70*35)-14;
	FILE *fp;
	fp=fopen("game.dat","r");
	if(fp==NULL){
		//printf error to screen
		{
			mirror_screen[i].Char.AsciiChar='D';
			mirror_screen[i].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+1].Char.AsciiChar='a';
			mirror_screen[i+1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+2].Char.AsciiChar='t';
			mirror_screen[i+2].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+3].Char.AsciiChar='a';
			mirror_screen[i+3].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+4].Char.AsciiChar='N';
			mirror_screen[i+4].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+5].Char.AsciiChar='o';
			mirror_screen[i+5].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
		    mirror_screen[i+6].Char.AsciiChar='t';
			mirror_screen[i+6].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+7].Char.AsciiChar='L';
			mirror_screen[i+7].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+8].Char.AsciiChar='o';
			mirror_screen[i+8].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+9].Char.AsciiChar='a';
			mirror_screen[i+9].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+10].Char.AsciiChar='d';
			mirror_screen[i+10].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+11].Char.AsciiChar='e';
			mirror_screen[i+11].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+12].Char.AsciiChar='d';
			mirror_screen[i+12].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
		}
		hiscore=0;
		compute_score(&score,Score);
		compute_score(&hiscore,Hiscore);
	}
	else{
		fscanf(fp,"%d",&hiscore);
		compute_score(&score,Score);
		compute_score(&hiscore,Hiscore);
		fclose(fp);
		//print data loaded to screen
		{
			mirror_screen[i].Char.AsciiChar='D';
			mirror_screen[i].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+1].Char.AsciiChar='a';
			mirror_screen[i+1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+2].Char.AsciiChar='t';
			mirror_screen[i+2].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+3].Char.AsciiChar='a';
			mirror_screen[i+3].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+4].Char.AsciiChar='L';
			mirror_screen[i+4].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+5].Char.AsciiChar='o';
			mirror_screen[i+5].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
		    mirror_screen[i+6].Char.AsciiChar='a';
			mirror_screen[i+6].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+7].Char.AsciiChar='d';
			mirror_screen[i+7].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+8].Char.AsciiChar='e';
			mirror_screen[i+8].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+9].Char.AsciiChar='d';
			mirror_screen[i+9].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+10].Char.AsciiChar='.';
			mirror_screen[i+10].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+11].Char.AsciiChar='.';
			mirror_screen[i+11].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
			mirror_screen[i+12].Char.AsciiChar='.';
			mirror_screen[i+12].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
		}
	}
}

void plot_score_board(CHAR_INFO *mirror_screen){
	int x=2382;
	//plot score
	{
		mirror_screen[x].Char.AsciiChar='S';
		mirror_screen[x].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+1].Char.AsciiChar='C';
		mirror_screen[x+1].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+2].Char.AsciiChar='O';
		mirror_screen[x+2].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+3].Char.AsciiChar='R';
		mirror_screen[x+3].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+4].Char.AsciiChar='E';
		mirror_screen[x+4].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+5].Char.AsciiChar=':';
		mirror_screen[x+5].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+6].Char.AsciiChar=Score[1];
		mirror_screen[x+6].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+7].Char.AsciiChar=Score[2];
		mirror_screen[x+7].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+8].Char.AsciiChar=Score[3];
		mirror_screen[x+8].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+12].Char.AsciiChar='M';
		mirror_screen[x+12].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+13].Char.AsciiChar=':';
		mirror_screen[x+13].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		if(gun_mode==1){
			mirror_screen[x+14].Char.AsciiChar='S';
			mirror_screen[x+14].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		}
		else{
			mirror_screen[x+14].Char.AsciiChar='A';
			mirror_screen[x+14].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;	
		}
		mirror_screen[x+15].Char.AsciiChar=' ';
		mirror_screen[x+15].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+16].Char.AsciiChar='G';
		mirror_screen[x+16].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN;
		mirror_screen[x+17].Char.AsciiChar=':';
		mirror_screen[x+17].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+18].Char.AsciiChar=grenade_count+'0';
		mirror_screen[x+18].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
	}
	
	x+=26;
	//plot title
	{
		mirror_screen[x].Char.AsciiChar='<';
		mirror_screen[x].Attributes=FOREGROUND_RED|FOREGROUND_BLUE;
		mirror_screen[x+1].Char.AsciiChar='<';
		mirror_screen[x+1].Attributes=FOREGROUND_RED|FOREGROUND_GREEN;
		mirror_screen[x+2].Char.AsciiChar=' ';
		mirror_screen[x+2].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+3].Char.AsciiChar='G';
		mirror_screen[x+3].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+4].Char.AsciiChar='U';
		mirror_screen[x+4].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+5].Char.AsciiChar='N';
		mirror_screen[x+5].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+6].Char.AsciiChar='N';
		mirror_screen[x+6].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+7].Char.AsciiChar='E';
		mirror_screen[x+7].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+8].Char.AsciiChar='R';
		mirror_screen[x+8].Attributes=FOREGROUND_RED|FOREGROUND_INTENSITY;
		mirror_screen[x+9].Char.AsciiChar=' ';
		mirror_screen[x+9].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+10].Char.AsciiChar='>';
		mirror_screen[x+10].Attributes=FOREGROUND_RED|FOREGROUND_GREEN;
		mirror_screen[x+11].Char.AsciiChar='>';
		mirror_screen[x+11].Attributes=FOREGROUND_RED|FOREGROUND_BLUE;
		mirror_screen[x+22].Char.AsciiChar='R';
		mirror_screen[x+22].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE;
		mirror_screen[x+23].Char.AsciiChar='\\';
		mirror_screen[x+23].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		mirror_screen[x+24].Char.AsciiChar='T';
		mirror_screen[x+24].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE;
		mirror_screen[x+25].Char.AsciiChar=':';
		mirror_screen[x+25].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE;
		mirror_screen[x+26].Char.AsciiChar=' ';
		mirror_screen[x+26].Attributes=FOREGROUND_RED|FOREGROUND_BLUE;
		mirror_screen[x+27].Char.AsciiChar=gun_mode+'0';
		mirror_screen[x+27].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
	}
	
	//plot hiscore
	{
		x+=30;
		mirror_screen[x].Char.AsciiChar='H';
		mirror_screen[x].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+1].Char.AsciiChar='I';
		mirror_screen[x+1].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+2].Char.AsciiChar='S';
		mirror_screen[x+2].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+3].Char.AsciiChar='C';
		mirror_screen[x+3].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+4].Char.AsciiChar='O';
		mirror_screen[x+4].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+5].Char.AsciiChar='R';
		mirror_screen[x+5].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+6].Char.AsciiChar='E';
		mirror_screen[x+6].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+7].Char.AsciiChar=':';
		mirror_screen[x+7].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+8].Char.AsciiChar=Hiscore[1];
		mirror_screen[x+8].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+9].Char.AsciiChar=Hiscore[2];
		mirror_screen[x+9].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
		mirror_screen[x+10].Char.AsciiChar=Hiscore[3];
		mirror_screen[x+10].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
	}
}

void plot_player(int x, CHAR_INFO *mirror_screen){
	if(player_state==0){
		mirror_screen[x-70].Char.AsciiChar='-';
		mirror_screen[x-70].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|
		FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_RED;
		mirror_screen[x-1].Char.AsciiChar='/';
		mirror_screen[x-1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|
		FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_RED;
		mirror_screen[x].Char.AsciiChar='#';
		mirror_screen[x].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|
		FOREGROUND_GREEN|BACKGROUND_RED;
		mirror_screen[x+1].Char.AsciiChar='/';
		mirror_screen[x+1].Attributes=FOREGROUND_BLUE|FOREGROUND_RED|
		FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_RED;
	}
	else{
		mirror_screen[x-70].Char.AsciiChar='^';
		mirror_screen[x-70].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE|FOREGROUND_RED|
		FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_RED;
		mirror_screen[x-1].Char.AsciiChar='!';
		mirror_screen[x-1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE|FOREGROUND_RED|
		FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_RED;
		mirror_screen[x].Char.AsciiChar='*';
		mirror_screen[x].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|FOREGROUND_BLUE|
		FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_INTENSITY;
		mirror_screen[x+1].Char.AsciiChar='!';
		mirror_screen[x+1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE|FOREGROUND_RED|
		FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_RED;
	}
}

void update_bullets(){
	bullet *iterator;
	iterator=bullet_sprites;
	int flag,i;
	while(iterator!=NULL){
		i=flag=0;
		while(i<MAX_ENEMIES){
			if(enemy_sprites[i]!=NULL){
				if(enemy_sprites[i]->head_pos==iterator->pos){
					delete_enemy_sprite(enemy_sprites[i]);
					delete_bullet_sprite(iterator);
					i=0;
					flag=1;
					score++;
					compute_score(&score,Score);
					no_of_enemies--;
					break;
				}
				if(enemy_sprites[i]->body_pos==iterator->pos){
					delete_enemy_sprite(enemy_sprites[i]);
					delete_bullet_sprite(iterator);
					i=0;
					flag=1;
					score++;
					compute_score(&score,Score);
					no_of_enemies--;
					break;
				}
				if(enemy_sprites[i]->tail_pos==iterator->pos){
					delete_enemy_sprite(enemy_sprites[i]);
					delete_bullet_sprite(iterator);
					i=0;
					flag=1;
					score++;
					compute_score(&score,Score);
					no_of_enemies--;
					break;
				}
			}
		i++;
		}
		if(flag==1){
			iterator=bullet_sprites;
			if(iterator==NULL){
				break;
			}
			continue;	
		}

		if(iterator->pos<=TOP_WALL){ //if bullet hits wall
			delete_bullet_sprite(iterator);
			iterator=bullet_sprites;
			if(iterator==NULL)
				break;
		}
		else{
			iterator->pos-=70;
		}
		iterator=iterator->next;
	}
}

void update_enemies(){

	int i=0;
	//enemy movement
	while(i<MAX_ENEMIES){
		if(enemy_sprites[i]!=NULL){
			//left turn
			if(((enemy_sprites[i]->head_pos%70)==0) && (enemy_sprites[i]->direction==1)){
				enemy_sprites[i]->head_pos=enemy_sprites[i]->head_pos+70;
				enemy_sprites[i]->body_pos+=1;
				enemy_sprites[i]->tail_pos+=1;
				enemy_sprites[i]->direction=0;
			}
			else if(((enemy_sprites[i]->head_pos%70)==0) && (enemy_sprites[i]->direction==0)){
				if(((enemy_sprites[i]->body_pos%70)==0)&&((enemy_sprites[i]->tail_pos%70)!=0)){
					enemy_sprites[i]->tail_pos=enemy_sprites[i]->body_pos;
					enemy_sprites[i]->body_pos=enemy_sprites[i]->head_pos;
					enemy_sprites[i]->head_pos=enemy_sprites[i]->head_pos+70;
				}
				else if(((enemy_sprites[i]->body_pos%70)==0)&&((enemy_sprites[i]->tail_pos%70)==0)){
					enemy_sprites[i]->tail_pos=enemy_sprites[i]->body_pos;
					enemy_sprites[i]->body_pos=enemy_sprites[i]->head_pos;
					enemy_sprites[i]->head_pos=enemy_sprites[i]->head_pos-1;
				}
	
			}
			else if(((enemy_sprites[i]->body_pos%70)==0)&&((enemy_sprites[i]->tail_pos%70)==0)){
				enemy_sprites[i]->tail_pos=enemy_sprites[i]->body_pos;
				enemy_sprites[i]->body_pos=enemy_sprites[i]->head_pos;
				enemy_sprites[i]->head_pos-=1;
			}//right turn
			else if(((enemy_sprites[i]->head_pos+70-1)%70)==0 && enemy_sprites[i]->direction==0){
				enemy_sprites[i]->tail_pos-=1;
				enemy_sprites[i]->body_pos-=1;
				enemy_sprites[i]->head_pos=enemy_sprites[i]->head_pos+70;
				enemy_sprites[i]->direction=1;
			}
			else if(((enemy_sprites[i]->head_pos+70-1)%70)==0 && enemy_sprites[i]->direction==1){
				if(((enemy_sprites[i]->body_pos+70-1)%70)==0 && ((enemy_sprites[i]->tail_pos+70-1)%70)!=0){
					enemy_sprites[i]->tail_pos=enemy_sprites[i]->body_pos;
					enemy_sprites[i]->body_pos=enemy_sprites[i]->head_pos;
					enemy_sprites[i]->head_pos=enemy_sprites[i]->head_pos+70;
				}
				else if(((enemy_sprites[i]->body_pos+70-1)%70)==0 && ((enemy_sprites[i]->tail_pos+70-1)%70)==0){
					enemy_sprites[i]->tail_pos=enemy_sprites[i]->body_pos;
					enemy_sprites[i]->body_pos=enemy_sprites[i]->head_pos;
					enemy_sprites[i]->head_pos+=1;
				}
			}
			else if(((enemy_sprites[i]->body_pos+70-1)%70)==0 && ((enemy_sprites[i]->tail_pos+70-1)%70)==0){
					enemy_sprites[i]->tail_pos=enemy_sprites[i]->body_pos;
					enemy_sprites[i]->body_pos=enemy_sprites[i]->head_pos;
					enemy_sprites[i]->head_pos+=1;
			}
			//
			else if(enemy_sprites[i]->direction==1){
				enemy_sprites[i]->body_pos+=1;
				enemy_sprites[i]->head_pos+=1;
				enemy_sprites[i]->tail_pos+=1;
			}
			else if(enemy_sprites[i]->direction==0){
				enemy_sprites[i]->body_pos-=1;
				enemy_sprites[i]->head_pos-=1;
				enemy_sprites[i]->tail_pos-=1;
			}
			
		}
		i++;
	}

	//if enemy breaches perimiter(player lose)
	i=0;
	while(i<MAX_ENEMIES){
		if(enemy_sprites[i]!=NULL){
			if(enemy_sprites[i]->head_pos>=PERIMETER_BREACH_POINT){
				gameover=1;
				return;
			}
		}
		i++;
	}
}

void plot_game_over(CHAR_INFO *mirror_screen){
	int x;
	//print game over
	{
		x=(70*35)-220;
		x+=65;
		mirror_screen[x].Char.AsciiChar='G';
		mirror_screen[x].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+1].Char.AsciiChar='A';
		mirror_screen[x+1].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+2].Char.AsciiChar='M';
		mirror_screen[x+2].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+3].Char.AsciiChar='E';
		mirror_screen[x+3].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+4].Char.AsciiChar=' ';
		mirror_screen[x+4].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+5].Char.AsciiChar='O';
		mirror_screen[x+5].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
		mirror_screen[x+6].Char.AsciiChar='V';
		mirror_screen[x+6].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
		mirror_screen[x+7].Char.AsciiChar='E';
		mirror_screen[x+7].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
		mirror_screen[x+8].Char.AsciiChar='R';
		mirror_screen[x+8].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
		mirror_screen[x+9].Char.AsciiChar=' ';
		mirror_screen[x+9].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
		mirror_screen[x+10].Char.AsciiChar='.';
		mirror_screen[x+10].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+11].Char.AsciiChar='!';
		mirror_screen[x+11].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+12].Char.AsciiChar='!';
		mirror_screen[x+12].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
		mirror_screen[x+13].Char.AsciiChar='!';
		mirror_screen[x+13].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
	}
	//if new hiscore
	if(hiscore<score){
		FILE *fp=fopen("game.dat","w");
		if(fp==NULL){
			getch();
			exit(0);
		}
		fprintf(fp,"%d",score);
		fclose(fp);
		//High score
		{
			x+=70;
			mirror_screen[x].Char.AsciiChar='H';
			mirror_screen[x].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+1].Char.AsciiChar='i';
			mirror_screen[x+1].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+2].Char.AsciiChar='g';
			mirror_screen[x+2].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+3].Char.AsciiChar='h';
			mirror_screen[x+3].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+4].Char.AsciiChar=' ';
			mirror_screen[x+4].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+5].Char.AsciiChar='S';
			mirror_screen[x+5].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+6].Char.AsciiChar='c';
			mirror_screen[x+6].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+7].Char.AsciiChar='o';
			mirror_screen[x+7].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+8].Char.AsciiChar='r';
			mirror_screen[x+8].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+9].Char.AsciiChar='e';
			mirror_screen[x+9].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+10].Char.AsciiChar=':';
			mirror_screen[x+10].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+11].Char.AsciiChar=Score[1];
			mirror_screen[x+11].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+12].Char.AsciiChar=Score[2];
			mirror_screen[x+12].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+13].Char.AsciiChar=Score[3];
			mirror_screen[x+13].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;	
			
		}
	
	}
	else{
		//print score
		{
			x+=70;
			mirror_screen[x].Char.AsciiChar='C';
			mirror_screen[x].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+1].Char.AsciiChar='u';
			mirror_screen[x+1].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+2].Char.AsciiChar='r';
			mirror_screen[x+2].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+3].Char.AsciiChar='r';
			mirror_screen[x+3].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+4].Char.AsciiChar=' ';
			mirror_screen[x+4].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+5].Char.AsciiChar='S';
			mirror_screen[x+5].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+6].Char.AsciiChar='c';
			mirror_screen[x+6].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+7].Char.AsciiChar='o';
			mirror_screen[x+7].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+8].Char.AsciiChar='r';
			mirror_screen[x+8].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+9].Char.AsciiChar='e';
			mirror_screen[x+9].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE|BACKGROUND_INTENSITY;
			mirror_screen[x+10].Char.AsciiChar=':';
			mirror_screen[x+10].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+11].Char.AsciiChar=Score[1];
			mirror_screen[x+11].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+12].Char.AsciiChar=Score[2];
			mirror_screen[x+12].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;
			mirror_screen[x+13].Char.AsciiChar=Score[3];
			mirror_screen[x+13].Attributes=FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|BACKGROUND_RED|BACKGROUND_BLUE;	
			
		}		
	}
}

void plot_enemies(CHAR_INFO *mirror_screen){
	
	int i=0;
	while(i<MAX_ENEMIES){
		if(enemy_sprites[i]!=NULL){
			if((enemy_sprites[i]->body_pos%70)==0 || (enemy_sprites[i]->head_pos%70)==0 || (enemy_sprites[i]->tail_pos%70)==0){
				if(heat==1){
					mirror_screen[enemy_sprites[i]->head_pos-1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->head_pos-1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->body_pos-1].Char.AsciiChar='X';
					mirror_screen[enemy_sprites[i]->body_pos-1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->tail_pos-1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->tail_pos-1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
				}
				else{
					mirror_screen[enemy_sprites[i]->head_pos-1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->head_pos-1].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->body_pos-1].Char.AsciiChar='*';
					mirror_screen[enemy_sprites[i]->body_pos-1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->tail_pos-1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->tail_pos-1].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
				}
			}
			else if(((enemy_sprites[i]->body_pos+70)%70)==0 || ((enemy_sprites[i]->head_pos+70)%70)==0){			
				if(heat==1){
					mirror_screen[enemy_sprites[i]->head_pos+1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->head_pos+1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->body_pos+1].Char.AsciiChar='X';
					mirror_screen[enemy_sprites[i]->body_pos+1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->tail_pos+1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->tail_pos+1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
				}
				else{
					mirror_screen[enemy_sprites[i]->head_pos+1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->head_pos+1].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->body_pos+1].Char.AsciiChar='*';
					mirror_screen[enemy_sprites[i]->body_pos+1].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->tail_pos+1].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->tail_pos+1].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
				}	
			}
			else{			
				if(heat==1){
					mirror_screen[enemy_sprites[i]->head_pos].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->head_pos].Attributes=FOREGROUND_INTENSITY|FOREGROUND_BLUE|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->body_pos].Char.AsciiChar='X';
					mirror_screen[enemy_sprites[i]->body_pos].Attributes=FOREGROUND_INTENSITY|FOREGROUND_GREEN|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->tail_pos].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->tail_pos].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
				}
				else{
					mirror_screen[enemy_sprites[i]->head_pos].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->head_pos].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->body_pos].Char.AsciiChar='*';
					mirror_screen[enemy_sprites[i]->body_pos].Attributes=FOREGROUND_INTENSITY|FOREGROUND_RED|BACKGROUND_BLUE;
					mirror_screen[enemy_sprites[i]->tail_pos].Char.AsciiChar='0';
					mirror_screen[enemy_sprites[i]->tail_pos].Attributes=FOREGROUND_GREEN|FOREGROUND_INTENSITY|BACKGROUND_BLUE;
				}
			}
		}
		i++;
	}
}

void plot_bullets(CHAR_INFO *mirror_screen){
	//error checking
	if(bullet_sprites==NULL)
		return;
	bullet *iterator;
	iterator=bullet_sprites;
	while(iterator!=NULL){
		mirror_screen[iterator->pos].Char.AsciiChar='*';
		mirror_screen[iterator->pos].Attributes=FOREGROUND_INTENSITY|BACKGROUND_INTENSITY|
		BACKGROUND_RED|FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		iterator=iterator->next;
	}
}

void enemy_init(){
	int i=0;
	while(i<MAX_ENEMIES){
		enemy_sprites[i++]=NULL;
	}
}

void create_enemy_sprite(){
	int i=0;
	while(i<MAX_ENEMIES){
		if(enemy_sprites[i]==NULL){
			enemy_sprites[i]=malloc(sizeof(enemy));
			enemy_sprites[i]->head_pos=2;
			enemy_sprites[i]->body_pos=1;
			enemy_sprites[i]->tail_pos=0;
			enemy_sprites[i]->direction=1;
			enemy_sprites[i]->next=NULL;
			no_of_enemies++;	
			return;	
		}
		i++;
	}
	return;
}

void delete_enemy_sprite(enemy *target){
	int i=0;
	if(target==NULL)
		return;
	/*	bounded search: a miss used to walk past the end of the
		array, so always stop at MAX_ENEMIES */
	while(i<MAX_ENEMIES&&enemy_sprites[i]!=target){
		i++;
	}
	if(i>=MAX_ENEMIES)
		return;		//not in the array, leave it alone
	enemy_sprites[i]=NULL;
	free(target);
}

void create_bullet_sprite(int pos){
	if(bullet_sprites==NULL){	//no bullets on screen
		bullet_sprites=malloc(sizeof(bullet));
		bullet_sprites->pos=pos;
		bullet_sprites->next=NULL;
		no_of_bullets++;
		return;
	}
	bullet *iterator,*temp,*new_bullet;
	iterator=bullet_sprites;
	while(iterator!=NULL){
		temp=iterator;
		iterator=iterator->next;
	}
	new_bullet=malloc(sizeof(bullet));
	new_bullet->pos=pos;
	new_bullet->next=NULL;
	temp->next=new_bullet;
	no_of_bullets++;
}

void delete_bullet_sprite(bullet *round){
	bullet	*iterator,*temp;
	if(round==NULL)
		return;
	iterator=bullet_sprites;
	//if first item in list
	if(iterator==round){
		bullet_sprites=bullet_sprites->next;
		free(iterator);
		no_of_bullets--;
		return;
	}
	//walk to the node before the one being removed
	while(iterator!=NULL&&iterator!=round){
		temp=iterator;
		iterator=iterator->next;
	}
	//not in the list: nothing to unlink
	if(iterator==NULL)
		return;
	temp->next=round->next;
	free(round);
	no_of_bullets--;
}

void draw(HANDLE *h,CHAR_INFO *ms,COORD *mss,COORD *msp,SMALL_RECT *ws){
		WriteConsoleOutputA(*h,ms,*mss,*msp,ws);
}

void clear(CHAR_INFO *mirror_screen){
	int i=0,row=1,j,l=0;

	while(i<(MAX_FIELD_LENGTH)){
		if((row%2)!=0){	//draw a path
			j=i+70;
			while(i<j){
				if(i>=PERIMETER_BREACH_ROW){
					mirror_screen[i].Char.AsciiChar='|';
					mirror_screen[i].Attributes=BACKGROUND_GREEN|BACKGROUND_BLUE;
				}
				else{
					mirror_screen[i].Char.AsciiChar=' ';
					mirror_screen[i].Attributes=BACKGROUND_INTENSITY;
				}

				i++;
			}
			row++;
		}
		else{			//draw a barrier
			j=i+70;
			while(i<j){
				mirror_screen[i].Char.AsciiChar='.';
				mirror_screen[i].Attributes=FOREGROUND_INTENSITY|
											FOREGROUND_BLUE|
											BACKGROUND_INTENSITY|
											BACKGROUND_GREEN;
				i++;
			}
			//to curve path
			if(l==1){
				if(i>=PERIMETER_BREACH_ROW){
					mirror_screen[i-70].Char.AsciiChar='-';
					mirror_screen[i-69].Char.AsciiChar='o';
					mirror_screen[i-70].Attributes=FOREGROUND_INTENSITY|
					BACKGROUND_RED|BACKGROUND_GREEN|FOREGROUND_GREEN|FOREGROUND_BLUE|
					FOREGROUND_RED;
					mirror_screen[i-69].Attributes=FOREGROUND_INTENSITY|
					BACKGROUND_GREEN|BACKGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE|
					FOREGROUND_RED;
				}
				else{
					mirror_screen[i-70].Char.AsciiChar=' ';
					mirror_screen[i-69].Char.AsciiChar=' ';
					mirror_screen[i-70].Attributes=BACKGROUND_INTENSITY;
					mirror_screen[i-69].Attributes=BACKGROUND_INTENSITY;
				}
				l=0;
			}
			else{
				if(i>=PERIMETER_BREACH_ROW){
					mirror_screen[i-1].Char.AsciiChar='|';
					mirror_screen[i-2].Char.AsciiChar='|';
					mirror_screen[i-1].Attributes=BACKGROUND_GREEN|BACKGROUND_BLUE;
					mirror_screen[i-2].Attributes=BACKGROUND_GREEN|BACKGROUND_BLUE;
				}
				else{
					mirror_screen[i-1].Char.AsciiChar=' ';
					mirror_screen[i-2].Char.AsciiChar=' ';
					mirror_screen[i-1].Attributes=BACKGROUND_INTENSITY;
					mirror_screen[i-2].Attributes=BACKGROUND_INTENSITY;
				}
				l=1;
			}

			row++;
		}
	}
	//draw player stage
	while(i<PLAYER_STAGE_END_POINT){
			mirror_screen[i].Char.AsciiChar=' ';
			mirror_screen[i].Attributes=BACKGROUND_INTENSITY;
			i++;
	}
	//draw info stage
	while(i<INFO_STAGE_END_POINT){
		mirror_screen[i].Char.AsciiChar=' ';
		mirror_screen[i].Attributes=FOREGROUND_INTENSITY;
		i++;
	}
}


/*	==========================================================================
		AUDIO (MCI / mmsystem)

		Split out at the bottom of this file. Background music and effects
		are opened as separate MCI devices, each under its own alias:

			bgm    one device, played once and left repeating
			shot   a small pool of aliases, cycled, so rapid fire overlaps
			blast  one device for the grenade

		Every call is non-blocking ("wait 0") and every failure is silent:
		a missing or unreadable file leaves the game running with no sound
		rather than stalling the loop or stopping the game.
	========================================================================== */

/*	Number of aliases in the gunshot pool. Shots reuse a device, so a pool
	is what allows several to sound at once instead of queueing. */
#define SHOT_POOL_SIZE 6

/*	Appends one line to audio_debug.txt.

		The game repaints the whole console every frame, so anything written
		to stderr is overwritten within a frame and is effectively invisible.
		A log file survives the run and can be read afterwards, which is the
		only practical way to see what MCI actually did. */
static void audio_log(const char *fmt,...){
	FILE *fp;
	va_list ap;
	fp=fopen("audio_debug.txt","a");
	if(fp==NULL)
		return;
	va_start(ap,fmt);
	vfprintf(fp,fmt,ap);
	va_end(ap);
	fputc('\n',fp);
	fclose(fp);
}

/*	Sends one MCI command and logs the result. Returns 0 on success. */
static MCIERROR mci_send_log(const char *cmd){
	char buf[128];
	MCIERROR r;
	buf[0]='\0';
	r=mciSendStringA(cmd,buf,sizeof(buf),0);
	if(r){
		char err[160];
		mciGetErrorStringA(r,err,sizeof(err));
		audio_log("FAIL %-42lu %s | %s",(unsigned long)r,cmd,err);
	}
	else{
		audio_log("  ok %-42s -> %s",cmd,buf);
	}
	return r;
}

static MCIERROR mci_send(const char *cmd){
	char buf[128];
	return mciSendStringA(cmd,buf,sizeof(buf),0);
}

/*	Opens one device. Returns 0 on success; on failure the alias is left
	empty and the caller carries on without sound. */
static int audio_open(const char *alias,const char *path){
	char cmd[256];
	sprintf(cmd,"open \"%s\" type waveaudio alias %s",path,alias);
	if(mci_send_log(cmd)!=0)
		return 0;
	return 1;
}

/*	Opens every device and starts the background music.

		Sound is best-effort: any failure leaves audio_available at 0 and
		the game runs silently. Returns 1 if sound is usable. */
int audio_init(){
	char cwd[1024];
	DWORD n;
	shot_next=0;
	bgm_open=0;
	blast_open=0;
	audio_available=0;

	/*	start from a clean log each run */
	fclose(fopen("audio_debug.txt","w"));

	/*	MCI resolves relative paths against the working directory, which is
		NOT necessarily the folder the exe sits in. Log it, because a
		wrong working directory is the usual reason the files open fine on
		one machine and fail on another. */
	n=GetCurrentDirectoryA(sizeof(cwd),cwd);
	audio_log("cwd=%s",n?cwd:"<GetCurrentDirectoryA failed>");
	{
		char exe[1024];
		DWORD m=GetModuleFileNameA(0,exe,sizeof(exe));
		audio_log("exe=%s",m?exe:"<GetModuleFileNameA failed>");
	}
	audio_log("--- init ---");

	if(!audio_open("bgm","assets\\background.wav")){
		/*	background music is not essential, but if it is missing the
			effects may still be present, so try them anyway */
	}
	else{
		bgm_open=1;
	}

	int i,shots=0;
	char alias[16];
	for(i=0;i<SHOT_POOL_SIZE;i++){
		sprintf(alias,"shot%d",i);
		if(audio_open(alias,"assets\\gunshot.wav"))
			shot_open[i]=1;
		else
			shot_open[i]=0;
		shots+=shot_open[i];
	}

	if(audio_open("blast","assets\\explode.wav"))
		blast_open=1;

	if(bgm_open||shots||blast_open)
		audio_available=1;

	/*	start the music only if it actually opened */
	if(bgm_open){
		/*	"repeat" loops the track and "wait 0" keeps the call off this
			thread. Both are standard MCI, but some drivers reject one or
			the other, so each form is tried and logged before giving up. */
		if(mci_send_log("play bgm repeat wait 0")!=0){
			audio_log("retrying without wait 0");
			if(mci_send_log("play bgm repeat")!=0){
				audio_log("retrying without repeat");
				if(mci_send_log("play bgm wait 0")!=0){
					/*	last resort: the plain form. This one does block
						until the track starts, but it is issued once at
						startup rather than during play */
					audio_log("retrying plain play");
					mci_send_log("play bgm");
				}
			}
		}
	}
	audio_log("bgm_open=%d audio_available=%d",bgm_open,audio_available);
	/*	audio_update_bgm() starts the loop from here; tell it the track is
		already playing so it does not restart it on the first frame */
	bgm_playing=bgm_open;
	return audio_available;
}

/*	Closes every device that was opened. Safe to call when audio_init()
	was never reached. */
void audio_shutdown(){
	int i;
	char cmd[64];
	if(bgm_open){
		mci_send("stop bgm");
		mci_send("close bgm");
		bgm_open=0;
	}
	if(blast_open){
		mci_send("stop blast");
		mci_send("close blast");
		blast_open=0;
	}
	for(i=0;i<SHOT_POOL_SIZE;i++){
		if(shot_open[i]){
			sprintf(cmd,"stop shot%d",i);
			mci_send(cmd);
			sprintf(cmd,"close shot%d",i);
			mci_send(cmd);
			shot_open[i]=0;
		}
	}
	audio_available=0;
}

/*	Keeps the background track looping.

		The MCI "repeat" modifier is rejected by some drivers (it fails on
		this setup with error 261), and "status ... current position" is
		rejected too, so neither can be used here. What does work is
		"status ... mode", which reports "playing" until the track ends
		and then "stopped". The loop is driven off that: once the mode is
		anything other than "playing", the track is restarted.

		Called once per frame from the main loop. */
void audio_update_bgm(){
	char buf[64];
	MCIERROR r;

	if(!bgm_open)
		return;

	buf[0]='\0';
	r=mciSendStringA("status bgm mode",buf,sizeof(buf),0);

	/*	a failed status is treated as "keep playing": restarting the track
		on every frame would make it stutter */
	if(r!=0)
		return;

	if(strcmp(buf,"playing")==0){
		bgm_playing=1;
		return;
	}

	/*	reached the end (or was stopped): start it again */
	if(bgm_playing){
		audio_log("bgm reached mode=%s, restarting",buf);
		bgm_playing=0;
	}
	mciSendStringA("play bgm from 0",NULL,0,0);
	bgm_playing=1;
}

/*	Plays one gunshot.

		Cycles round-robin through the pool so consecutive shots overlap
		instead of cutting each other off, and uses "wait 0" so a busy
		device can never stall the game loop. A busy alias simply drops the
		shot, which is preferable to blocking.

		Returns 1 if a shot was triggered. */
int audio_play_gunshot(){
	int i,tries;
	char cmd[64],buf[64];

	if(!audio_available)
		return 0;

	for(tries=0;tries<SHOT_POOL_SIZE;tries++){
		i=shot_next;
		shot_next=(shot_next+1)%SHOT_POOL_SIZE;
		if(!shot_open[i])
			continue;

		/*	Skip aliases that are still sounding. This driver rejects
			the "wait 0" modifier, so a plain "play" blocks until the
			device accepts it -- calling it on a busy device is what
			stalls the game loop. Checking the mode first means we only
			ever play into an idle device. */
		sprintf(cmd,"status shot%d mode",i);
		buf[0]='\0';
		if(mciSendStringA(cmd,buf,sizeof(buf),0)==0
			&&strcmp(buf,"playing")==0)
			continue;		//still audible, try the next alias

		sprintf(cmd,"play shot%d from 0",i);
		if(mciSendStringA(cmd,NULL,0,0)==0)
			return 1;
		/*	play failed even when idle: fall through to the next alias */
	}
	return 0;
}

/*	Plays the explosion. Non-blocking where the driver allows it. */
int audio_play_explosion(){
	char buf[64];
	if(!audio_available||!blast_open)
		return 0;
	/*	same busy check as the gunshot: only play into an idle device, so
		the plain (blocking) play never stalls the loop */
	buf[0]='\0';
	if(mciSendStringA("status blast mode",buf,sizeof(buf),0)==0
		&&strcmp(buf,"playing")==0)
		return 0;
	return mciSendStringA("play blast from 0",NULL,0,0)==0;
}
