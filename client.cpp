/*
 *	Client.cpp
 *  Programming Assignment #1
 *  CIS 532 Fall 2010
 *
 *	Nicholas Chaimov
 */

#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "duckchat.h"

// ***** FUNCTION DECLARATIONS *****

void restoreTerminal(void);
void setupTerminal(void);
void refreshAll(void);
void clearInput(void);

void printErrorMsg(const char * msg);
void printWarnMsg(const char * msg);

bool handleInput(int sock, struct addrinfo * p);

void handleNetwork(int sock, struct addrinfo * p);
void sendLoginPacket(int sock, struct addrinfo * p, const char * userName);
void sendLogoutPacket(int sock, struct addrinfo * p);
void sendJoinPacket(int sock, struct addrinfo * p, const char * channelName);
void sendLeavePacket(int sock, struct addrinfo * p, const char * channelName);
void sendSayPacket(int sock, struct addrinfo * p, const char * channelName, const char * msg);
void sendListPacket(int sock, struct addrinfo * p);
void sendWhoPacket(int sock, struct addrinfo * p, const char * channelName);
void sendKeepAlivePacket(int sock, struct addrinfo * p);

void timerExpired(int signum);

// ***** CONSTANTS *****

const int MAX_NUM_CHANNELS = 32;
const int MAX_NUM_USERS = 32;
const size_t MAX_BUFFER_SIZE = sizeof(text) + (CHANNEL_MAX*MAX_NUM_CHANNELS) + (USERNAME_MAX*MAX_NUM_USERS);
const int KEEP_ALIVE_FREQ = 60;
const int MAXLINE = 64;

// ***** GLOBAL VARIABLES *****

WINDOW * wnd;			// The large window where messages are written.
WINDOW * inputWnd;		// The small window at the bottom where input is typed.

// Size of terminal:
int termRows;			
int termCols;

char curChannel[CHANNEL_MAX+1];	// Which channel we're in now

int nchars = 0;			// Number of characters of input we've read.
char * line;			// The line of input we're reading.

int sock = -1;			// Socket's file descriptor ID
text * buf;				// Space to store incoming packets.
bool timeForKeepAlive = false;
std::set<std::string> channelsJoined;

// Apparently Solaris doesn't have strnlen built in.
static inline size_t strnlen(const char *s, size_t max) {
    register const char *p;
    for(p = s; *p && max--; ++p);
    return(p - s);
}

int main(int argc, char ** argv) {
    if(argc != 4) {
        std::cerr << "usage: " << argv[0] << " server_name port user_name" << std::endl;
        exit(-1);
    }
    char * hostName = argv[1];
    int portNum = atoi(argv[2]);
    char * portNumStr = argv[2];
    char * userName = argv[3];

    if(portNum < 0 || portNum > 65535) {
        std::cerr << "error: port number must be between 0 and 65535" << std::endl;
        exit(-2);
    } 

    if(strnlen(userName, USERNAME_MAX+1) > USERNAME_MAX) {
        std::cerr << "error: username can be no longer than " << USERNAME_MAX << " characters" << std::endl;
        exit(-3);
    }

    struct addrinfo hints, *servinfo, *p;
    int status;
    int numbytes;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    status = getaddrinfo(hostName, portNumStr, &hints, &servinfo);

    if(status != 0) {
        std::cerr << "error: unable to resolve address: " << gai_strerror(status) << std::endl;
        exit(-4);
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(sock == -1) {
            std::cerr << "warning: could not connect to socket" << std::endl;
            continue;
        }
        break;
    }

    if(p == NULL) {
        std::cerr << "error: all sockets failed to connect" << std::endl;
        exit(-5);
    }
	
	memset(curChannel, 0, CHANNEL_MAX);
	strncpy(curChannel, "Common", CHANNEL_MAX);
	channelsJoined.insert(curChannel);
	
	buf = (text *) malloc(MAX_BUFFER_SIZE);

    fcntl(sock, F_SETFL, O_NONBLOCK);

    sendLoginPacket(sock, p, userName);
    sendJoinPacket(sock, p, "Common");

    atexit(restoreTerminal);
    setupTerminal();

    line = new char[MAXLINE+1];
	memset(line, '\0', MAXLINE+1);

	clearInput();
    refreshAll();

	signal(SIGALRM, timerExpired);
	alarm(KEEP_ALIVE_FREQ);

    while(handleInput(sock, p)) {
    }

    sendLogoutPacket(sock, p);

    delete [] line; 
	free(buf);

    return 0;
}

