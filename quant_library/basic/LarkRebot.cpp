#include "LarkRebot.h"
#include "StrategyConfig.h"
#include "config.h"


LarkRebot::LarkRebot() {
	url = StrategyConfig::GetInstance().GetLarkUrl();
	//url = "https://open.larksuite.com/open-apis/bot/v2/hook/8658c092-bf13-48a1-9c85-d73b70b54219";
	authorization = "wRvT9^ZsgP";;
	channel = 0;
	groupUrl = url + "/messages/sendByGroup";
    voiceCallUrl = url + "/voice/call_feishu";
	groupVoiceCallUrl = url + "/voice/call_feishu_group";

	running = true;
    runningThread = new thread(&LarkRebot::Run, this);

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
			string s;
			if (rLarkMsg.Pop(s)) {
				SendMsg(tag + "  " + s);
			}
        } catch(exception& e) {
        }
        usleep(1000);
    }
}

void LarkRebot::SendMsg(string msg) {
	/*
	try {
		web::http::client::http_client_config config;
        config.set_timeout(utility::seconds(5));
		web::http::client::http_client restclient(url, config);
    	web::http::http_request request(web::http::methods::POST);

    	web::json::value body;
    	body["msg_type"] = web::json::value::string("text");
		web::json::value text;
		text["text"] = web::json::value::string(msg);
		body["content"] = text;
		
    	request.set_body(body.serialize(), "application/json; charset=UTF-8");
    	request.headers().set_content_type("application/json; charset=UTF-8");
		LOG_INFO("SendMsg body: %s", body.to_string().c_str());

    	restclient.request(request).then([](web::http::http_response response) -> pplx::task<web::json::value> {
			auto code = response.status_code();
        	if (code == web::http::status_codes::OK || code == web::http::status_codes::BadRequest || code == web::http::status_codes::TooManyRequests || code == web::http::status_codes::Unauthorized) {
                return response.extract_json();
			}
        	throw exception();
        	return pplx::task_from_result(web::json::value());
        })
        .then([&](pplx::task<web::json::value> previousTask) {
			web::json::value const& content = previousTask.get();
        })
        .wait();
	} catch(exception e) {
        std::cerr << "LarkRebot SendMsg error: " << e.what() << std::endl;
    } catch (...) {
		std::cerr << "LarkRebot SendMsg caught a unknown exception!" << std::endl;
	}
	*/
}

void LarkRebot::SendMsg(string msg, ReceiveInfo& receiveInfo) {
	/*
	try {
		web::http::client::http_client_config config;
        config.set_timeout(utility::seconds(5));
		web::http::client::http_client restclient(url, config);
    	web::http::http_request request(web::http::methods::POST);

    	web::json::value body;
		body["authorization"] = web::json::value::string(authorization);
		body["receive_id"] = web::json::value::string(receiveInfo.id);
		body["receive_id_type"] = web::json::value::string(receiveInfo.type);
    	body["msg_type"] = web::json::value::string("text");
		body["content"] = web::json::value::string(msg);
		char data[8];
		sprintf(data, "%d", channel);
		body["channel"] = web::json::value::string(data);
		
    	request.set_body(body.serialize(), "application/json; charset=UTF-8");
    	request.headers().set_content_type("application/json; charset=UTF-8");
		LOG_INFO("SendMsg body: %s", body.to_string().c_str());

    	restclient.request(request).then([](web::http::http_response response) -> pplx::task<web::json::value> {
			auto code = response.status_code();
        	if (code == web::http::status_codes::OK || code == web::http::status_codes::BadRequest || code == web::http::status_codes::TooManyRequests || code == web::http::status_codes::Unauthorized) {
                return response.extract_json();
			}
        	throw exception();
        	return pplx::task_from_result(web::json::value());
        })
        .then([&](pplx::task<web::json::value> previousTask) {
			web::json::value const& content = previousTask.get();
        })
        .wait();
	} catch(exception e) {
        std::cerr << "LarkRebot SendMsg error: " << e.what() << std::endl;
    } catch (...) {
		std::cerr << "LarkRebot SendMsg caught a unknown exception!" << std::endl;
	}
	*/
}

