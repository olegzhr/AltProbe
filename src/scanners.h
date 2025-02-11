/*
 *   Copyright 2021 Oleg Zharkov
 *
 *   Licensed under the Apache License, Version 2.0 (the "License").
 *   You may not use this file except in compliance with the License.
 *   A copy of the License is located at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   or in the "license" file accompanying this file. This file is distributed
 *   on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *   express or implied. See the License for the specific language governing
 *   permissions and limitations under the License.
 */

#ifndef SCANNERS_H
#define	SCANNERS_H

#include <boost/asio.hpp>
#include "base64.h"
#include "controller.h"
#include "filters.h"

using boost::asio::ip::tcp;
using namespace std;

class Scanners : public Controller,
        public ExceptionListener,
        public MessageListener {
public: 
    
    Destination* consumerCommand;
    MessageConsumer* consumer;
    int update_status;
        
    Posture posture;
    std::stringstream strStream, comp;
    
    FiltersSingleton fs;
        
    Scanners() {
        consumer = NULL;
        update_status = 0;
    }
        
    virtual int Open(int mode, pid_t pid);
    virtual void Close();
    virtual int GetConfig();
    int GetStatus() {
        return update_status;
    }
    
    int Go();
    void onMessage(const Message* message);
    void onException(const CMSException& ex AMQCPP_UNUSED);
    string onTextMessage(const Message* message, string corr_id);
   
    string ScanKubeHunter(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanTrivy(string project, string target, int delay, int param, string alert_corr, string corr_id);
    string ScanZap(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanNmap(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanNuclei(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanNikto(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanCloudsploit(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanSemgrep(string project, string target, int delay, string alert_corr, string corr_id);
    string ScanSonarqube(string project, string target, int delay, string alert_corr, string corr_id);
        
    void ResetStreams() {
        comp.str("");
        comp.clear();
        strStream.str("");
        strStream.clear();
    }
    
};

#endif	/* SCANNERS_H */

