#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <map>
#include <vector>
#include <list>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "duckchat.h"

using namespace std;

class User;
class Channel;

void timerExpired(int signum);

const int MAX_NUM_CHANNELS = 32;
const int MAX_NUM_USERS = 32;
const int KEEP_ALIVE_DELAY = 60;

static inline size_t strnlen(const char *s, size_t max) {
    register const char *p;
    for(p = s; *p && max--; ++p);
    return(p - s);
}

class User {
public:
	string name;
	string key;
	struct sockaddr_storage * address;
	list<Channel *> channels;
	bool seen;
	
	User(const string n, const string k, struct sockaddr_storage * a) : name(n), key(k), address(a), seen(true) {};
	
	virtual ~User() {
		delete address;
	};
};

class Channel {
public:
	string name;
	list<User *> users;
	
	Channel(const string n) : name(n) {};
	
	virtual ~Channel() {}; 
};


map<string, User *> users;
map<string, Channel *> channels;

void sendError(User * user, string msg);

bool isUserInChannel(User * user, Channel * channel) {
	if(user == NULL) {
		cerr << "User was null!" << endl;
		return false;
	}
	if(channel == NULL) {
		cerr << "Channel was null!" << endl;
		return false;
	}
	list<User *>::iterator it = find(channel->users.begin(), channel->users.end(), user);
	if(it == channel->users.end()) {
		return false;
	} else {
		return true;
	}
}

void addUserToChannel(User * user, Channel * channel) {
	if(user == NULL) {
		cerr << "User was null!" << endl;
		return;
	}
	if(channel == NULL) {
		cerr << "Channel was null!" << endl;
		return;
	}
	if(isUserInChannel(user, channel)) {
		sendError(user, "Already in that channel!");
		return;
	}
	if(channel->users.size() > MAX_NUM_USERS) {
		sendError(user, "Channel is full!");
		return;
	}
	user->channels.push_back(channel);
	channel->users.push_back(user);
	cout << "User " << user->name << " added to channel " << channel->name << endl;
}

void addUserToChannelNamed(User * user, string name) {
	if(user == NULL) {
		cerr << "User was null!" << endl;
		return;
	}
	
	map<string, Channel *>::iterator it = channels.find(name);
	if(it == channels.end() || (*it).second == NULL) {
		int n = 0;
		for(map<string, Channel *>::iterator it = channels.begin(); it != channels.end(); ++it) {
			Channel * ch = (*it).second;
			if(ch != NULL) {
				n++;
			}
		}
		if(n > MAX_NUM_CHANNELS) {
			sendError(user, "Too many channels!");
			return;
		}
		channels[name] = new Channel(name);
	}
	addUserToChannel(user, channels[name]);
}

void removeUserFromChannel(User * user, Channel * channel) {
	if(user == NULL) {
		cerr << "User was null!" << endl;
		return;
	}
	if(channel == NULL) {
		cerr << "Channel was null!" << endl;
		return;
	}
	
	if(!isUserInChannel(user, channel)) {
		sendError(user, "Not in that channel!");
		return;
	}
	if(channel->name.compare("Common") == 0) {
		sendError(user, "You can't leave Common!");
		return;
	}
	user->channels.remove(channel);
	channel->users.remove(user);
	cout << "User " << user->name << " removed from channel " << channel->name << endl;
	if(channel->users.empty()) {
		cout << "Removing channel " << channel->name << " because it has no users" << endl;
		channels[channel->name] = NULL;
		delete channel;
	}
}

void removeUserFromChannelNamed(User * user, string name) {
	if(user == NULL) {
		cerr << "User was null!" << endl;
		return;
	}
	
	map<string, Channel *>::iterator it = channels.find(name);
	if(it == channels.end() || (*it).second == NULL) {
		sendError(user, "Can't leave a nonexistent channel");
	} else {
		removeUserFromChannel(user, channels[name]);
	}
}


void removeUserFromAllChannels(User * user) {
	if(user == NULL) {
		cerr << "User was null!" << endl;
		return;
	}	
	cout << "Removing user " << user->name << " from all channels. " << endl;
	for(map<string, Channel *>::iterator it = channels.begin(); it != channels.end(); ++it) {
		Channel * ch = (*it).second;
		if(ch != NULL) {
			ch->users.remove(user);
			cout << "User " << user->name << " removed from channel " << ch->name << endl;
		}
	}
	user->channels.clear();
}