void LarkRebot::SendGroupMsg(string msg, ReceiveGroupInfo& receiveGroupInfo) {
	/*
	try {
		web::http::client::http_client_config config;
        config.set_timeout(utility::seconds(5));
		web::http::client::http_client restclient(groupUrl, config);
    	web::http::http_request request(web::http::methods::POST);

    	web::json::value body;
		body["authorization"] = web::json::value::string(authorization);
		body["group_code"] = web::json::value::string(receiveGroupInfo.code);
		body["importance"] = web::json::value::string(receiveGroupInfo.importance);
    	body["msg_type"] = web::json::value::string("text");
		body["content"] = web::json::value::string(msg);
		char data[8];
		sprintf(data, "%d", channel);
		body["channel"] = web::json::value::string(data);
    	request.set_body(body.serialize(), "application/json; charset=UTF-8");
    	request.headers().set_content_type("application/json; charset=UTF-8");
		LOG_INFO("SendGroupMsg body: %s", body.to_string().c_str());

    	restclient.request(request).then([](web::http::http_response response) -> pplx::task<web::json::value> {
			auto code = response.status_code();
        	if (code == web::http::status_codes::OK || code == web::http::status_codes::BadRequest || code == web::http::status_codes::TooManyRequests || code == web::http::status_codes::Unauthorized) {
                return response.extract_json();
			}
        	throw exception();
        	return pplx::task_from_result(web::json::value());
        })
        .then([&](pplx::task<web::json::value> previousTask) {
			web::json::value const& content = previousTask.get();
        })
        .wait();
	} catch(exception e) {
        std::cerr << "LarkRebot SendGroupMsg error: " << e.what() << std::endl;
    } catch (...) {
		std::cerr << "LarkRebot SendGroupMsg caught a unknown exception!" << std::endl;
	}
	*/
}

void LarkRebot::SendGroupMsgCard(MsgCard& msgCard, ReceiveGroupInfo& receiveGroupInfo) {
	/*
	try {
		web::http::client::http_client_config config;
        config.set_timeout(utility::seconds(5));
		web::http::client::http_client restclient(groupUrl, config);
    	web::http::http_request request(web::http::methods::POST);

    	web::json::value body;
		body["authorization"] = web::json::value::string(authorization);
		body["group_code"] = web::json::value::string(receiveGroupInfo.code);
		body["importance"] = web::json::value::string(receiveGroupInfo.importance);
    	body["msg_type"] = web::json::value::string("text");
		char data[8];
		sprintf(data, "%d", msgCard.templateId);
		body["template_id"] = web::json::value::string(data);

		web::json::value content;
		content["title"] = web::json::value::string(msgCard.title);
		content["object"] = web::json::value::string(msgCard.object);
		content["datetime"] = web::json::value::string(msgCard.datetime);
		content["content"] = web::json::value::string(msgCard.content);
		body["content"] = web::json::value::string(content.serialize());
		sprintf(data, "%d", channel);
		body["channel"] = web::json::value::string(data);

    	request.set_body(body.serialize(), "application/json; charset=UTF-8");
    	request.headers().set_content_type("application/json; charset=UTF-8");
		LOG_INFO("SendGroupMsgCard body: %s", body.to_string().c_str());

    	restclient.request(request).then([](web::http::http_response response) -> pplx::task<web::json::value> {
			auto code = response.status_code();
        	if (code == web::http::status_codes::OK || code == web::http::status_codes::BadRequest || code == web::http::status_codes::TooManyRequests || code == web::http::status_codes::Unauthorized) {
                return response.extract_json();
			}
        	throw exception();
        	return pplx::task_from_result(web::json::value());
        })
        .then([&](pplx::task<web::json::value> previousTask) {
			web::json::value const& content = previousTask.get();
        })
        .wait();
	} catch(exception e) {
        std::cerr << "LarkRebot SendGroupMsgCard error: " << e.what() << std::endl;
    } catch (...) {
		std::cerr << "LarkRebot SendGroupMsgCard caught a unknown exception!" << std::endl;
	}
	*/
}