void sendLoginPacket(int sock, struct addrinfo * p, const char * userName) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_login packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_LOGIN);
    strncpy(packet.req_username, userName, USERNAME_MAX);
    int status = sendto(sock, &packet, sizeof(struct request_login), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		std::cerr << "error: failed to send login packet" << std::endl;
		exit(-5);
    }
}

void sendLogoutPacket(int sock, struct addrinfo * p) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_logout packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_LOGOUT);
    int status = sendto(sock, &packet, sizeof(struct request_logout), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send logout packet");
    }
}

void sendJoinPacket(int sock, struct addrinfo * p, const char * channelName) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_join packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_JOIN);
    strncpy(packet.req_channel, channelName, CHANNEL_MAX);
    int status = sendto(sock, &packet, sizeof(struct request_join), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send join packet");
    }
}

void sendLeavePacket(int sock, struct addrinfo * p, const char * channelName) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_leave packet;
    packet.req_type = htonl(REQ_LEAVE);
    strncpy(packet.req_channel, channelName, CHANNEL_MAX);
    int status = sendto(sock, &packet, sizeof(struct request_leave), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send leave packet");
    }
}

void sendSayPacket(int sock, struct addrinfo * p, const char * channelName, const char * msg) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_say packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_SAY);
    strncpy(packet.req_channel, channelName, CHANNEL_MAX);
	strncpy(packet.req_text, msg, SAY_MAX);
    int status = sendto(sock, &packet, sizeof(struct request_say), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send say packet");
    }
}

void sendListPacket(int sock, struct addrinfo * p) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_list packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_LIST);
    int status = sendto(sock, &packet, sizeof(struct request_list), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send list packet");
    }
}

void sendWhoPacket(int sock, struct addrinfo * p, const char * channelName) {
	alarm(KEEP_ALIVE_FREQ);
    struct request_who packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_WHO);
    strncpy(packet.req_channel, channelName, CHANNEL_MAX);
    int status = sendto(sock, &packet, sizeof(struct request_who), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send who packet");
    }
}

void sendKeepAlivePacket(int sock, struct addrinfo * p) {
    struct request_keep_alive packet;
	memset(&packet, '\0', sizeof(packet));
    packet.req_type = htonl(REQ_KEEP_ALIVE);
    int status = sendto(sock, &packet, sizeof(struct request_keep_alive), 0, p->ai_addr, p->ai_addrlen);
    if(status == -1) {
		printErrorMsg("unable to send keep-alive packet");
    }
}