int sock;
struct addrinfo *p;

void sendError(User * user, string msg) {
	cout << "Sending error: " << msg << endl;
	if(user == NULL) {
		cerr << "Tried to send error to unknown user" << endl;
		return;
	}
	struct text_error pkt;
	pkt.txt_type = htonl(TXT_ERROR);
	strncpy(pkt.txt_error, msg.c_str(), SAY_MAX);
	int status = -1;
	size_t addrSize;
	if(user->address->ss_family == AF_INET) {
		addrSize = sizeof(sockaddr);
	} else {
		addrSize = sizeof(sockaddr_in6);
	}
	status = sendto(sock, &pkt, sizeof(text_error), 0, (sockaddr *) user->address, addrSize);
	if(status == -1) {
		perror("while sending error");
	}
}

void logout(User * user) {
	if(user == NULL) {
		cerr << "Tried to log out an unknown user" << endl;
		return;
	}
	
	removeUserFromAllChannels(user);
	users[user->key] = NULL;
	delete user;
}

void say(User * user, Channel * channel, char * msg) {
	if(user == NULL) {
		cerr << "Unknown user tried to say something!" << endl;
		return;
	}
	if(channel == NULL) {
		sendError(user, "Channel you sent to doesn't exist");
		return;
	}
	if(!isUserInChannel(user, channel)) {
		sendError(user, "You aren't in that channel!");
		return;
	}
	cout << "[" << channel->name << "][" << user->name << "]: " << msg << endl;
	struct text_say pkt;
	pkt.txt_type = htonl(TXT_SAY);
	strncpy(pkt.txt_channel, channel->name.c_str(), CHANNEL_MAX);
	strncpy(pkt.txt_username, user->name.c_str(), USERNAME_MAX);
	strncpy(pkt.txt_text, msg, SAY_MAX);
	for(list<User *>::iterator it = channel->users.begin(); it != channel->users.end(); ++it) {
		User * u = *it;
		if(u != NULL) {
			cerr << "sock: " << sock << ", pkt: " << &pkt << endl;
			cerr << "user: " << u->key << endl;
			size_t addrSize;
			if(u->address->ss_family == AF_INET) {
				addrSize = sizeof(sockaddr);
			} else {
				addrSize = sizeof(sockaddr_in6);
			}
			int status = sendto(sock, &pkt, sizeof(text_say), 0, (sockaddr *) u->address, addrSize);
			if(status == -1) {
				cerr << "When trying to send say to " << u->name << endl;
				perror("while sending say");
			}
		}
	}
}

void listChannels(User * user) {
	if(user == NULL) {
		cerr << "Tried to send channel list to unknown user" << endl;
		return;
	}
	cout << "Sending channel list to " << user->name << endl;
	list<Channel *> channelsToSend;
	int n = 0;
	for(map<string, Channel *>::iterator it = channels.begin(); it != channels.end(); ++it) {
		Channel * ch = (*it).second;
		if(ch != NULL) {
			channelsToSend.push_back(ch);
			n++;
		}
	}
	const size_t pktSize = sizeof(text_list) + (n * sizeof(channel_info));
	struct text_list * pkt = (text_list *) malloc( pktSize );
	memset(pkt, '\0', pktSize);
	pkt->txt_type = htonl(TXT_LIST);
	pkt->txt_nchannels = htonl(n);
	int i = 0;
	for(list<Channel *>::iterator it = channelsToSend.begin(); it != channelsToSend.end(); ++it) {
		strncpy(pkt->txt_channels[i++].ch_channel, (*it)->name.c_str(), CHANNEL_MAX);
	}
	size_t addrSize;
	if(user->address->ss_family == AF_INET) {
		addrSize = sizeof(sockaddr);
	} else {
		addrSize = sizeof(sockaddr_in6);
	}
	int status = sendto(sock, pkt, pktSize, 0, (sockaddr *) user->address, addrSize);
	if(status == -1) {
		perror("while sending channel list");
	}
	free(pkt);
}

