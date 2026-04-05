#ifndef _QUANT_DBP_H
#define _QUANT_DBP_H

#include "DataStruct.h"
#include "command_helper.h"
#include "dbp/dbpreader.h"


class QuantDbp {
public:
    static QuantDbp& Instance() {
        static QuantDbp quantDbp;
        return quantDbp;
    }

    ~QuantDbp() {}

    void SetDbp(dbp::DbpReader* dbp) {
        dbpreader = dbp;
    }

    void Subscribe(string spreadInstId) {
        if (dbpreader) {
            LOG_INFO("Subscribe dbpreader spreadInstId:%s", spreadInstId.c_str());
            dbpreader->Subscribe(spreadInstId);
        } else {
            LOG_INFO("Subscribe dbpreader is null");
        }
    }

    void UnSubscribe(string spreadInstId) {
        if (dbpreader) {
            LOG_INFO("UnSubscribe dbpreader spreadInstId:%s", spreadInstId.c_str());
            dbpreader->UnSubscribe(spreadInstId);
        } else {
            LOG_INFO("UnSubscribe dbpreader is null");
        }
    }

private:
    QuantDbp() {
        dbpreader = nullptr;
    }

    dbp::DbpReader* dbpreader;
};

#endif