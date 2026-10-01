#include "LarkRebot.h"
#include "StrategyConfig.h"
#include "config.h"
#include "BeastRestClient.h"


LarkRebot::LarkRebot() {
	url = StrategyConfig::GetInstance().GetLarkUrl();

	running = true;
    runningThread = new std::thread(&LarkRebot::Run, this);

	Config* config = Config::instance();
	config->get_string("tag", tag);
}

LarkRebot::~LarkRebot() {
}

LarkRebot& LarkRebot::GetInstance() {
	static LarkRebot larkRebot;
	return larkRebot;
}

void LarkRebot::Run() {
    while (running) {
        try {
			std::string s;
			if (queue.Pop(s)) {
				Send(tag + "  " + s);
			}
        } catch(std::exception& e) {
        }
        usleep(1000);
    }
}

void LarkRebot::SendMsg(const std::string& msg) {
	queue.Push(msg);
}

void LarkRebot::Send(const std::string& s) {
	size_t scheme_pos = url.find("://");
	if (scheme_pos != std::string::npos) {
		url = url.substr(scheme_pos + 3);
	}

	// 查找第一个 /
	size_t slash_pos = url.find('/');
	std::string host = url.substr(0, slash_pos);
	std::string target = url.substr(slash_pos);
	
	std::string body_in = fmt::format("{{\"msg_type\":\"text\",\"content\":{{\"text\":\"{}\"}}}}", s);

   	int status = 0;
    std::string body;
    try {
        if (!Net::Instance().syncPost("lark", host, target, body_in, {}, {}, body, status)) {
            LOG_ERROR("SendLarkMsg syncPost return false");
            return;
        }
        if (status != 200) {
            LOG_INFO("SendLarkMsg status: {}", status);
            return;
        }
    }
    catch(exception& e) {
        LOG_INFO("SendLarkMsg Error: {}", e.what());
    }
}