void who(User * user, Channel * channel) {
	if(user == NULL) {
		cerr << "Tried to send who list to unknown user" << endl;
		return;
	}
	if(channel == NULL) {
		sendError(user, "You tried to show members of a nonexistent channel!");
		return;
	}
	cout << "Sending who list to " << user->name << " for channel " << channel->name << endl;
	int n = channel->users.size();
	const size_t pktSize = sizeof(text_who) + (n * sizeof(user_info));
	struct text_who * pkt = (text_who *) malloc( pktSize );
	memset(pkt, '\0', pktSize);
	pkt->txt_type = htonl(TXT_WHO);
	pkt->txt_nusernames = htonl(n);
	strncpy(pkt->txt_channel, channel->name.c_str(), CHANNEL_MAX);
	int i = 0;
	for(list<User *>::iterator it = channel->users.begin(); it != channel->users.end(); ++it) {
		strncpy(pkt->txt_users[i++].us_username, (*it)->name.c_str(), USERNAME_MAX);
	}
	size_t addrSize;
	if(user->address->ss_family == AF_INET) {
		addrSize = sizeof(sockaddr);
	} else {
		addrSize = sizeof(sockaddr_in6);
	}
	int status = sendto(sock, pkt, pktSize, 0, (sockaddr *) user->address, addrSize);
	if(status == -1) {
		perror("while sending who list");
	}
	free(pkt);
}

void timerExpired(int signum) {
	for(map<string, User *>::iterator it = users.begin(); it != users.end(); ++it) {
		User * u = (*it).second;
		if(u != NULL) {
			if(!u->seen) {
				cout << "Logging out user " << u->name << " due to inactivity" << endl;
				logout(u);
			}
			u->seen = false;
		}
	}
	signal(SIGALRM, timerExpired);
	alarm(KEEP_ALIVE_DELAY);
}