void handleNetwork(int sock, struct addrinfo * p) {
	struct sockaddr_storage fromAddr;
	socklen_t fromAddrLen = sizeof(fromAddr);
	int recvSize = recvfrom(sock, buf, MAX_BUFFER_SIZE, 0, (struct sockaddr *)&fromAddr, &fromAddrLen);
	if(recvSize > 0) {
		
		if(recvSize >= sizeof(text)) {
			buf->txt_type = ntohl(buf->txt_type);
			switch(buf->txt_type) {
				
				case TXT_SAY:
					if(recvSize >= sizeof(text_say)) {
						text_say * pkt = (text_say *) buf;
						wprintw(wnd, "[");
						wattron(wnd, COLOR_PAIR(3));
						wprintw(wnd, "%.32s", pkt->txt_channel);
						wattroff(wnd, COLOR_PAIR(3));
						wprintw(wnd, "][");
						wattron(wnd, COLOR_PAIR(4));
						wprintw(wnd, "%.32s", pkt->txt_username);
						wattroff(wnd, COLOR_PAIR(4));
						wprintw(wnd, "]: %.64s\n", pkt->txt_text);
						refreshAll();
					} else {
						char err[256];
						snprintf(err, 256, "say packet should be at least %d bytes, but got %d", sizeof(text_say), recvSize);
						printWarnMsg(err);
					}
					break;
					
				case TXT_ERROR:
					if(recvSize >= sizeof(text_error)) {
						text_error * pkt = (text_error *) buf;
						char say[SAY_MAX+1];
						strncpy(say, pkt->txt_error, SAY_MAX);
						say[SAY_MAX] = '\0';
						printErrorMsg(say);
					} else {
						char err[256];
						snprintf(err, 256, "error packet should be at least %d bytes, but got %d", sizeof(text_error), recvSize);
						printWarnMsg(err);
					}
					break;
					
				case TXT_LIST:
					if(recvSize >= sizeof(text_list)) {
						text_list * pkt = (text_list *)buf;
						pkt->txt_nchannels = htonl(pkt->txt_nchannels);
						const size_t expectedSize = sizeof(text_list) + 
													(pkt->txt_nchannels * sizeof(channel_info));
						if(recvSize >= expectedSize) {
							wattron(wnd, A_BOLD);
							wprintw(wnd, "Existing channels:\n");
							wattroff(wnd, A_BOLD);
							for(int i = 0; i < pkt->txt_nchannels; ++i) {
								wattron(wnd, COLOR_PAIR(3));
								wprintw(wnd, "\t%.32s\n", pkt->txt_channels[i].ch_channel);
								wattroff(wnd, COLOR_PAIR(3));
							}
							refreshAll();
						} else {
							char err[256];
							snprintf(err, 256, "list packet should be at least %d bytes, but got %d", expectedSize, recvSize);
							printWarnMsg(err);
						}
					} else {
						char err[256];
						snprintf(err, 256, "list packet should be at least %d bytes, but got %d", sizeof(text_list), recvSize);
						printWarnMsg(err);
						
					}
					break;
					
				case TXT_WHO:
					if(recvSize >= sizeof(text_who)) {
						text_who * pkt = (text_who *)buf;
						pkt->txt_nusernames = htonl(pkt->txt_nusernames);
						const size_t expectedSize = sizeof(text_who) + 
													(pkt->txt_nusernames * sizeof(user_info));
						if(recvSize >= expectedSize) {
							wattron(wnd, A_BOLD);
							wprintw(wnd, "Users on channel ");
							wattron(wnd, COLOR_PAIR(3));
							wprintw(wnd, "%.32s", pkt->txt_channel);
							wattroff(wnd, COLOR_PAIR(3));
							wprintw(wnd, ":\n");
							wattroff(wnd, A_BOLD);
							for(int i = 0; i < pkt->txt_nusernames; ++i) {
								wattron(wnd, COLOR_PAIR(4));
								wprintw(wnd, "\t%.32s\n", pkt->txt_users[i].us_username);
								wattroff(wnd, COLOR_PAIR(4));
							}
							refreshAll();
						} else {
							char err[256];
							snprintf(err, 256, "list packet should be at least %d bytes, but got %d", expectedSize, recvSize);
							printWarnMsg(err);
						}
					} else {
						char err[256];
						snprintf(err, 256, "list packet should be at least %d bytes, but got %d", sizeof(text_list), recvSize);
						printWarnMsg(err);

					}
					break;

				
				default:
					char err[256];
					snprintf(err, 256, "got an unrecognized packet type %d", buf->txt_type);
					printWarnMsg(err);
					
			}
		} else {
			char err[256];
			snprintf(err, 256, "expected at least %d bytes, but got %d", sizeof(text), recvSize);
			printWarnMsg(err);
		}
		
	}
}