void LarkRebot::SendVoiceCall(string msg, string receiveId) {
	/*
	int64_t currentTime = gettickcount();
	int64_t lastSendTime = 0;
	auto iter = mIdTime.find(receiveId);
	if (iter != mIdTime.end()) {
		lastSendTime = iter->second;
	}

	try {
		web::http::client::http_client_config config;
        config.set_timeout(utility::seconds(10));
		web::http::client::http_client restclient(voiceCallUrl, config);
    	web::http::http_request request(web::http::methods::POST);

    	web::json::value body;
		body["authorization"] = web::json::value::string(authorization);
		body["receive_id"] = web::json::value::string(receiveId);
		body["content"] = web::json::value::string(msg);

    	request.set_body(body.serialize(), "application/json; charset=UTF-8");
    	request.headers().set_content_type("application/json; charset=UTF-8");
		LOG_INFO("SendVoiceCall body: %s", body.to_string().c_str());

    	restclient.request(request).then([](web::http::http_response response) -> pplx::task<web::json::value> {
			auto code = response.status_code();
        	if (code == web::http::status_codes::OK || code == web::http::status_codes::BadRequest || code == web::http::status_codes::TooManyRequests || code == web::http::status_codes::Unauthorized) {
                return response.extract_json();
			}
        	throw exception();
        	return pplx::task_from_result(web::json::value());
        })
        .then([&](pplx::task<web::json::value> previousTask) {
			web::json::value const& content = previousTask.get();
        		std::cerr << "LarkRebot SendVoiceCall content: " << content.to_string() << std::endl;
        })
        .wait();
	} catch(exception e) {
        std::cerr << "LarkRebot SendVoiceCall error: " << e.what() << std::endl;
    } catch (...) {
		std::cerr << "LarkRebot SendVoiceCall caught a unknown exception!" << std::endl;
	}
	*/
}

void LarkRebot::SendGroupVoiceCall(string msg, string groupId, vector<string>& vUserId) {
	/*
	if (groupId.size() <= 0) {
		return;
	}

	int64_t currentTime = gettickcount();
	int64_t lastSendTime = 0;
	auto iter = mIdTime.find(groupId);
	if (iter != mIdTime.end()) {
		lastSendTime = iter->second;
	}

	mIdTime[groupId] = currentTime;

	try {
		web::http::client::http_client_config config;
        config.set_timeout(utility::seconds(10));
		web::http::client::http_client restclient(groupVoiceCallUrl, config);
    	web::http::http_request request(web::http::methods::POST);

    	web::json::value body;
		body["authorization"] = web::json::value::string(authorization);
		body["group_id"] = web::json::value::string(groupId);
		body["content"] = web::json::value::string(msg);

		if (vUserId.size() > 0) {
			vector<web::json::value> v;
			for (size_t i = 0; i < vUserId.size(); ++i) {
				web::json::value userId = web::json::value::string(vUserId[i]);
    			v.push_back(userId);
			}
    		body["user_ids"] = web::json::value::array(v);
		}


    	request.set_body(body.serialize(), "application/json; charset=UTF-8");
    	request.headers().set_content_type("application/json; charset=UTF-8");

		LOG_INFO("SendGroupVoiceCall body: %s", body.to_string().c_str());

    	restclient.request(request).then([](web::http::http_response response) -> pplx::task<web::json::value> {
			auto code = response.status_code();
        	if (code == web::http::status_codes::OK || code == web::http::status_codes::BadRequest || code == web::http::status_codes::TooManyRequests || code == web::http::status_codes::Unauthorized) {
                return response.extract_json();
			}
        	throw exception();
        	return pplx::task_from_result(web::json::value());
        })
        .then([&](pplx::task<web::json::value> previousTask) {
			web::json::value const& content = previousTask.get();
        		std::cerr << "LarkRebot SendGroupVoiceCall content: " << content.to_string() << std::endl;
        })
        .wait();
	} catch(exception e) {
        std::cerr << "LarkRebot SendGroupVoiceCall error: " << e.what() << std::endl;
    } catch (...) {
		std::cerr << "LarkRebot SendGroupVoiceCall caught a unknown exception!" << std::endl;
	}
	*/
}
