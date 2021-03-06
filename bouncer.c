/* 
 
Filename:  bouncer.c
Version:  1.0
Created:  04/08/2013 06:45:04 PM
Revision:  none
Compiler:  gcc

Make: gcc -g bouncer.c -lncurses -lrt -o bouncer
 

Author:  Art Diky
Company:  Hunter College
Description: An event-driven game demonstarting the usage of library 
NCurses and asynchronus IO handling. 
The program makes a simple animation and receives user input to control the
speed/direction of animation
The program installs two handlers, one to process asynchronus 
user input and the other to process timer events to adjust 
the speed of animation
Modified to contain a data race that causes an assertion 
failure in function update_from_input()

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <curses.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <aio.h>
#include <assert.h>

#define INITIAL_SPEED 50
#define RIGHT 1
#define LEFT -1
#define ROW 12
#define MESSAGE "ooooooo=>"
#define REVMSGE "<=ooooooo"
#define BLANK   "         "

int dir;   // Direction of movement
int speed; // Chars per second
int row;   // Current row
int col;   // Current column
int is_changed = FALSE;
int finished;
volatile sig_atomic_t input_ready;
struct aiocb kbcbuf;

void on_alarm(int);          //Event-handler for Timer events
void on_input(int);          // Event-handler for User Inputs
int update_from_input(int*, int*);
void setup_aio_buffer(struct aiocb *buff);

int set_timer(int which, long initial, long repeat)
{
    struct itimerval itimer;
    long secs;
                
    // Init delay
    secs = initial / 1000;
    itimer.it_value.tv_sec  = secs;
    itimer.it_value.tv_usec = (initial - secs*1000) * 1000;
        
    // Init repeat interval
    secs = repeat / 1000;
    itimer.it_interval.tv_sec  = secs;
    itimer.it_interval.tv_usec = (repeat - secs*1000) * 1000;
        
    return setitimer(which, &itimer, NULL);
}

int main(int argc, char *argv[])
{
    // Setup signal handling
    struct sigaction newhandler;
    sigset_t         blocked;
   

    newhandler.sa_handler = on_input;   // name of handler
    newhandler.sa_flags   = SA_RESTART; // restart handler on execution
    sigemptyset(&blocked);              // create empty set of blocked sirnals
    newhandler.sa_mask = blocked;       // setup this set to be mask
    if( sigaction(SIGIO, &newhandler, NULL) == -1)
        perror("sigaction");

    newhandler.sa_handler = on_alarm;  // set handler for SIGALRM
    if ( sigaction(SIGALRM, &newhandler, NULL) == -1)
        perror("sigaction");

    // Prepare terminal
    initscr();
    cbreak();
    noecho();
    clear();
    curs_set(0); // hide cursor

    // Initialize parameters
    row = ROW;
    col = 0;
    dir = RIGHT;
    finished = 0;
    speed = INITIAL_SPEED; 

    // Turn on keyboard signals
    setup_aio_buffer(&kbcbuf);
    aio_read(&kbcbuf);

    // Start the real timer
    set_timer(ITIMER_REAL, 1000/ speed, 1000/speed);

    mvaddstr(LINES-1, 0, "Current speed:");
    refresh();

    /* Put the message into the first position and start */
    mvaddstr(row, col, MESSAGE);
    // Start Event-loop
    while(!finished)
    {
        if(input_ready)
        {
            finished = update_from_input(&speed, &dir);
            input_ready = 0;
        }
        else
            pause();
    }

    // Clean up
    endwin();
    return 0;
}

void on_alarm(int signum)
{
    static int row = ROW;
    static int col = 0;
    char message[40];
    move(row,col);
    addstr(BLANK);  // erase old string
    col += dir;
    move(row, col);
    if (dir == RIGHT)
    {
        addstr(MESSAGE);
        if (col+strlen(MESSAGE) >= COLS+1)
            dir = LEFT;
    }
    else 
    {
        addstr(REVMSGE);
        if (col <= 0)
            dir = RIGHT;
    }
    
    move (LINES -1, 0);
    sprintf(message, "Current speed: %d (chr/s)", speed);
    addstr(message);
    refresh();
    is_changed= FALSE;	
}

void on_input(signum)
{
    input_ready = 1;
}

// Handler called when aio_read() has something to read
int update_from_input(int *speed, int *dir)
{
    int c;
    int numTimerAdjustments = 0;
	 int numSpeedChangeRequests = 0;
    char mssg[40];
    char *cp = (char *) kbcbuf.aio_buf; // cast to char
    finished = FALSE;

    // Check for errors
    if ( aio_error(&kbcbuf) == 1)
    {
        perror("reading failed");
    }
    else
        // get number of char to read
        if (aio_return(&kbcbuf) == 1)
        {
				
            c = *cp;
            /*ndelay = 0;*/
            switch(c)
            {
                case 'Q':
                case 'q':
                    finished = 1;    // quit program
                    break;

                case ' ':
                    *dir = ((*dir) == LEFT) ? RIGHT: LEFT;
                    break;
                
                case 'f':
                    if (1000 / *speed > 2 )
                    {
                        *speed = *speed + 2;
                        is_changed = TRUE;
                    }
						  numSpeedChangeRequests++;
                    break;
                case 's':
                    if (1000 / *speed <= 500)
                    {
                        *speed = *speed - 2;
                        is_changed = TRUE;
                    }
 						  numSpeedChangeRequests++;
                    break;
            }
		//usleep(500); /Uncomment it to see the affect of race*/
	    if (is_changed)
           {
               set_timer(ITIMER_REAL, 1000 / *speed, 1000 / *speed);
               sprintf(mssg, "Current speed: %d (c/s)", *speed);
               mvaddstr(LINES-1, 0, mssg);
	      numTimerAdjustments++;
           }
            assert(numSpeedChangeRequests==numTimerAdjustments); 
            move(LINES-1, COLS-12);
            sprintf(mssg, "Last char: %c", c);
            addstr(mssg);
            refresh();

        }
    
    // Place new request
    aio_read(&kbcbuf);
    return finished;
}

void setup_aio_buffer(struct aiocb *buf)
{
    static char input[1];

    // describe what to read
    buf->aio_fildes    = 0;      // file descriptor for I/O
    buf->aio_buf       = input;  // address of buffer
    buf->aio_nbytes    = 1;      // buffer size
    buf->aio_offset    = 0;    

    // describe what to do when read is ready
    buf->aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    buf->aio_sigevent.sigev_signo  = SIGIO;        // send SIGIO
}