bool handleInput(int sock, struct addrinfo * p) {
    int charRead;
    while(true)  {
		if(timeForKeepAlive) {
			sendKeepAlivePacket(sock, p);
			timeForKeepAlive = false;
		}
		handleNetwork(sock, p);
        charRead = wgetch(inputWnd);        
        if(charRead == 127 && nchars > 0) {
            int cy, cx;
            getyx(inputWnd, cy, cx);
            mvwaddch(inputWnd, cy, cx-1, ' ');
			wmove(inputWnd, cy, cx-1);
            line[nchars] = '\0';
            --nchars;
            return true;
        } else if(charRead >= 32 && charRead <= 126 && nchars < MAXLINE) {
            wechochar(inputWnd, charRead);
            line[nchars] = charRead;
            refreshAll();
            ++nchars;
            return true;
        } else if(charRead == '\n') {
            if(strncmp(line, "/exit", std::max(nchars, MAXLINE+1)) == 0) {
                return false;
            } else if(strncmp(line, "/join ", 6) == 0) {
                sendJoinPacket(sock, p, &(line[6]));
				memset(curChannel, '\0', CHANNEL_MAX+1);
				strncpy(curChannel, &(line[6]), CHANNEL_MAX);
				channelsJoined.insert(curChannel);
			} else if(strncmp(line, "/switch ", 8) == 0) {
				char chanName[CHANNEL_MAX+1];
				memset(chanName, '\0', CHANNEL_MAX+1);
				strncpy(chanName, &(line[8]), CHANNEL_MAX);
				if(channelsJoined.count(chanName) > 0) {
					strncpy(curChannel, chanName, CHANNEL_MAX);
				} else {
					char err[256];
					snprintf(err, 256, "you have not subscribed to channel %.32s", chanName);
					printErrorMsg(err);
				}
            } else if(strncmp(line, "/leave ", 7) == 0) {
                sendLeavePacket(sock, p, &(line[7]));
				memset(curChannel, '\0', CHANNEL_MAX+1);
				strncpy(curChannel, &(line[7]), CHANNEL_MAX);
				channelsJoined.erase(curChannel);
				memset(curChannel, '\0', CHANNEL_MAX);
			} else if(strncmp(line, "/list", 5) == 0) {
				sendListPacket(sock, p);
			} else if(strncmp(line, "/who ", 5) == 0) {
				sendWhoPacket(sock, p, &(line[5]));
            } else if(line[0] == '/') {
				printErrorMsg("unrecognized command");
			} else if(curChannel[0] != '\0'){
				sendSayPacket(sock, p, curChannel, line);
			}
            //wprintw(wnd, "%s\n", line);
            memset(line, '\0', MAXLINE+1);
            clearInput();
            return true;
        }
    };
}

void printErrorMsg(const char * msg) {
	wattron(wnd, COLOR_PAIR(1));
	wprintw(wnd, "Error: ");
	wattroff(wnd, COLOR_PAIR(1));
	wprintw(wnd, "%s\n", msg);
	refreshAll();
}

void printWarnMsg(const char * msg) {
	wattron(wnd, COLOR_PAIR(2));
	wprintw(wnd, "Warning: ");
	wattroff(wnd, COLOR_PAIR(2));
	wprintw(wnd, "%s\n", msg);
	refreshAll();
}

void clearInput(void) {
	nchars = 0;
	wmove(inputWnd, 1, 0);
	whline(inputWnd, ' ', termCols);
	int chanLen = strnlen(curChannel, CHANNEL_MAX);
	if(termCols - chanLen - 64 > 0) {
		wmove(inputWnd, 1, termCols - chanLen);
		wattron(inputWnd, COLOR_PAIR(5));
		wprintw(inputWnd, "%.32s", curChannel);
	wattroff(inputWnd, COLOR_PAIR(5));
	}
	wmove(inputWnd, 1, 0);
	waddch(inputWnd, '>' | A_BOLD);
	waddch(inputWnd, ' ');
	refreshAll();
}

void refreshAll(void) {
	wrefresh(wnd);
	wrefresh(inputWnd);
}

void setupTerminal(void) {
    wnd = initscr();
    getmaxyx(wnd,termRows,termCols);
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
	init_pair(3, COLOR_GREEN, COLOR_BLACK);
	init_pair(4, COLOR_CYAN, COLOR_BLACK);
	init_pair(5, COLOR_BLACK, COLOR_WHITE);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
	wresize(wnd, termRows-3, termCols);
	wsetscrreg(wnd, 0, termRows-3);
	scrollok(wnd, true);
    inputWnd = newwin(3, termCols, termRows-3, 0);
    whline(inputWnd, '-', termCols);
    wmove(inputWnd, 1, 0);
	nodelay(wnd, true);
	nodelay(inputWnd, true);
}


void restoreTerminal(void) {
    delwin(inputWnd);
    endwin();
    if(sock > 0) {
        close(sock);
    }
}

void timerExpired(int signum) {
	timeForKeepAlive = true;
	signal(SIGALRM, timerExpired);
	alarm(KEEP_ALIVE_FREQ);
}
