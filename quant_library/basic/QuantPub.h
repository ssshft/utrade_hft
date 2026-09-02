#ifndef _QUANT_PUB_H
#define _QUANT_PUB_H

#include "DataStruct.h"
#include "command_helper.h"


class QuantPub {
public:
    static QuantPub& Instance() {
        static QuantPub quantPub;
        return quantPub;
    }

    ~QuantPub() {}

    // void SetPub(RedisClient* redisClient) {
    //     client = redisClient;
    // }

    void SetPubChannel(string channel) {
        pubChannel = channel;
    }

    void Publish(string content) {
        msgQueue.Push(content);        
    }

   void Run() {
    /*
        while (running) {
            try {
			    string s;
			    if (msgQueue.Pop(s)) {
                    client->publish(pubChannel.c_str(), s.c_str());
			    }
            } catch(exception& e) {
            }
            usleep(1);
        }
        */
    }

private:
    QuantPub() {
        //client = nullptr;
        running = true;
        runningThread = new thread(&QuantPub::Run, this);
    }

    //RedisClient* client;
    bool running;
    thread* runningThread;
    RQUEUE msgQueue;
    string pubChannel;
};

#endif