int main(int argc, char ** argv) {
	
	signal(SIGALRM, timerExpired);
	alarm(KEEP_ALIVE_DELAY);
	
	if(argc != 3) {
        std::cerr << "usage: " << argv[0] << " server_name port" << std::endl;
        exit(-1);
    }
    char * hostName = argv[1];
    int portNum = atoi(argv[2]);
    char * portNumStr = argv[2];

    if(portNum < 0 || portNum > 65535) {
        std::cerr << "error: port number must be between 0 and 65535" << std::endl;
        exit(-2);
    } 

    struct addrinfo hints, *servinfo;
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

	if (bind(sock, p->ai_addr, p->ai_addrlen) == -1) {
        close(sock);
		std::cerr << "unable to bind socket" << std::endl;
    }

   // fcntl(sock, F_SETFL, O_NONBLOCK);
    
	char ipstr[INET6_ADDRSTRLEN];
	int port;

	if (p->ai_addr->sa_family == AF_INET) {
	    struct sockaddr_in *s = (struct sockaddr_in *)&(p->ai_addr);
	    port = ntohs(s->sin_port);
	    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
	} else {
	    struct sockaddr_in6 *s = (struct sockaddr_in6 *)&(p->ai_addr);
	    port = ntohs(s->sin6_port);
	    inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
	}
	
	Channel * common = new Channel("Common");
	channels["Common"] = common;
		
	cout << "Waiting for packets on " << ipstr << " local port " << port << endl;
		
	request * buf = (request *) malloc( sizeof(request_say) );
	 
	while(true) {
		struct sockaddr_storage * fromAddr = new struct sockaddr_storage;
		socklen_t fromAddrLen = sizeof(sockaddr_storage);
		int recvSize = recvfrom(sock, buf, sizeof(request_say), 0, (struct sockaddr *)fromAddr, &fromAddrLen);
		if(recvSize != -1) {
			if (fromAddr->ss_family == AF_INET) {
			    struct sockaddr_in *s = (struct sockaddr_in *)fromAddr;
			    port = ntohs(s->sin_port);
			    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
			} else {
			    struct sockaddr_in6 *s = (struct sockaddr_in6 *)fromAddr;
			    port = ntohs(s->sin6_port);
			    inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
			}
		
			char ip_port_str[INET6_ADDRSTRLEN + 30];
			snprintf(ip_port_str, INET6_ADDRSTRLEN + 30, "%s/%d", ipstr, port);
		
			if(recvSize >= sizeof(request)) {
				if(users[ip_port_str] != NULL) {
					users[ip_port_str]->seen = true;
				}
				buf->req_type = ntohl(buf->req_type);

				switch(buf->req_type) {
					case REQ_LOGIN:
						if(recvSize >= sizeof(request_login)) {
							request_login * pkt = (request_login *)buf;
							char userName[USERNAME_MAX+1];
							memset(userName, '\0', USERNAME_MAX+1);
							strncpy(userName, pkt->req_username, USERNAME_MAX);
							User * user = new User(userName, ip_port_str, fromAddr);
							if(strnlen(userName, USERNAME_MAX) == 0) {
								sendError(user, "Username length must be non-zero");
								delete user;
							} else {
								users[ip_port_str] = user;
								cout << "User " << user->name << " logged in from " << ip_port_str << endl;
								//addUserToChannel(user, common);
							}
						} else {
							cerr << "Expected a login packet to have " << sizeof(request_login) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;	
					
					case REQ_LOGOUT:
						if(recvSize >= sizeof(request_logout)) {
							cout << "User " << users[ip_port_str]->name << " logged out." << endl;
							logout(users[ip_port_str]);
						} else {
							cerr << "Expected a logout packet to have " << sizeof(request_logout) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;	
				
					case REQ_JOIN:
						if(recvSize >= sizeof(request_join)) {
							request_join * pkt = (request_join *)buf;
							User * user = users[ip_port_str];
							char chanName[CHANNEL_MAX+1];
							memset(chanName, '\0', CHANNEL_MAX+1);
							strncpy(chanName, pkt->req_channel, CHANNEL_MAX);
							addUserToChannelNamed(user, chanName);
						} else {
							cerr << "Expected a join packet to have " << sizeof(request_join) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;	
					
					case REQ_LEAVE:
						if(recvSize >= sizeof(request_leave)) {
							request_leave * pkt = (request_leave *)buf;
							User * user = users[ip_port_str];
							char chanName[CHANNEL_MAX+1];
							memset(chanName, '\0', CHANNEL_MAX+1);
							strncpy(chanName, pkt->req_channel, CHANNEL_MAX);
							removeUserFromChannelNamed(user, chanName);
						} else {
							cerr << "Expected a leave packet to have " << sizeof(request_leave) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;	
					
					case REQ_SAY:
						if(recvSize >= sizeof(request_say)) {
							request_say * pkt = (request_say *)buf;
							User * user = users[ip_port_str];
							char chanName[CHANNEL_MAX+1];
							memset(chanName, '\0', CHANNEL_MAX+1);
							strncpy(chanName, pkt->req_channel, CHANNEL_MAX);
							Channel * channel = channels[chanName];
							say(user, channel, pkt->req_text);
						} else {
							cerr << "Expected a say packet to have " << sizeof(request_leave) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;	
				
					case REQ_LIST:
						if(recvSize >= sizeof(request_list)) {
							User * user = users[ip_port_str];
							listChannels(user);
						} else {
							cerr << "Expected a list packet to have " << sizeof(request_logout) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;	
				
					case REQ_WHO:
						if(recvSize >= sizeof(request_who)) {
							request_who * pkt = (request_who *) buf;
							User * user = users[ip_port_str];
							char chanName[CHANNEL_MAX+1];
							memset(chanName, '\0', CHANNEL_MAX+1);
							strncpy(chanName, pkt->req_channel, CHANNEL_MAX);
							Channel * channel = channels[chanName];
							who(user, channel);
						} else {
							cerr << "Expected a who packet to have " << sizeof(request_logout) << " bytes, but got " << recvSize << " bytes." << endl;
						}
						break;
					
					case REQ_KEEP_ALIVE:
						if(recvSize >= sizeof(request_keep_alive)) {
							User * user = users[ip_port_str];
							if(user != NULL) {
								cout << "Got keep alive from " << user->name << endl;
							} else {
								cerr << "Got keep-alive from nonexistent user" << endl;
							}
						}
						break;
				
					default: cerr << "Unrecognized packet type " << buf->req_type << endl;
				}
			} else {
				cerr << "Expected a packet to have at least " << sizeof(request) << " bytes, but got " << recvSize << " bytes." << endl;
			}
		}
	}
    
}
